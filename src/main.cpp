#include <cstdint>
#include <cctype>
#include <chrono>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "dgtl/all.hpp"

namespace {

enum class ExitCode : int {
    ok = 0,
    invalid_args = 2,
    pipeline_failure = 3,
    emit_failure = 4,
    io_failure = 5,
    internal_error = 6,
};

enum class BackendErrorCode : uint32_t {
    none = 0,
    unsupported_backend = 0xB001,
    emit_empty_output = 0xB002,
    output_write_failed = 0xB003,
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
};

struct AppOptions {
    bool verbose = true;
    bool print_asm = true;
    bool run_evolution = true;
    BackendKind backend = BackendKind::arm64;
    std::string backend_name = "arm64";
    std::string asm_output_path;
};

struct SmokeCheck {
    std::string name;
    bool passed = false;
    std::string detail;
};

bool emit_arm64(const dgtl::ir::IRStream& stream,
                const dgtl::core::SymbolTable& symbols,
                bool verbose_asm,
                std::string& out_text);

bool emit_x64(const dgtl::ir::IRStream& stream,
              std::vector<uint8_t>& out_bytes,
              std::string& out_text);

bool emit_nasm(const dgtl::ir::IRStream& stream,
               const dgtl::core::SymbolTable& symbols,
               std::string& out_text);

bool emit_wasm(const dgtl::ir::IRStream& stream,
               std::vector<uint8_t>& out_bytes,
               std::string& out_text);

void print_usage(const char* argv0) {
    std::cout
        << "DGTL Compiler Smoke Driver\n"
        << "Usage: " << argv0 << " [options]\n\n"
        << "Options:\n"
        << "  --help                 Show this help\n"
        << "  --quiet                Reduce terminal output\n"
        << "  --no-asm-print         Do not print assembly to stdout\n"
        << "  --no-evolution         Skip IREvolver mutate step\n"
        << "  --backend <name>       Select backend: arm64|x64|nasm|wasm\n"
        << "  --emit-asm <path>      Write emitted output to file\n";
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
    return false;
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
        if (arg == "--no-asm-print") {
            opts.print_asm = false;
            continue;
        }
        if (arg == "--no-evolution") {
            opts.run_evolution = false;
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
        if (arg == "--emit-asm") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --emit-asm\n";
                return ParseStatus::error;
            }
            opts.asm_output_path = argv[++i];
            if (opts.asm_output_path.empty()) {
                std::cerr << "Empty path for --emit-asm is not allowed\n";
                return ParseStatus::error;
            }
            continue;
        }

        std::cerr << "Unknown option: " << arg << "\n";
        return ParseStatus::error;
    }
    return ParseStatus::ok;
}

bool build_release_emit_ir(dgtl::ir::IRStream& stream) {
    stream.reset();
    stream.begin_transaction();
    stream.push_node(dgtl::ir::OpCode::LOAD_IMM, dgtl::ir::Flags::IS_CONST, 1, dgtl::ir::INVALID_SLOT, 21, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::LOAD_IMM, dgtl::ir::Flags::IS_CONST, 2, dgtl::ir::INVALID_SLOT, 21, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 3, 1, 2, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 4, 3, 3, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::RET, dgtl::ir::Flags::IS_RETURN, 0, 4, dgtl::ir::INVALID_SLOT, dgtl::ir::INVALID_SLOT, 0);
    stream.commit_transaction();
    return stream.size() != 0;
}

bool build_fusion_ir(dgtl::ir::IRStream& stream) {
    stream.reset();
    stream.begin_transaction();
    stream.push_node(dgtl::ir::OpCode::LOAD_IMM, dgtl::ir::Flags::IS_CONST, 10, dgtl::ir::INVALID_SLOT, 2, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::LOAD_IMM, dgtl::ir::Flags::IS_CONST, 11, dgtl::ir::INVALID_SLOT, 3, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::LOAD_IMM, dgtl::ir::Flags::IS_CONST, 12, dgtl::ir::INVALID_SLOT, 4, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::MUL_F32, dgtl::ir::Flags::NONE, 20, 10, 11, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::ADD_F32, dgtl::ir::Flags::NONE, 21, 20, 12, dgtl::ir::INVALID_SLOT, 0);
    stream.commit_transaction();
    return stream.size() == 5;
}

bool build_simd_ir(dgtl::ir::IRStream& stream) {
    stream.reset();
    stream.begin_transaction();
    for (uint32_t i = 0; i < 8; ++i) {
        stream.push_node(dgtl::ir::OpCode::LOAD_IMM,
                         dgtl::ir::Flags::IS_CONST,
                         i + 1,
                         dgtl::ir::INVALID_SLOT,
                         i + 1,
                         dgtl::ir::INVALID_SLOT,
                         0);
    }
    for (uint32_t i = 0; i < 4; ++i) {
        const uint32_t lhs = (i * 2) + 1;
        const uint32_t rhs = lhs + 1;
        stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 100 + i, lhs, rhs, dgtl::ir::INVALID_SLOT, 0);
    }
    stream.commit_transaction();
    return stream.size() == 12;
}

bool build_pressure_ir(dgtl::ir::IRStream& stream, uint32_t vars) {
    if (vars < 2) return false;
    stream.reset();
    stream.begin_transaction();

    for (uint32_t i = 0; i < vars; ++i) {
        stream.push_node(dgtl::ir::OpCode::LOAD_IMM,
                         dgtl::ir::Flags::IS_CONST,
                         i + 1,
                         dgtl::ir::INVALID_SLOT,
                         i + 1,
                         dgtl::ir::INVALID_SLOT,
                         0);
    }

    uint32_t accum = 1;
    uint32_t next_result = vars + 1;
    for (uint32_t i = 2; i <= vars; ++i) {
        stream.push_node(dgtl::ir::OpCode::ADD_I32,
                         dgtl::ir::Flags::NONE,
                         next_result,
                         accum,
                         i,
                         dgtl::ir::INVALID_SLOT,
                         0);
        accum = next_result;
        ++next_result;
    }

    stream.push_node(dgtl::ir::OpCode::RET, dgtl::ir::Flags::IS_RETURN, 0, accum, dgtl::ir::INVALID_SLOT, dgtl::ir::INVALID_SLOT, 0);
    stream.commit_transaction();
    return stream.size() != 0;
}

void run_core_optimizations(dgtl::ir::IRStream& stream) {
    stream.perform_gvn();
    stream.perform_dead_code_elimination();
}

std::optional<dgtl::ir::IRNode> find_node_by_result(const dgtl::ir::IRStream& stream, uint32_t result_id) {
    for (size_t i = 0; i < stream.size(); ++i) {
        const auto node = stream.get_node(i);
        if (node.result_id == result_id) {
            return node;
        }
    }
    return std::nullopt;
}

void append_check(std::vector<SmokeCheck>& checks, std::string name, bool passed, std::string detail) {
    checks.push_back({std::move(name), passed, std::move(detail)});
}

dgtl::pass::TargetArch target_arch_for_backend(BackendKind backend) {
    switch (backend) {
        case BackendKind::arm64: return dgtl::pass::TargetArch::ARM_NEON;
        case BackendKind::wasm: return dgtl::pass::TargetArch::WASM_SIMD128;
        case BackendKind::x64:
        case BackendKind::nasm:
        default:
            return dgtl::pass::TargetArch::X86_AVX512;
    }
}

uint32_t expected_simd_opcode(BackendKind backend) {
    switch (backend) {
        case BackendKind::arm64: return 0x304;
        case BackendKind::wasm: return 0x404;
        case BackendKind::x64:
        case BackendKind::nasm:
        default:
            return 0x204;
    }
}

SmokeCheck run_folding_check(dgtl::core::DGTL_Arena& arena, dgtl::analysis::DGTLAICostModel& cost_model) {
    dgtl::ir::IRStream stream(arena, 64);
    if (!build_release_emit_ir(stream)) {
        return {"constant-fold", false, "failed to build release IR"};
    }

    const float cost_before = cost_model.predict_cost(stream);
    run_core_optimizations(stream);
    const float cost_after = cost_model.predict_cost(stream);
    const auto folded = find_node_by_result(stream, 4);
    const auto ret = stream.size() > 0 ? stream.get_node(stream.size() - 1) : dgtl::ir::IRNode{};

    const bool structurally_folded =
        folded.has_value() &&
        (folded->opcode == dgtl::ir::OpCode::LOAD_IMM || folded->opcode == dgtl::ir::OpCode::CONST) &&
        ret.opcode == dgtl::ir::OpCode::RET && ret.op_a == 4;

    std::ostringstream oss;
    oss << "nodes=" << stream.size() << " cost=" << cost_before << "->" << cost_after;
    return {"constant-fold", structurally_folded, oss.str()};
}

SmokeCheck run_fusion_check(dgtl::core::DGTL_Arena& arena) {
    dgtl::ir::IRStream stream(arena, 32);
    if (!build_fusion_ir(stream)) {
        return {"fusion", false, "failed to build fusion IR"};
    }

    dgtl::core::DGTLFormalVerifier verifier;
    dgtl::pass::DGTLInstructionFusionSynth fusion(verifier);
    fusion.execute(stream);

    const auto producer = find_node_by_result(stream, 20);
    const auto consumer = find_node_by_result(stream, 21);
    const bool producer_nopped = producer.has_value() && producer->opcode == dgtl::ir::OpCode::NOP;
    const bool consumer_fused = consumer.has_value() &&
        consumer->opcode == dgtl::ir::OpCode::FMA_F32 &&
        (consumer->flags & dgtl::ir::Flags::IS_MACRO_FUSED);

    return {"fusion", producer_nopped && consumer_fused, "mul+add -> fma structural check"};
}

SmokeCheck run_simd_check(dgtl::core::DGTL_Arena& arena, BackendKind backend) {
    dgtl::ir::IRStream stream(arena, 32);
    if (!build_simd_ir(stream)) {
        return {"simd-legalize", false, "failed to build simd IR"};
    }

    dgtl::pass::DGTLSIMDLegalizer legalizer(target_arch_for_backend(backend));
    legalizer.legalize(stream);

    const uint32_t expected = expected_simd_opcode(backend);
    size_t legalized_nodes = 0;
    for (size_t i = 0; i < stream.size(); ++i) {
        const auto node = stream.get_node(i);
        if (static_cast<uint32_t>(node.opcode) == expected &&
            (node.flags & dgtl::ir::Flags::IS_SIMD_BURST) &&
            (node.flags & dgtl::ir::Flags::REQUIRES_VEC_PORT)) {
            ++legalized_nodes;
        }
    }

    std::ostringstream oss;
    oss << "expected=0x" << std::hex << expected << std::dec << " legalized_nodes=" << legalized_nodes;
    return {"simd-legalize", legalized_nodes >= 1, oss.str()};
}

SmokeCheck run_ai_cost_check() {
    dgtl::analysis::DGTLAICostModel model;

    dgtl::ir::IRNode scalar{};
    scalar.opcode = dgtl::ir::OpCode::ADD_I32;
    scalar.result_id = 1;
    scalar.op_a = 2;
    scalar.op_b = 3;
    scalar.refresh_intent_hash();

    dgtl::ir::IRNode simd{};
    simd.opcode = dgtl::ir::OpCode::VEC_ADD_F32_512;
    simd.flags = dgtl::ir::Flags::IS_SIMD_BURST | dgtl::ir::Flags::REQUIRES_VEC_PORT;
    simd.result_id = 4;
    simd.op_a = 5;
    simd.op_b = 6;
    simd.refresh_intent_hash();

    const float scalar_cost = model.predict_node_cost(scalar);
    const float simd_cost = model.predict_node_cost(simd);
    const float independence = model.predict_independence(scalar, simd);

    dgtl::analysis::RuntimeFeedback feedback;
    feedback.actual_latency = simd_cost * 0.9;
    model.update_with_feedback(simd, feedback);
    const float adjusted_cost = model.predict_node_cost(simd);

    const bool sane = std::isfinite(scalar_cost) && std::isfinite(simd_cost) && std::isfinite(adjusted_cost) &&
                      scalar_cost > 0.0f && simd_cost > scalar_cost &&
                      independence >= 0.0f && independence <= 1.0f;

    std::ostringstream oss;
    oss << "scalar=" << scalar_cost << " simd=" << simd_cost << " adjusted=" << adjusted_cost;
    return {"ai-cost", sane, oss.str()};
}

SmokeCheck run_pressure_check(dgtl::core::DGTL_Arena& arena) {
    dgtl::ir::IRStream stream(arena, 256);
    if (!build_pressure_ir(stream, 25)) {
        return {"pressure-zero-spill", false, "failed to build pressure IR"};
    }

    dgtl::analysis::LivenessAnalyzer liveness;
    liveness.analyze_stream(stream);

    dgtl::isa::DGTLARM64RegAlloc regalloc;
    regalloc.execute(stream, liveness);

    const bool integrity_ok = stream.validate_shadow_integrity();
    size_t spill_candidates = 0;
    const auto* atoms = stream.raw_atoms();
    for (size_t i = 0; i < stream.size(); ++i) {
        if (atoms[i].flags & dgtl::ir::Flags::IS_SPILL_CANDIDATE) {
            ++spill_candidates;
        }
    }
    stream.rollback_transaction();

    std::ostringstream oss;
    oss << "spill_candidates=" << spill_candidates;
    return {"pressure-zero-spill", integrity_ok && spill_candidates == 0, oss.str()};
}

SmokeCheck run_determinism_check(dgtl::core::DGTL_Arena& arena, dgtl::core::SymbolTable& symbols) {
    dgtl::ir::IRStream a(arena, 64);
    dgtl::ir::IRStream b(arena, 64);
    build_release_emit_ir(a);
    build_release_emit_ir(b);
    run_core_optimizations(a);
    run_core_optimizations(b);

    std::string asm_a;
    std::string asm_b;
    const bool emit_ok = emit_arm64(a, symbols, true, asm_a) && emit_arm64(b, symbols, true, asm_b);
    const bool deterministic = emit_ok && a.generate_logic_hash() == b.generate_logic_hash() && asm_a == asm_b;

    std::ostringstream oss;
    oss << "hash=0x" << std::hex << a.generate_logic_hash() << std::dec;
    return {"determinism", deterministic, oss.str()};
}

SmokeCheck run_latency_check(dgtl::core::DGTL_Arena& arena,
                             dgtl::core::SymbolTable& symbols,
                             BackendKind backend) {
    constexpr size_t iterations = 64;
    double total_us = 0.0;
    bool ok = true;

    for (size_t i = 0; i < iterations; ++i) {
        dgtl::ir::IRStream stream(arena, 64);
        build_release_emit_ir(stream);

        const auto start = std::chrono::high_resolution_clock::now();
        run_core_optimizations(stream);

        std::string emitted_text;
        std::vector<uint8_t> emitted_bytes;
        bool emitted = false;

        switch (backend) {
            case BackendKind::arm64:
                emitted = emit_arm64(stream, symbols, true, emitted_text);
                break;
            case BackendKind::x64:
                emitted = emit_x64(stream, emitted_bytes, emitted_text);
                break;
            case BackendKind::nasm:
                emitted = emit_nasm(stream, symbols, emitted_text);
                break;
            case BackendKind::wasm:
                emitted = emit_wasm(stream, emitted_bytes, emitted_text);
                break;
        }

        const auto end = std::chrono::high_resolution_clock::now();
        total_us += std::chrono::duration_cast<std::chrono::duration<double, std::micro>>(end - start).count();
        if (!emitted) {
            ok = false;
            break;
        }
    }

    const double avg_us = total_us / static_cast<double>(iterations);
    std::ostringstream oss;
    oss << "avg_us=" << avg_us << " iterations=" << iterations;
    return {"latency", ok && std::isfinite(avg_us) && avg_us >= 0.0, oss.str()};
}

bool run_release_smoke_suite(const AppOptions& opts,
                            dgtl::core::DGTL_Arena& arena,
                            dgtl::core::SymbolTable& symbols) {
    dgtl::analysis::DGTLAICostModel cost_model;
    std::vector<SmokeCheck> checks;

    const auto run_check = [&](const char* label, auto&& fn) {
        if (opts.verbose) {
            std::cout << "[DGTL] running " << label << "\n";
        }
        checks.push_back(fn());
    };

    run_check("constant-fold", [&] { return run_folding_check(arena, cost_model); });
    run_check("fusion", [&] { return run_fusion_check(arena); });
    run_check("simd-legalize", [&] { return run_simd_check(arena, opts.backend); });
    run_check("ai-cost", [&] { return run_ai_cost_check(); });
    run_check("pressure-zero-spill", [&] { return run_pressure_check(arena); });
    run_check("determinism", [&] { return run_determinism_check(arena, symbols); });
    run_check("latency", [&] { return run_latency_check(arena, symbols, opts.backend); });

    bool all_pass = true;
    std::cout << "[DGTL] Release smoke checks\n";
    for (const auto& check : checks) {
        std::cout << "  [" << (check.passed ? "PASS" : "FAIL") << "] "
                  << check.name << " :: " << check.detail << "\n";
        all_pass &= check.passed;
    }
    return all_pass;
}

void run_evolution_step(dgtl::ir::IRStream& stream) {
    dgtl::ir::evolution::IREvolver evolver;
    for (size_t i = 0; i < stream.size(); ++i) {
        const auto node = stream.get_node(i);
        if (!node.is_terminator() && node.opcode != dgtl::ir::OpCode::NOP) {
            auto& node_ref = stream.get_node_ref(i);
            evolver.mutate(&node_ref);
            return;
        }
    }
}

bool emit_arm64(const dgtl::ir::IRStream& stream,
                const dgtl::core::SymbolTable& symbols,
                bool verbose_asm,
                std::string& out_text) {
    dgtl::isa::ARM64Emitter emitter(stream, symbols);
    dgtl::isa::ARM64Emitter::Config cfg;
    cfg.verbose_asm = verbose_asm;

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

bool write_text_file(const std::string& path, const std::string& content) {
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
        if (ec) return false;
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
        if (ec) return false;
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
            std::cout << "[DGTL] Starting production smoke pipeline\n";
        }

        dgtl::core::DGTL_Arena arena(1, 64);
        dgtl::core::SymbolTable symbols(arena);

        if (!run_release_smoke_suite(opts, arena, symbols)) {
            std::cerr << "[DGTL] Release smoke suite failed\n";
            return static_cast<int>(ExitCode::pipeline_failure);
        }

        dgtl::ir::IRStream stream(arena, 256);
        if (!build_release_emit_ir(stream)) {
            std::cerr << "[DGTL] Failed to build backend sample IR\n";
            return static_cast<int>(ExitCode::pipeline_failure);
        }

        run_core_optimizations(stream);
        if (stream.size() == 0 || !stream.validate_shadow_integrity()) {
            std::cerr << "[DGTL] IR validation failed after optimization\n";
            return static_cast<int>(ExitCode::pipeline_failure);
        }

        if (opts.run_evolution) {
            run_evolution_step(stream);
        }

        std::string emitted_text;
        std::vector<uint8_t> emitted_bytes;
        bool emitted_binary = false;

        switch (opts.backend) {
            case BackendKind::arm64:
                if (!emit_arm64(stream, symbols, opts.verbose, emitted_text)) {
                    std::cerr << "[DGTL] ARM64 emission produced no output (code=0x"
                              << std::hex << static_cast<uint32_t>(BackendErrorCode::emit_empty_output)
                              << std::dec << ")\n";
                    return static_cast<int>(ExitCode::emit_failure);
                }
                break;
            case BackendKind::x64:
                emitted_binary = true;
                if (!emit_x64(stream, emitted_bytes, emitted_text)) {
                    std::cerr << "[DGTL] x64 emission produced no output (code=0x"
                              << std::hex << static_cast<uint32_t>(BackendErrorCode::emit_empty_output)
                              << std::dec << ")\n";
                    return static_cast<int>(ExitCode::emit_failure);
                }
                break;
            case BackendKind::nasm:
                if (!emit_nasm(stream, symbols, emitted_text)) {
                    std::cerr << "[DGTL] NASM emission produced no output (code=0x"
                              << std::hex << static_cast<uint32_t>(BackendErrorCode::emit_empty_output)
                              << std::dec << ")\n";
                    return static_cast<int>(ExitCode::emit_failure);
                }
                break;
            case BackendKind::wasm:
                emitted_binary = true;
                if (!emit_wasm(stream, emitted_bytes, emitted_text)) {
                    std::cerr << "[DGTL] WASM emission produced no output (code=0x"
                              << std::hex << static_cast<uint32_t>(BackendErrorCode::emit_empty_output)
                              << std::dec << ")\n";
                    return static_cast<int>(ExitCode::emit_failure);
                }
                break;
        }

        if (!opts.asm_output_path.empty()) {
            const bool ok = emitted_binary
                ? write_binary_file(opts.asm_output_path, emitted_bytes)
                : write_text_file(opts.asm_output_path, emitted_text);

            if (!ok) {
                std::cerr << "[DGTL] Failed to write output: " << opts.asm_output_path
                          << " (code=0x" << std::hex
                          << static_cast<uint32_t>(BackendErrorCode::output_write_failed)
                          << std::dec << ")\n";
                return static_cast<int>(ExitCode::io_failure);
            }
        }

        if (opts.verbose) {
            std::cout << "[DGTL] Backend=" << opts.backend_name
                      << (emitted_binary ? " (binary)" : " (text)") << "\n";
        }

        std::cout << "[DGTL] Nodes=" << stream.size()
                  << " LogicHash=0x" << std::hex << stream.generate_logic_hash() << std::dec << "\n";

        if (opts.print_asm) {
            std::cout << emitted_text;
            if (!emitted_text.empty() && emitted_text.back() != '\n') {
                std::cout << '\n';
            }
        }

        if (opts.verbose) {
            std::cout << "[DGTL] Pipeline completed successfully\n";
        }

        return static_cast<int>(ExitCode::ok);
    } catch (const std::exception& ex) {
        std::cerr << "[DGTL] Fatal exception: " << ex.what() << "\n";
        return static_cast<int>(ExitCode::internal_error);
    } catch (...) {
        std::cerr << "[DGTL] Fatal unknown exception\n";
        return static_cast<int>(ExitCode::internal_error);
    }
}

