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
#include <cstdlib>
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

enum class BenchmarkProfile : uint8_t {
    fold,
    fusion,
    simd,
    pressure,
    predication,
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
    bool print_asm = true;
    bool run_evolution = false;
    bool benchmark_mode = false;
    BackendKind backend = BackendKind::arm64;
    std::string backend_name = "arm64";
    std::string asm_output_path;
    size_t benchmark_iterations = 256;
    BenchmarkProfile benchmark_profile = BenchmarkProfile::fold;
    uint32_t pressure_vars = 25;
    bool benchmark_dump = false;
    PolyhedralMode polyhedral_mode = PolyhedralMode::off;
    std::string polyhedral_mode_name = "off";
    uint32_t polyhedral_tile = 0;
    PolyhedralParallelPolicy polyhedral_parallel = PolyhedralParallelPolicy::auto_policy;
    std::string polyhedral_parallel_name = "auto";
};

struct SmokeCheck {
    std::string name;
    bool passed = false;
    std::string detail;
};

struct EmissionResult {
    bool ok = false;
    bool emitted_binary = false;
    size_t fallback_ops = 0;
    std::string emitted_text;
    std::vector<uint8_t> emitted_bytes;
};

struct TimedEmissionResult {
    bool ok = false;
    double optimize_us = 0.0;
    double emit_us = 0.0;
    double total_us = 0.0;
    double ai_predicted_cost_pre = 0.0;
    double ai_predicted_cost_post = 0.0;
    bool hot_swap_applied = false;
    std::string hot_swap_mode;
    size_t output_size = 0;
    size_t fallback_ops = 0;
    size_t nodes_built = 0;
    size_t nodes_after_profile = 0;
    size_t nodes_after_core_opt = 0;
    EmissionResult emission;
};

struct BackendBenchmark {
    bool ok = false;
    bool runtime_supported = false;
    double optimize_us = 0.0;
    double emit_us = 0.0;
    double total_us = 0.0;
    double runtime_us = 0.0;
    size_t output_size = 0;
    size_t fallback_ops = 0;
    size_t nodes_built = 0;
    size_t nodes_after_profile = 0;
    size_t nodes_after_core_opt = 0;
    size_t hot_swap_count = 0;
    std::string hot_swap_last_mode;
    std::string detail;
    std::string dump_text;
    bool dump_is_binary = false;
};

bool emit_arm64(const dgtl::ir::IRStream& stream,
                const dgtl::core::SymbolTable& symbols,
                bool verbose_asm,
                std::string& out_text);

bool emit_x64(const dgtl::ir::IRStream& stream,
              std::vector<uint8_t>& out_bytes,
              std::string& out_text,
              size_t* out_fallback_ops = nullptr);

bool emit_nasm(const dgtl::ir::IRStream& stream,
               const dgtl::core::SymbolTable& symbols,
               std::string& out_text);

bool emit_wasm(const dgtl::ir::IRStream& stream,
               std::vector<uint8_t>& out_bytes,
               std::string& out_text);

bool emit_backend(BackendKind backend,
                  const dgtl::ir::IRStream& stream,
                  const dgtl::core::SymbolTable& symbols,
                  bool verbose_asm,
                  EmissionResult& result);

TimedEmissionResult time_backend_pipeline(dgtl::core::DGTL_Arena& arena,
                                         dgtl::core::SymbolTable& symbols,
                                         BackendKind backend,
                                         bool verbose_asm,
                                         BenchmarkProfile profile = BenchmarkProfile::fold,
                                         uint32_t pressure_vars = 25);

BackendBenchmark run_backend_benchmark(dgtl::core::DGTL_Arena& arena,
                                       dgtl::core::SymbolTable& symbols,
                                       BackendKind backend,
                                       size_t iterations,
                                       bool verbose_asm,
                                       BenchmarkProfile profile = BenchmarkProfile::fold,
                                       uint32_t pressure_vars = 25);

void print_usage(const char* argv0) {
    std::cout
        << "DGTL Compiler Smoke Driver\n"
        << "Usage: " << argv0 << " [options]\n\n"
        << "Options:\n"
        << "  --help                 Show this help\n"
        << "  --quiet                Reduce terminal output\n"
        << "  --no-asm-print         Do not print assembly to stdout\n"
        << "  --evolution            Enable IR evolution mutate step\n"
        << "  --no-evolution         Skip IREvolver mutate step\n"
        << "  --benchmark            Print benchmark breakdown for selected backend\n"
        << "  --benchmark-iterations <n>  Benchmark iteration count\n"
        << "  --benchmark-profile <p> IR workload profile: fold|fusion|simd|pressure|predication\n"
        << "  --pressure-vars <n>    Variable count for pressure profile (default: 25)\n"
        << "  --benchmark-dump       Print benchmark-emitted artifact (hex or text)\n"
        << "  --backend <name>       Select backend: arm64|x64|nasm|wasm\n"
        << "  --polyhedral-mode <mode>  Polyhedral pipeline mode: off|safe|aggressive (default: off)\n"
        << "  --polyhedral-tile <n>     Polyhedral tile size (0 means auto)\n"
        << "  --polyhedral-parallel <p> Polyhedral parallel policy: auto|off|on (default: auto)\n"
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
        if (arg == "--evolution") {
            opts.run_evolution = true;
            continue;
        }
        if (arg == "--no-evolution") {
            opts.run_evolution = false;
            continue;
        }
        if (arg == "--benchmark") {
            opts.benchmark_mode = true;
            continue;
        }
        if (arg == "--benchmark-dump") {
            opts.benchmark_dump = true;
            continue;
        }
        if (arg == "--benchmark-iterations") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --benchmark-iterations\n";
                return ParseStatus::error;
            }
            try {
                const unsigned long long parsed = std::stoull(argv[++i]);
                if (parsed == 0) {
                    std::cerr << "--benchmark-iterations must be > 0\n";
                    return ParseStatus::error;
                }
                opts.benchmark_iterations = static_cast<size_t>(parsed);
            } catch (const std::exception&) {
                std::cerr << "Invalid value for --benchmark-iterations: " << argv[i] << "\n";
                return ParseStatus::error;
            }
            continue;
        }
        if (arg == "--benchmark-profile") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --benchmark-profile\n";
                return ParseStatus::error;
            }
            const std::string_view p(argv[++i]);
            if (p == "fold") {
                opts.benchmark_profile = BenchmarkProfile::fold;
            } else if (p == "fusion") {
                opts.benchmark_profile = BenchmarkProfile::fusion;
            } else if (p == "simd") {
                opts.benchmark_profile = BenchmarkProfile::simd;
            } else if (p == "pressure") {
                opts.benchmark_profile = BenchmarkProfile::pressure;
            } else if (p == "predication") {
                opts.benchmark_profile = BenchmarkProfile::predication;
            } else {
                std::cerr << "Unknown benchmark profile: " << p << " (expected fold|fusion|simd|pressure|predication)\n";
                return ParseStatus::error;
            }
            continue;
        }
        if (arg == "--pressure-vars") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --pressure-vars\n";
                return ParseStatus::error;
            }
            try {
                const unsigned long long parsed = std::stoull(argv[++i]);
                if (parsed < 2) {
                    std::cerr << "--pressure-vars must be >= 2\n";
                    return ParseStatus::error;
                }
                opts.pressure_vars = static_cast<uint32_t>(parsed);
            } catch (const std::exception&) {
                std::cerr << "Invalid value for --pressure-vars: " << argv[i] << "\n";
                return ParseStatus::error;
            }
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

bool build_backend_sample_ir(dgtl::ir::IRStream& stream) {
    dgtl::pipeline::probes::build_release_int_probe(stream);
    return stream.size() == 5;
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

bool build_predication_ir(dgtl::ir::IRStream& stream) {
    dgtl::pipeline::probes::build_masked_predication_probe(stream);
    return stream.size() == 7;
}

// Bench-specific variants: same IR as the smoke builders but include a RET so
// the full chain stays live through DCE and emission does not fail.
bool build_fusion_bench_ir(dgtl::ir::IRStream& stream) {
    stream.reset();
    stream.begin_transaction();
    stream.push_node(dgtl::ir::OpCode::LOAD_IMM, dgtl::ir::Flags::IS_CONST, 10, dgtl::ir::INVALID_SLOT, 2, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::LOAD_IMM, dgtl::ir::Flags::IS_CONST, 11, dgtl::ir::INVALID_SLOT, 3, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::LOAD_IMM, dgtl::ir::Flags::IS_CONST, 12, dgtl::ir::INVALID_SLOT, 4, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::MUL_F32,  dgtl::ir::Flags::NONE, 20, 10, 11, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::ADD_F32,  dgtl::ir::Flags::NONE, 21, 20, 12, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::RET, dgtl::ir::Flags::IS_RETURN, 0, 21, dgtl::ir::INVALID_SLOT, dgtl::ir::INVALID_SLOT, 0);
    stream.commit_transaction();
    return stream.size() == 6;
}

bool build_simd_bench_ir(dgtl::ir::IRStream& stream) {
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
    // RET references the last accumulated result (id=103) to keep the chain live.
    stream.push_node(dgtl::ir::OpCode::RET, dgtl::ir::Flags::IS_RETURN, 0, 103, dgtl::ir::INVALID_SLOT, dgtl::ir::INVALID_SLOT, 0);
    stream.commit_transaction();
    return stream.size() == 13;
}

std::string_view profile_name(BenchmarkProfile profile) {
    switch (profile) {
        case BenchmarkProfile::fold:     return "fold";
        case BenchmarkProfile::fusion:   return "fusion";
        case BenchmarkProfile::simd:     return "simd";
        case BenchmarkProfile::pressure: return "pressure";
        case BenchmarkProfile::predication: return "predication";
    }
    return "unknown";
}

bool build_ir_for_profile(dgtl::ir::IRStream& stream, BenchmarkProfile profile, uint32_t vars) {
    switch (profile) {
        case BenchmarkProfile::fold:     return build_release_emit_ir(stream);
        case BenchmarkProfile::fusion:   return build_fusion_bench_ir(stream);
        case BenchmarkProfile::simd:     return build_simd_bench_ir(stream);
        case BenchmarkProfile::pressure: return build_pressure_ir(stream, vars);
        case BenchmarkProfile::predication: return build_predication_ir(stream);
    }
    return false;
}

void run_core_optimizations(dgtl::ir::IRStream& stream) {
    stream.perform_gvn();
    dgtl::pass::DGTLTemporalVectorFusion temporal_fusion;
    temporal_fusion.execute(stream);
    dgtl::pass::DGTLMaskedPredicationLowering masked_predication;
    masked_predication.execute(stream);
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

[[maybe_unused]] void append_check(std::vector<SmokeCheck>& checks, std::string name, bool passed, std::string detail) {
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
    if (liveness.has_corruption()) {
        return {"pressure-zero-spill", false, liveness.last_error()};
    }

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

SmokeCheck run_determinism_check(dgtl::core::DGTL_Arena& arena,
                                 dgtl::core::SymbolTable& symbols,
                                 BackendKind backend) {
    dgtl::ir::IRStream a(arena, 64);
    dgtl::ir::IRStream b(arena, 64);
    build_backend_sample_ir(a);
    build_backend_sample_ir(b);
    run_core_optimizations(a);
    run_core_optimizations(b);

    std::string asm_a;
    std::string asm_b;
    std::vector<uint8_t> bytes_a;
    std::vector<uint8_t> bytes_b;
    bool emit_ok = false;

    switch (backend) {
        case BackendKind::arm64:
            emit_ok = emit_arm64(a, symbols, true, asm_a) && emit_arm64(b, symbols, true, asm_b);
            break;
        case BackendKind::x64:
            emit_ok = emit_x64(a, bytes_a, asm_a) && emit_x64(b, bytes_b, asm_b);
            break;
        case BackendKind::nasm:
            emit_ok = emit_nasm(a, symbols, asm_a) && emit_nasm(b, symbols, asm_b);
            break;
        case BackendKind::wasm:
            emit_ok = emit_wasm(a, bytes_a, asm_a) && emit_wasm(b, bytes_b, asm_b);
            break;
    }

    const bool deterministic = emit_ok && a.generate_logic_hash() == b.generate_logic_hash() && asm_a == asm_b;

    std::ostringstream oss;
    oss << "hash=0x" << std::hex << a.generate_logic_hash() << std::dec;
    return {"determinism", deterministic, oss.str()};
}

SmokeCheck run_latency_check(dgtl::core::DGTL_Arena& arena,
                             dgtl::core::SymbolTable& symbols,
                             BackendKind backend) {
    constexpr size_t iterations = 64;
    double total_opt_us = 0.0;
    double total_emit_us = 0.0;
    double total_us = 0.0;
    size_t last_output_size = 0;
    bool ok = true;

    for (size_t i = 0; i < iterations; ++i) {
        const TimedEmissionResult timed = time_backend_pipeline(arena, symbols, backend, true, BenchmarkProfile::fusion, 25);
        total_opt_us += timed.optimize_us;
        total_emit_us += timed.emit_us;
        total_us += timed.total_us;
        last_output_size = timed.output_size;
        if (!timed.ok) {
            ok = false;
            break;
        }
    }

    const double avg_opt_us = total_opt_us / static_cast<double>(iterations);
    const double avg_emit_us = total_emit_us / static_cast<double>(iterations);
    const double avg_us = total_us / static_cast<double>(iterations);
    std::ostringstream oss;
    oss << "opt_us=" << avg_opt_us
        << " emit_us=" << avg_emit_us
        << " total_us=" << avg_us
        << " output_size=" << last_output_size
        << " iterations=" << iterations;
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
    run_check("determinism", [&] { return run_determinism_check(arena, symbols, opts.backend); });
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
            stream.set_node(i, node_ref);
            return;
        }
    }
}

bool emit_backend(BackendKind backend,
                  const dgtl::ir::IRStream& stream,
                  const dgtl::core::SymbolTable& symbols,
                  bool verbose_asm,
                  EmissionResult& result) {
    result = EmissionResult{};

    switch (backend) {
        case BackendKind::arm64:
            result.ok = emit_arm64(stream, symbols, verbose_asm, result.emitted_text);
            result.emitted_binary = false;
            return result.ok;
        case BackendKind::x64:
            result.ok = emit_x64(stream,
                                 result.emitted_bytes,
                                 result.emitted_text,
                                 &result.fallback_ops);
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
    }

    return false;
}

TimedEmissionResult time_backend_pipeline(dgtl::core::DGTL_Arena& arena,
                                         dgtl::core::SymbolTable& symbols,
                                         BackendKind backend,
                                         bool verbose_asm,
                                         BenchmarkProfile profile,
                                         uint32_t pressure_vars) {
    TimedEmissionResult timed{};
    const size_t capacity = (profile == BenchmarkProfile::pressure)
        ? static_cast<size_t>(pressure_vars) * 3 + 16
        : 128;
    dgtl::ir::IRStream stream(arena, capacity);
    if (!build_ir_for_profile(stream, profile, pressure_vars)) {
        return timed;
    }
    timed.hot_swap_mode = profile_name(profile);
    timed.nodes_built = stream.size();

    dgtl::analysis::DGTLAICostModel ai_model;
    timed.ai_predicted_cost_pre = ai_model.predict_cost(stream);

    const auto optimize_start = std::chrono::high_resolution_clock::now();

    // Run the profile-specific optimization pass first (timed inside opt_us),
    // then follow with the standard GVN + DCE cleanup.
    if (profile == BenchmarkProfile::fusion) {
        dgtl::core::DGTLFormalVerifier verifier;
        dgtl::pass::DGTLInstructionFusionSynth fusion_pass(verifier);
        fusion_pass.execute(stream);
    } else if (profile == BenchmarkProfile::simd) {
        dgtl::pass::DGTLSIMDLegalizer legalizer(target_arch_for_backend(backend));
        legalizer.legalize(stream);
    } else if (profile == BenchmarkProfile::predication) {
        dgtl::pass::DGTLMaskedPredicationLowering predication_pass;
        predication_pass.execute(stream);
    }

    // JIT-style hot-path hot-swap driven by AI cost telemetry.
    if (backend == BackendKind::arm64 && timed.ai_predicted_cost_pre > 35.0f) {
        dgtl::pass::DGTLTemporalVectorFusion temporal_fusion;
        temporal_fusion.execute(stream);
        dgtl::pass::DGTLMaskedPredicationLowering predication_pass;
        predication_pass.execute(stream);
        if (timed.ai_predicted_cost_pre > 65.0f) {
            dgtl::pass::DGTLSIMDLegalizer legalizer(target_arch_for_backend(backend));
            legalizer.legalize(stream);
            timed.hot_swap_mode = "int->float-simd/predication";
        } else {
            timed.hot_swap_mode = "int->float-fusion";
        }
        timed.hot_swap_applied = true;
    }

    timed.nodes_after_profile = stream.size();
    run_core_optimizations(stream);
    timed.nodes_after_core_opt = stream.size();
    timed.ai_predicted_cost_post = ai_model.predict_cost(stream);

    const auto optimize_end = std::chrono::high_resolution_clock::now();

    timed.ok = emit_backend(backend, stream, symbols, verbose_asm, timed.emission);
    const auto emit_end = std::chrono::high_resolution_clock::now();

    timed.optimize_us = std::chrono::duration_cast<std::chrono::duration<double, std::micro>>(optimize_end - optimize_start).count();
    timed.emit_us = std::chrono::duration_cast<std::chrono::duration<double, std::micro>>(emit_end - optimize_end).count();
    timed.total_us = std::chrono::duration_cast<std::chrono::duration<double, std::micro>>(emit_end - optimize_start).count();
    timed.output_size = timed.emission.emitted_binary ? timed.emission.emitted_bytes.size() : timed.emission.emitted_text.size();
    timed.fallback_ops = timed.emission.fallback_ops;
    return timed;
}

BackendBenchmark run_backend_benchmark(dgtl::core::DGTL_Arena& arena,
                                       dgtl::core::SymbolTable& symbols,
                                       BackendKind backend,
                                       size_t iterations,
                                       bool verbose_asm,
                                       BenchmarkProfile profile,
                                       uint32_t pressure_vars) {
    BackendBenchmark result{};
    if (iterations == 0) {
        result.detail = "iterations must be > 0";
        return result;
    }

    double total_opt_us = 0.0;
    double total_emit_us = 0.0;
    double total_total_us = 0.0;
    size_t output_size = 0;
    EmissionResult last_emission;

    for (size_t i = 0; i < iterations; ++i) {
        const TimedEmissionResult timed = time_backend_pipeline(arena, symbols, backend, verbose_asm, profile, pressure_vars);
        if (!timed.ok) {
            result.detail = "backend emission failed during benchmark";
            return result;
        }
        total_opt_us += timed.optimize_us;
        total_emit_us += timed.emit_us;
        total_total_us += timed.total_us;
        output_size = timed.output_size;
        result.fallback_ops = timed.fallback_ops;
        result.nodes_built = timed.nodes_built;
        result.nodes_after_profile = timed.nodes_after_profile;
        result.nodes_after_core_opt = timed.nodes_after_core_opt;
        if (timed.hot_swap_applied) {
            ++result.hot_swap_count;
            result.hot_swap_last_mode = timed.hot_swap_mode;
        }
        last_emission = timed.emission;
    }

    result.ok = true;
    result.optimize_us = total_opt_us / static_cast<double>(iterations);
    result.emit_us = total_emit_us / static_cast<double>(iterations);
    result.total_us = total_total_us / static_cast<double>(iterations);
    result.output_size = output_size;
    result.dump_text = last_emission.emitted_text;
    result.dump_is_binary = last_emission.emitted_binary;

    // JIT execution is only safe for the fold profile: the emitted code is a clean
    // movabs rax, imm64 + ret that respects the x64 calling convention.
    // Other profiles emit code that references callee-saved registers (R12-R15)
    // without the prologue/epilogue needed to preserve them.
    if (backend == BackendKind::x64 && profile == BenchmarkProfile::fold &&
        !last_emission.emitted_bytes.empty()) {
        dgtl::exec::StrikeRunner runner;
        const size_t injected = runner.inject_code(arena, last_emission.emitted_bytes);
        if (injected != 0) {
#if defined(_WIN32) || defined(_WIN64)
            _putenv_s("DGTL_STRIKE_EXECUTE", "1");
#else
            setenv("DGTL_STRIKE_EXECUTE", "1", 1);
#endif
            result.runtime_supported = true;
            double total_exec_us = 0.0;
            bool exec_ok = true;
            for (size_t i = 0; i < iterations; ++i) {
                const auto exec_start = std::chrono::high_resolution_clock::now();
                const auto metrics = runner.execute(arena, injected, nullptr);
                const auto exec_end = std::chrono::high_resolution_clock::now();
                if (metrics.status_code != static_cast<uint32_t>(dgtl::exec::StrikeStatus::Ok)) {
                    exec_ok = false;
                    break;
                }
                total_exec_us += std::chrono::duration_cast<std::chrono::duration<double, std::micro>>(exec_end - exec_start).count();
            }
#if defined(_WIN32) || defined(_WIN64)
            _putenv_s("DGTL_STRIKE_EXECUTE", "");
#else
            unsetenv("DGTL_STRIKE_EXECUTE");
#endif
            if (!exec_ok) {
                result.ok = false;
                result.detail = "x64 runtime execution failed during benchmark";
                return result;
            }
            result.runtime_us = total_exec_us / static_cast<double>(iterations);
        }
    }

    std::ostringstream oss;
    oss << "profile=" << profile_name(profile)
        << " opt_us=" << result.optimize_us
        << " emit_us=" << result.emit_us
        << " total_us=" << result.total_us
        << " output_size=" << result.output_size
        << " fallback_ops=" << result.fallback_ops
        << " nodes=" << result.nodes_built
        << "->" << result.nodes_after_profile
        << "->" << result.nodes_after_core_opt
        << " hot_swaps=" << result.hot_swap_count;
    if (!result.hot_swap_last_mode.empty()) {
        oss << " hot_swap_mode=" << result.hot_swap_last_mode;
    }
    if (result.runtime_supported) {
        oss << " exec_us=" << result.runtime_us;
    } else {
        oss << " exec_us=n/a";
    }
    oss << " iterations=" << iterations;
    result.detail = oss.str();
    return result;
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
              std::string& out_text,
              size_t* out_fallback_ops) {
    out_bytes.clear();
    out_text.clear();
    if (out_fallback_ops != nullptr) {
        *out_fallback_ops = 0;
    }
    dgtl::isa::X64StrikeEmitter emitter;
    emitter.process_stream(stream, out_bytes);
    if (out_fallback_ops != nullptr) {
        *out_fallback_ops = emitter.fallback_hits();
    }
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
        if (!build_backend_sample_ir(stream)) {
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

        EmissionResult emission;
        if (!emit_backend(opts.backend, stream, symbols, opts.verbose, emission)) {
            std::cerr << "[DGTL] " << opts.backend_name
                      << " emission produced no output (code=0x"
                      << std::hex << static_cast<uint32_t>(BackendErrorCode::emit_empty_output)
                      << std::dec << ")\n";
            return static_cast<int>(ExitCode::emit_failure);
        }

        std::string emitted_text = std::move(emission.emitted_text);
        std::vector<uint8_t> emitted_bytes = std::move(emission.emitted_bytes);
        const bool emitted_binary = emission.emitted_binary;

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

        if (opts.benchmark_mode) {
            const BackendBenchmark benchmark = run_backend_benchmark(arena,
                                                                    symbols,
                                                                    opts.backend,
                                                                    opts.benchmark_iterations,
                                                                    false,
                                                                    opts.benchmark_profile,
                                                                    opts.pressure_vars);
            if (!benchmark.ok) {
                std::cerr << "[DGTL] Benchmark failed: " << benchmark.detail << "\n";
                return static_cast<int>(ExitCode::pipeline_failure);
            }
            std::cout << "[DGTL] Benchmark :: " << benchmark.detail << "\n";
            std::cout << "[DGTL] Polyhedral :: mode=" << opts.polyhedral_mode_name
                      << " parallel=" << opts.polyhedral_parallel_name
                      << " tile=" << opts.polyhedral_tile << "\n";
            if (opts.benchmark_dump) {
                std::cout << "[DGTL] BenchmarkDump "
                          << (benchmark.dump_is_binary ? "(hex)" : "(text)") << "\n";
                std::cout << benchmark.dump_text;
                if (!benchmark.dump_text.empty() && benchmark.dump_text.back() != '\n') {
                    std::cout << '\n';
                }
            }
        }

        std::cout << "[DGTL] Nodes=" << stream.size()
                  << " PolyMode=" << opts.polyhedral_mode_name
                  << " PolyParallel=" << opts.polyhedral_parallel_name
                  << " PolyTile=" << opts.polyhedral_tile
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

