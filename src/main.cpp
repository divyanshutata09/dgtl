/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

#include <cctype>
#include <cstdint>
#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "dgtl/all.hpp"
// Add these for Titan-Stable integration:
#include "dgtl/jit/runtime/Hardware_Scheduler.hpp"
#include "dgtl/pipeline/Pass_Decision_Engine.hpp"
#include <thread>
#ifdef _WIN32
    #include <windows.h>
#endif

namespace {

enum class ExitCode : int {
    ok = 0,
    invalid_args = 2,
    pipeline_failure = 3,
    emit_failure = 4,
    io_failure = 5,
    internal_error = 6,
};

enum class ParseStatus : int {
    ok,
    help_requested,
    error,
};

enum class BackendKind : uint8_t {
    arm64,
    x64,
    nasm,
    wasm,
    elfobj,
    staticlib,
    coffobj,
    cofflib,
    macho,
};

enum class ReleaseIRMode : uint8_t {
    integer,
    floating,
    floating_simd_widen,
    floating_simd_narrow,
};

enum class PolyhedralMode : uint8_t {
    off,
    safe,
    aggressive,
};

enum class PolyhedralParallelPolicy : uint8_t {
    auto_policy,
    off,
    on,
};

struct AppOptions {
    bool verbose = true;
    bool print_output = true;
    BackendKind backend = BackendKind::arm64;
    std::string backend_name = "arm64";
    ReleaseIRMode ir_mode = ReleaseIRMode::integer;
    std::string ir_mode_name = "int";
    PolyhedralMode polyhedral_mode = PolyhedralMode::off;
    std::string polyhedral_mode_name = "off";
    uint32_t polyhedral_tile = 0;
    PolyhedralParallelPolicy polyhedral_parallel = PolyhedralParallelPolicy::auto_policy;
    std::string polyhedral_parallel_name = "auto";
    std::string output_path;
    std::string frontend_payload_path;
    bool jit_execute = false;
};

struct EmissionResult {
    bool ok = false;
    bool emitted_binary = false;
    std::string emitted_text;
    std::vector<uint8_t> emitted_bytes;
};

void print_usage(const char* argv0) {
    std::cout
        << "DGTL Production Driver\n"
        << "Usage: " << argv0 << " [options]\n\n"
        << "Options:\n"
        << "  --help               Show this help\n"
        << "  --quiet              Reduce terminal output\n"
        << "  --no-print           Do not print emitted output to stdout\n"
        << "  --backend <name>     Select backend: arm64|x64|nasm|wasm|elfobj|staticlib|coffobj|cofflib|macho\n"
        << "  --ir-mode <mode>     Select release IR: int|float|float-simd-widen|float-simd-narrow (default: int)\n"
        << "  --polyhedral-mode <mode>  Polyhedral pipeline mode: off|safe|aggressive (default: off)\n"
        << "  --polyhedral-tile <n>     Polyhedral tile size (0 means auto)\n"
        << "  --polyhedral-parallel <p> Polyhedral parallel policy: auto|off|on (default: auto)\n"
        << "  --frontend-payload <path> Load universal frontend payload IR module\n"
        << "  --emit-output <path> Write emitted output to file\n"
        << "  --jit-execute        Compile and execute x64 code via StrikeOrchestrator\n";
}

std::string to_lower_ascii(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

bool parse_backend(std::string backend_str, BackendKind& out_kind, std::string& out_name) {
    backend_str = to_lower_ascii(std::move(backend_str));
    if (backend_str == "arm64") {
        out_kind = BackendKind::arm64;
        out_name = backend_str;
        return true;
    }
    if (backend_str == "x64") {
        out_kind = BackendKind::x64;
        out_name = backend_str;
        return true;
    }
    if (backend_str == "nasm") {
        out_kind = BackendKind::nasm;
        out_name = backend_str;
        return true;
    }
    if (backend_str == "wasm") {
        out_kind = BackendKind::wasm;
        out_name = backend_str;
        return true;
    }
    if (backend_str == "elfobj" || backend_str == "obj" || backend_str == "o") {
        out_kind = BackendKind::elfobj;
        out_name = "elfobj";
        return true;
    }
    if (backend_str == "staticlib" || backend_str == "archive" || backend_str == "a" || backend_str == "lib") {
        out_kind = BackendKind::staticlib;
        out_name = "staticlib";
        return true;
    }
    if (backend_str == "coffobj" || backend_str == "coff") {
        out_kind = BackendKind::coffobj;
        out_name = "coffobj";
        return true;
    }
    if (backend_str == "cofflib" || backend_str == "coffarchive") {
        out_kind = BackendKind::cofflib;
        out_name = "cofflib";
        return true;
    }
    if (backend_str == "macho" || backend_str == "mach-o" || backend_str == "macho64") {
        out_kind = BackendKind::macho;
        out_name = "macho";
        return true;
    }
    return false;
}

bool parse_ir_mode(std::string mode_str, ReleaseIRMode& out_mode, std::string& out_name) {
    mode_str = to_lower_ascii(std::move(mode_str));
    if (mode_str == "int" || mode_str == "integer") {
        out_mode = ReleaseIRMode::integer;
        out_name = "int";
        return true;
    }
    if (mode_str == "float" || mode_str == "fp32") {
        out_mode = ReleaseIRMode::floating;
        out_name = "float";
        return true;
    }
    if (mode_str == "float-simd-widen" || mode_str == "fcvtl") {
        out_mode = ReleaseIRMode::floating_simd_widen;
        out_name = "float-simd-widen";
        return true;
    }
    if (mode_str == "float-simd-narrow" || mode_str == "fcvtn") {
        out_mode = ReleaseIRMode::floating_simd_narrow;
        out_name = "float-simd-narrow";
        return true;
    }
    return false;
}

bool parse_polyhedral_mode(std::string mode_str, PolyhedralMode& out_mode, std::string& out_name) {
    mode_str = to_lower_ascii(std::move(mode_str));
    if (mode_str == "off") {
        out_mode = PolyhedralMode::off;
        out_name = "off";
        return true;
    }
    if (mode_str == "safe") {
        out_mode = PolyhedralMode::safe;
        out_name = "safe";
        return true;
    }
    if (mode_str == "aggressive") {
        out_mode = PolyhedralMode::aggressive;
        out_name = "aggressive";
        return true;
    }
    return false;
}

bool parse_polyhedral_parallel_policy(std::string policy_str,
                                      PolyhedralParallelPolicy& out_policy,
                                      std::string& out_name) {
    policy_str = to_lower_ascii(std::move(policy_str));
    if (policy_str == "auto") {
        out_policy = PolyhedralParallelPolicy::auto_policy;
        out_name = "auto";
        return true;
    }
    if (policy_str == "off") {
        out_policy = PolyhedralParallelPolicy::off;
        out_name = "off";
        return true;
    }
    if (policy_str == "on") {
        out_policy = PolyhedralParallelPolicy::on;
        out_name = "on";
        return true;
    }
    return false;
}

// === P0-001: CONFIGURATION CONVERSION ===
dgtl::core::PolyhedralConfig build_polyhedral_config(const AppOptions& opts) {
    dgtl::core::PolyhedralConfig config;

    // Map AppOptions PolyhedralMode to PolyhedralConfig mode
    if (opts.polyhedral_mode == PolyhedralMode::off) {
        config.mode = dgtl::core::PolyhedralMode::off;
    } else if (opts.polyhedral_mode == PolyhedralMode::safe) {
        config.mode = dgtl::core::PolyhedralMode::safe;
    } else if (opts.polyhedral_mode == PolyhedralMode::aggressive) {
        config.mode = dgtl::core::PolyhedralMode::aggressive;
    }

    // Map parallel policy
    if (opts.polyhedral_parallel == PolyhedralParallelPolicy::auto_policy) {
        config.parallel_policy = dgtl::core::PolyhedralParallelPolicy::auto_policy;
    } else if (opts.polyhedral_parallel == PolyhedralParallelPolicy::off) {
        config.parallel_policy = dgtl::core::PolyhedralParallelPolicy::off;
    } else if (opts.polyhedral_parallel == PolyhedralParallelPolicy::on) {
        config.parallel_policy = dgtl::core::PolyhedralParallelPolicy::on;
    }

    config.tile_size = opts.polyhedral_tile;

    return config;
}

dgtl::core::ArchType backend_to_arch_type(BackendKind backend) {
    switch (backend) {
        case BackendKind::x64:
            return dgtl::core::select_preferred_x86_arch();
        case BackendKind::arm64:
            return dgtl::core::ArchType::AARCH64_SVE;
        case BackendKind::nasm:
            return dgtl::core::select_preferred_x86_arch();  // NASM defaults to host preferred x64 SIMD tier
        case BackendKind::wasm:
            return dgtl::core::ArchType::WASM_SIMD;
        case BackendKind::elfobj:
            return dgtl::core::select_preferred_x86_arch();
        case BackendKind::staticlib:
            return dgtl::core::select_preferred_x86_arch();
        case BackendKind::coffobj:
            return dgtl::core::select_preferred_x86_arch();
        case BackendKind::cofflib:
            return dgtl::core::select_preferred_x86_arch();
        case BackendKind::macho:
            return dgtl::core::ArchType::AARCH64_SVE;
        default:
            return dgtl::core::select_preferred_x86_arch();
    }
}

ParseStatus parse_args(int argc, char** argv, AppOptions& opts) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--help") {
            print_usage(argv[0]);
            return ParseStatus::help_requested;
        }
        if (arg == "--quiet") {
            opts.verbose = false;
            continue;
        }
        if (arg == "--no-print") {
            opts.print_output = false;
            continue;
        }
        if (arg == "--backend") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --backend\n";
                return ParseStatus::error;
            }
            if (!parse_backend(argv[++i], opts.backend, opts.backend_name)) {
                std::cerr << "Unsupported backend: " << argv[i] << "\n";
                return ParseStatus::error;
            }
            continue;
        }
        if (arg == "--ir-mode") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --ir-mode\n";
                return ParseStatus::error;
            }
            if (!parse_ir_mode(argv[++i], opts.ir_mode, opts.ir_mode_name)) {
                std::cerr << "Unsupported IR mode: " << argv[i] << "\n";
                return ParseStatus::error;
            }
            continue;
        }
        if (arg == "--polyhedral-mode") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --polyhedral-mode\n";
                return ParseStatus::error;
            }
            if (!parse_polyhedral_mode(argv[++i], opts.polyhedral_mode, opts.polyhedral_mode_name)) {
                std::cerr << "Unsupported polyhedral mode: " << argv[i] << "\n";
                return ParseStatus::error;
            }
            continue;
        }
        if (arg == "--polyhedral-tile") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --polyhedral-tile\n";
                return ParseStatus::error;
            }
            try {
                const unsigned long long parsed = std::stoull(argv[++i]);
                opts.polyhedral_tile = static_cast<uint32_t>(parsed);
            } catch (const std::exception&) {
                std::cerr << "Invalid value for --polyhedral-tile: " << argv[i] << "\n";
                return ParseStatus::error;
            }
            continue;
        }
        if (arg == "--polyhedral-parallel") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --polyhedral-parallel\n";
                return ParseStatus::error;
            }
            if (!parse_polyhedral_parallel_policy(argv[++i],
                                                  opts.polyhedral_parallel,
                                                  opts.polyhedral_parallel_name)) {
                std::cerr << "Unsupported polyhedral parallel policy: " << argv[i] << "\n";
                return ParseStatus::error;
            }
            continue;
        }
        if (arg == "--emit-output") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --emit-output\n";
                return ParseStatus::error;
            }
            opts.output_path = argv[++i];
            if (opts.output_path.empty()) {
                std::cerr << "Empty path for --emit-output is not allowed\n";
                return ParseStatus::error;
            }
            continue;
        }

        if (arg == "--frontend-payload") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --frontend-payload\n";
                return ParseStatus::error;
            }
            opts.frontend_payload_path = argv[++i];
            if (opts.frontend_payload_path.empty()) {
                std::cerr << "Empty path for --frontend-payload is not allowed\n";
                return ParseStatus::error;
            }
            continue;
        }
        if (arg == "--jit-execute") {
            opts.jit_execute = true;
            continue;
        }

        std::cerr << "Unknown option: " << arg << "\n";
        return ParseStatus::error;
    }

    return ParseStatus::ok;
}

bool build_release_ir(dgtl::ir::IRStream& stream) {
    dgtl::pipeline::probes::build_release_int_probe(stream);
    return stream.size() == 5;
}

bool build_release_ir_float(dgtl::ir::IRStream& stream) {
    dgtl::pipeline::probes::build_release_float_probe(stream);
    return stream.size() == 3;
}

bool build_release_ir_float_simd_widen(dgtl::ir::IRStream& stream) {
    dgtl::pipeline::probes::build_fcvtl_probe(stream, 21, 10);
    return stream.size() == 2;
}

bool build_release_ir_float_simd_narrow(dgtl::ir::IRStream& stream) {
    dgtl::pipeline::probes::build_fcvtn_probe(stream, 22, 11);
    return stream.size() == 2;
}

bool build_release_ir_for_mode(dgtl::ir::IRStream& stream, ReleaseIRMode mode) {
    if (mode == ReleaseIRMode::floating) {
        return build_release_ir_float(stream);
    }
    if (mode == ReleaseIRMode::floating_simd_widen) {
        return build_release_ir_float_simd_widen(stream);
    }
    if (mode == ReleaseIRMode::floating_simd_narrow) {
        return build_release_ir_float_simd_narrow(stream);
    }
    return build_release_ir(stream);
}

void run_core_optimizations(dgtl::ir::IRStream& stream, ReleaseIRMode mode) {
    (void)mode;
    dgtl::analysis::DGTLAICostModel cost_model;
    dgtl::pipeline::DGTLAnalysisManager am;
    dgtl::pipeline::PassDecisionEngine pde;
    dgtl::jit::DGTLHardwareScheduler scheduler(cost_model);

    // 1. Global Value Numbering (Base)
    stream.perform_gvn();

    // 2. PDE-Gated Instruction Fusion
    dgtl::core::DGTLFormalVerifier verifier;
    dgtl::pass::DGTLInstructionFusionSynth fusion(verifier);
    dgtl::graph::Region region;
    if (pde.should_run_pass(&fusion, &region, am, cost_model)) {
        fusion.execute(stream);
    }

    // 3. Hardware-Aware Port Scheduling
    scheduler.schedule(stream);

    // 4. SIMD legalization routed by preferred host policy (AVX2 dual-pump first).
    const dgtl::pass::TargetArch simd_arch = []() noexcept {
        switch (dgtl::core::preferred_simd_tier()) {
            case dgtl::core::SimdTier::AVX512:
                return dgtl::pass::TargetArch::X86_AVX512;
            case dgtl::core::SimdTier::AVX2:
                return dgtl::pass::TargetArch::X86_AVX2;
            case dgtl::core::SimdTier::Scalar:
            default:
                return dgtl::pass::TargetArch::GENERIC;
        }
    }();
    if (simd_arch != dgtl::pass::TargetArch::GENERIC) {
        dgtl::core::prime_preferred_simd_units();
    }
    dgtl::pass::DGTLSIMDLegalizer legalizer(simd_arch);
    legalizer.legalize(stream);

    // 5. Final Cleanup
    stream.perform_dead_code_elimination();
}
// === P0-001: POLYHEDRAL ANALYSIS PIPELINE ===
void run_polyhedral_analysis(dgtl::ir::IRStream& stream,
                             const dgtl::core::PolyhedralConfig& poly_config,
                             const dgtl::core::TargetMachine& target,
                             bool verbose) {
    if (!poly_config.is_enabled()) {
        if (verbose) {
            std::cout << "[DGTL-POLY] Polyhedral analysis disabled\n";
        }
        return;
    }

    if (verbose) {
        std::cout << "[DGTL-POLY] Building execution graph for SCoP detection...\n";
    }

    // Build execution graph and run SCoP detection (P0-002)
    dgtl::graph::ExecutionGraph exec_graph;
    exec_graph.build_from_ir(stream);

    // Schedule regions based on target
    exec_graph.schedule(target);

    // Count SCoP classifications
    uint32_t affine_count = 0;
    uint32_t non_affine_count = 0;

    for (const auto& region : exec_graph.regions) {
        if (region && region->scop_annotation.affinity == dgtl::graph::ScopAffinity::AFFINE_CANDIDATE) {
            affine_count++;
            if (verbose) {
                std::cout << "[DGTL-POLY]   Region: affine (depth=" << region->scop_annotation.loop_depth
                          << ", can_tile=" << (region->scop_annotation.can_tile ? "yes" : "no")
                          << ", can_parallelize=" << (region->scop_annotation.can_parallelize ? "yes" : "no")
                          << ")\n";
            }
        } else {
            non_affine_count++;
        }
    }

    if (verbose) {
        std::cout << "[DGTL-POLY] SCoP detection complete: "
                  << affine_count << " affine candidates, "
                  << non_affine_count << " non-affine regions\n";
    }

    // PLACEHOLDER: Next phases (P0-003+) would apply transforms here based on mode
    if (poly_config.allow_aggressive_transforms()) {
        if (verbose) {
            std::cout << "[DGTL-POLY] Aggressive mode enabled (transforms queued for future passes)\n";
        }
    } else {
        if (verbose) {
            std::cout << "[DGTL-POLY] Safe mode enabled (conservative transform policy)\n";
        }
    }
}

bool emit_arm64(const dgtl::ir::IRStream& stream,
                const dgtl::core::SymbolTable& symbols,
                std::string& out_text) {
    dgtl::isa::ARM64Emitter emitter(stream, symbols);
    dgtl::isa::ARM64Emitter::Config cfg;
    cfg.verbose_asm = true;

    std::ostringstream oss;
    emitter.emit_assembly(oss, cfg);
    out_text = oss.str();
    return !out_text.empty();
}

std::string bytes_to_hex(const std::vector<uint8_t>& bytes) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(bytes.size() * 3);
    for (size_t i = 0; i < bytes.size(); ++i) {
        const uint8_t b = bytes[i];
        out.push_back(kHex[(b >> 4) & 0xF]);
        out.push_back(kHex[b & 0xF]);
        if (i + 1 < bytes.size()) {
            out.push_back(' ');
        }
    }
    return out;
}

bool emit_x64(const dgtl::ir::IRStream& stream,
              std::vector<uint8_t>& out_bytes,
              std::string& out_text) {
    out_bytes.clear();
    out_text.clear();

    dgtl::isa::X64StrikeEmitter emitter;
    emitter.process_stream(stream, out_bytes);
    if (out_bytes.empty()) {
        return false;
    }

    out_text = bytes_to_hex(out_bytes);
    return true;
}

bool emit_nasm(const dgtl::ir::IRStream& stream,
               const dgtl::core::SymbolTable& symbols,
               std::string& out_text) {
    out_text.clear();
    dgtl::isa::DGTLNasmEmitter emitter;
    emitter.emit(stream, symbols);
    out_text = emitter.get_output();
    return !out_text.empty();
}

bool emit_wasm(const dgtl::ir::IRStream& stream,
               std::vector<uint8_t>& out_bytes,
               std::string& out_text) {
    out_bytes.clear();
    out_text.clear();

    dgtl::exec::DGTLWasmEncoder encoder;
    encoder.encode(stream);
    out_bytes = encoder.get_binary();
    if (out_bytes.empty()) {
        return false;
    }

    out_text = bytes_to_hex(out_bytes);
    return true;
}

bool emit_elfobj(const dgtl::ir::IRStream& stream,
                 std::vector<uint8_t>& out_bytes,
                 std::string& out_text) {
    out_bytes.clear();
    out_text.clear();

    dgtl::exec::DGTLUniversalLinker linker;
    if (!linker.build_elf_relocatable(out_bytes, stream, "DGTL_kernel_entry")) {
        return false;
    }

    out_text = "ELF relocatable object generated: " + std::to_string(out_bytes.size()) + " bytes";
    return true;
}

bool emit_staticlib(const dgtl::ir::IRStream& stream,
                    std::vector<uint8_t>& out_bytes,
                    std::string& out_text) {
    out_bytes.clear();
    out_text.clear();

    dgtl::exec::DGTLUniversalLinker linker;
    if (!linker.build_static_archive(out_bytes, stream, "module.o")) {
        return false;
    }

    out_text = "Static archive generated: " + std::to_string(out_bytes.size()) + " bytes";
    return true;
}

bool emit_coffobj(const dgtl::ir::IRStream& stream,
                  std::vector<uint8_t>& out_bytes,
                  std::string& out_text) {
    out_bytes.clear();
    out_text.clear();

    dgtl::exec::DGTLUniversalLinker linker;
    if (!linker.build_coff_relocatable(out_bytes, stream, "DGTL_kernel_entry")) {
        return false;
    }

    out_text = "COFF relocatable object generated: " + std::to_string(out_bytes.size()) + " bytes";
    return true;
}

bool emit_cofflib(const dgtl::ir::IRStream& stream,
                  std::vector<uint8_t>& out_bytes,
                  std::string& out_text) {
    out_bytes.clear();
    out_text.clear();

    dgtl::exec::DGTLUniversalLinker linker;
    if (!linker.build_coff_archive(out_bytes, stream, "module.obj", "DGTL_kernel_entry")) {
        return false;
    }

    out_text = "COFF archive generated: " + std::to_string(out_bytes.size()) + " bytes";
    return true;
}

bool emit_macho(const dgtl::ir::IRStream& stream,
                const dgtl::core::SymbolTable& symbols,
                std::vector<uint8_t>& out_bytes,
                std::string& out_text) {
    out_bytes.clear();
    out_text.clear();

    dgtl::isa::MachOEmitter emitter(stream, symbols);
    auto result = emitter.emit_to_binary();
    if (!result.success) {
        return false;
    }

    out_bytes = std::move(result.binary_data);
    out_text = "Mach-O ARM64 object generated: " + std::to_string(out_bytes.size()) + " bytes";
    return true;
}

bool emit_backend(BackendKind backend,
                  const dgtl::ir::IRStream& stream,
                  const dgtl::core::SymbolTable& symbols,
                  EmissionResult& result) {
    result = EmissionResult{};

    switch (backend) {
        case BackendKind::arm64:
            result.ok = emit_arm64(stream, symbols, result.emitted_text);
            result.emitted_binary = false;
            return result.ok;
        case BackendKind::x64:
            result.ok = emit_x64(stream, result.emitted_bytes, result.emitted_text);
            result.emitted_binary = true;
            return result.ok;
        case BackendKind::nasm:
            result.ok = emit_nasm(stream, symbols, result.emitted_text);
            result.emitted_binary = false;
            return result.ok;
        case BackendKind::wasm:
            result.ok = emit_wasm(stream, result.emitted_bytes, result.emitted_text);
            result.emitted_binary = true;
            return result.ok;
        case BackendKind::elfobj:
            result.ok = emit_elfobj(stream, result.emitted_bytes, result.emitted_text);
            result.emitted_binary = true;
            return result.ok;
        case BackendKind::staticlib:
            result.ok = emit_staticlib(stream, result.emitted_bytes, result.emitted_text);
            result.emitted_binary = true;
            return result.ok;
        case BackendKind::coffobj:
            result.ok = emit_coffobj(stream, result.emitted_bytes, result.emitted_text);
            result.emitted_binary = true;
            return result.ok;
        case BackendKind::cofflib:
            result.ok = emit_cofflib(stream, result.emitted_bytes, result.emitted_text);
            result.emitted_binary = true;
            return result.ok;
        case BackendKind::macho:
            result.ok = emit_macho(stream, symbols, result.emitted_bytes, result.emitted_text);
            result.emitted_binary = true;
            return result.ok;
    }

    return false;
}

bool write_text_file(const std::string& path, const std::string& content) {
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
        if (ec) {
            return false;
        }
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    return out.good();
}

bool write_binary_file(const std::string& path, const std::vector<uint8_t>& bytes) {
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
        if (ec) {
            return false;
        }
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    if (!bytes.empty()) {
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    return out.good();
}

bool build_ir_from_frontend_payload(const AppOptions& opts,
                                    dgtl::core::DGTL_Arena& arena,
                                    dgtl::ir::IRStream& out_stream,
                                    std::string& error) {
    dgtl::core::FrontendPayloadModule payload;
    if (!dgtl::core::read_frontend_payload_file(opts.frontend_payload_path, payload, &error)) {
        return false;
    }

    if (payload.functions.empty()) {
        error = "frontend payload contains no functions";
        return false;
    }

    const auto& first_fn = payload.functions.front();
    const size_t reserve_nodes = std::max<size_t>(256, first_fn.instructions.size() + 16);
    dgtl::core::DGTLFunction lowered(arena, first_fn.name, reserve_nodes);
    dgtl::core::DGTLControlFlowGraph cfg;
    dgtl::core::DGTLABIRegistry abi;

    // Pre-register Myzon runtime intrinsic ABI signatures
    {
        dgtl::core::DGTLABIParameter size_param{"size", dgtl::core::DGTLABIValueClass::i64, 0xFF, 0, 0};
        dgtl::core::DGTLABIParameter ptr_param{"ptr", dgtl::core::DGTLABIValueClass::pointer, 0xFF, 0, 0};
        std::vector<dgtl::core::DGTLABIParameter> one_i64{size_param};
        std::vector<dgtl::core::DGTLABIParameter> one_ptr{ptr_param};
        abi.register_internal_signature("__myzon_alloc_huge", dgtl::core::DGTLABIValueClass::pointer, 0, one_i64);
        abi.register_internal_signature("__myzon_free", dgtl::core::DGTLABIValueClass::i32, 0, one_ptr);

        // Pre-register all callee names found in the payload so cross-function
        // calls and user-level intrinsics pass ABI validation.
        for (const auto& fn : payload.functions) {
            std::vector<dgtl::core::DGTLABIParameter> params;
            for (const auto& p : fn.parameters) {
                params.push_back({p.name, dgtl::core::DGTLABIValueClass::i32, 0xFF, 0, p.type_handle});
            }
            abi.register_internal_signature(fn.name, fn.return_abi_class, fn.return_type_handle, params);
        }

        // Scan all call instructions for callee names not yet registered
        // (e.g. user-level intrinsics like verify_buffer) and auto-register
        // with a generic variadic signature.
        for (const auto& fn : payload.functions) {
            for (const auto& ins : fn.instructions) {
                if (ins.kind == dgtl::core::FrontendInstructionKind::call && !ins.callee_name.empty()) {
                    if (!abi.find_signature(ins.callee_name)) {
                        abi.register_internal_signature(
                            ins.callee_name,
                            dgtl::core::DGTLABIValueClass::i32, 0,
                            std::span<const dgtl::core::DGTLABIParameter>{},
                            /*is_variadic=*/true);
                    }
                }
            }
        }
    }

    const auto lower_result = dgtl::core::FrontendPipelineLowerer::lower_function(first_fn, lowered, cfg, &abi);
    if (!lower_result.ok) {
        error = lower_result.message;
        return false;
    }

    out_stream.copy_from(lowered.stream());
    if (out_stream.size() == 0) {
        error = "frontend payload lowered to empty stream";
        return false;
    }

    return true;
}

} // namespace

int main(int argc, char** argv) {
    try {
        AppOptions opts;
        const ParseStatus parse_status = parse_args(argc, argv, opts);
        if (parse_status == ParseStatus::help_requested) {
            return static_cast<int>(ExitCode::ok);
        }
        if (parse_status == ParseStatus::error) {
            return static_cast<int>(ExitCode::invalid_args);
        }

        if (opts.verbose) {
            std::cout << "[DGTL] Starting production pipeline\n";
        }

        dgtl::core::DGTL_Arena arena(1, 64);
        dgtl::core::SymbolTable symbols(arena);

        // === P0-001: CREATE CONTEXT WITH POLYHEDRAL CONFIG ===
        dgtl::core::DGTLContext context(arena);
        dgtl::core::PolyhedralConfig poly_config = build_polyhedral_config(opts);
        context.set_polyhedral_config(poly_config);

        dgtl::ir::IRStream stream(arena, 256);
        if (!opts.frontend_payload_path.empty()) {
            std::string payload_error;
            if (!build_ir_from_frontend_payload(opts, arena, stream, payload_error)) {
                std::cerr << "[DGTL] Failed to build IR from frontend payload: " << payload_error << "\n";
                return static_cast<int>(ExitCode::pipeline_failure);
            }
            if (opts.verbose) {
                std::cout << "[DGTL] Loaded frontend payload IR from " << opts.frontend_payload_path << "\n";
            }
        } else {
            if (!build_release_ir_for_mode(stream, opts.ir_mode)) {
                std::cerr << "[DGTL] Failed to build release IR\n";
                return static_cast<int>(ExitCode::pipeline_failure);
            }
        }

        run_core_optimizations(stream, opts.ir_mode);
        if (stream.size() == 0 || !stream.validate_shadow_integrity()) {
            std::cerr << "[DGTL] IR validation failed after optimization\n";
            return static_cast<int>(ExitCode::pipeline_failure);
        }

        // === P0-001: RUN POLYHEDRAL ANALYSIS IF ENABLED ===
        dgtl::core::ArchType arch = backend_to_arch_type(opts.backend);
        dgtl::core::TargetMachine target(arch);
        run_polyhedral_analysis(stream, poly_config, target, opts.verbose);

        EmissionResult emission;
        if (!emit_backend(opts.backend, stream, symbols, emission)) {
            std::cerr << "[DGTL] Backend emission produced no output\n";
            return static_cast<int>(ExitCode::emit_failure);
        }

        std::string emitted_text = std::move(emission.emitted_text);
        std::vector<uint8_t> emitted_bytes = std::move(emission.emitted_bytes);

        if (!opts.output_path.empty()) {
            const bool ok = emission.emitted_binary
                ? write_binary_file(opts.output_path, emitted_bytes)
                : write_text_file(opts.output_path, emitted_text);

            if (!ok) {
                std::cerr << "[DGTL] Failed to write output: " << opts.output_path << "\n";
                return static_cast<int>(ExitCode::io_failure);
            }
        }

        // === JIT EXECUTE: Fire Strike via StrikeOrchestrator ===
        if (opts.jit_execute) {
            if (opts.backend != BackendKind::x64) {
                std::cerr << "[DGTL] --jit-execute requires --backend x64\n";
                return static_cast<int>(ExitCode::invalid_args);
            }

            if (opts.verbose) {
                std::cout << "[DGTL-JIT] Firing strike via StrikeOrchestrator...\n";
            }

            dgtl::core::StrikeOrchestrator orchestrator(context, arena);
            auto strike_metrics = orchestrator.fire_strike(stream);

            const bool strike_ok = (strike_metrics.status_code == dgtl::core::StrikeOrchestrator::STRIKE_OK);
            std::cout << "[DGTL-JIT] Strike "
                      << (strike_ok ? "OK" : "FAILED")
                      << " status=0x" << std::hex << strike_metrics.status_code << std::dec
                      << " executed_instructions=" << strike_metrics.executed_instructions
                      << " code_size=" << orchestrator.get_last_code_size()
                      << "\n";

            if (!strike_ok) {
                std::cerr << "[DGTL-JIT] Strike execution failed\n";
                return static_cast<int>(ExitCode::emit_failure);
            }
        }

        std::cout << "[DGTL] Backend=" << opts.backend_name
                  << (emission.emitted_binary ? " (binary)" : " (text)")
                  << " IRMode=" << opts.ir_mode_name
                  << " PolyMode=" << opts.polyhedral_mode_name
                  << " PolyParallel=" << opts.polyhedral_parallel_name
                  << " PolyTile=" << opts.polyhedral_tile
                  << " Nodes=" << stream.size()
                  << " LogicHash=0x" << std::hex << stream.generate_logic_hash() << std::dec
                  << "\n";

        if (opts.print_output) {
            std::cout << emitted_text;
            if (!emitted_text.empty() && emitted_text.back() != '\n') {
                std::cout << '\n';
            }
        }

        // Retain experimental warm-up block in source, but keep production path deterministic.
    #if 0
        if (opts.verbose) {
            std::cout << "[DGTL] Starting production pipeline (Titan-Stable v3.0)\n";
        }

    #ifdef _WIN32
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
        SetThreadAffinityMask(GetCurrentThread(), 1 << 0);
    #endif

        dgtl::core::DGTL_Arena arena(1, 128);
        auto& local = arena.get_local();
        std::memset(local.raw_data(), 0, 1024 * 1024);
    #endif
        return static_cast<int>(ExitCode::ok);
    } catch (const std::exception& ex) {
        std::cerr << "[DGTL] Fatal exception: " << ex.what() << "\n";
        return static_cast<int>(ExitCode::internal_error);
    } catch (...) {
        std::cerr << "[DGTL] Fatal unknown exception\n";
        return static_cast<int>(ExitCode::internal_error);
    }
}

