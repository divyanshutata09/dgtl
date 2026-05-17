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
    arm32,     // ARM32 NEON gather/scatter
    riscv64,   // RISC-V 64 V-extension gather/scatter
    cuda,      // CUDA PTX (NVIDIA)
    rocm,      // ROCm AMD GCN CDNA
    macho,     // Mach-O ARM64 relocatable binary
    elfobj,    // ELF64 relocatable object
    staticlib, // GNU ar static archive
    coffobj,   // COFF relocatable object
    cofflib,   // COFF import-library archive
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
    // Benchmark-only surrogate path: serializes emitted ARM64 assembly bytes.
    // Keep opt-in until DGTL has a real ARM64 machine-code emitter path.
    bool arm64_binary_stream = false;
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
                const dgtl::semantic::SymbolTable& symbols,
                bool verbose_asm,
                std::string& out_text,
                std::vector<uint8_t>* out_binary_stream = nullptr);

bool emit_x64(const dgtl::ir::IRStream& stream,
              std::vector<uint8_t>& out_bytes,
              std::string& out_text,
              size_t* out_fallback_ops = nullptr);

bool emit_nasm(const dgtl::ir::IRStream& stream,
               const dgtl::semantic::SymbolTable& symbols,
               std::string& out_text);

bool emit_wasm(const dgtl::ir::IRStream& stream,
               std::vector<uint8_t>& out_bytes,
               std::string& out_text);

std::string bytes_to_hex(const std::vector<uint8_t>& bytes);

bool emit_backend(BackendKind backend,
                  const dgtl::ir::IRStream& stream,
                  const dgtl::semantic::SymbolTable& symbols,
                  bool verbose_asm,
                  bool arm64_binary_stream,
                  EmissionResult& result);

TimedEmissionResult time_backend_pipeline(dgtl::core::DGTL_Arena& arena,
                                         dgtl::semantic::SymbolTable& symbols,
                                         BackendKind backend,
                                         bool verbose_asm,
                                         BenchmarkProfile profile = BenchmarkProfile::fold,
                                         uint32_t pressure_vars = 25,
                                         bool arm64_binary_stream = false);

BackendBenchmark run_backend_benchmark(dgtl::core::DGTL_Arena& arena,
                                       dgtl::semantic::SymbolTable& symbols,
                                       BackendKind backend,
                                       size_t iterations,
                                       bool verbose_asm,
                                       BenchmarkProfile profile = BenchmarkProfile::fold,
                                       uint32_t pressure_vars = 25,
                                       bool arm64_binary_stream = false);

bool write_text_file(const std::string& path, const std::string& content);
bool write_binary_file(const std::string& path, const std::vector<uint8_t>& bytes);

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
        << "  --arm64-binary-stream  Benchmark ARM64 as serialized binary stream (opt-in)\n"
        << "  --backend <name>       Select backend: arm64|x64|nasm|wasm|arm32|riscv64|cuda|rocm|macho|elfobj|staticlib|coffobj|cofflib\n"
        << "  --polyhedral-mode <mode>  Polyhedral pipeline mode: off|safe|aggressive (default: off)\n"
        << "  --polyhedral-tile <n>     Polyhedral tile size (0 means auto)\n"
        << "  --polyhedral-parallel <p> Polyhedral parallel policy: auto|off|on (default: auto)\n"
        << "  --emit-asm <path>      Write emitted output to file\n";
}

bool parse_backend(std::string backend_str, BackendKind& out_kind, std::string& out_name) {
    backend_str = dgtl::backend::driver::shared::to_lower_ascii(std::move(backend_str));
    dgtl::backend::driver::BackendDriverTarget driver_target{};
    if (dgtl::backend::driver::parse_backend_driver_target(backend_str, driver_target)) {
        switch (driver_target) {
            case dgtl::backend::driver::BackendDriverTarget::arm64:
                out_kind = BackendKind::arm64;
                break;
            case dgtl::backend::driver::BackendDriverTarget::x64:
                out_kind = BackendKind::x64;
                break;
            case dgtl::backend::driver::BackendDriverTarget::nasm:
                out_kind = BackendKind::nasm;
                break;
            case dgtl::backend::driver::BackendDriverTarget::wasm:
                out_kind = BackendKind::wasm;
                break;
            case dgtl::backend::driver::BackendDriverTarget::arm32:
                out_kind = BackendKind::arm32;
                break;
            case dgtl::backend::driver::BackendDriverTarget::riscv64:
                out_kind = BackendKind::riscv64;
                break;
            case dgtl::backend::driver::BackendDriverTarget::cuda:
                out_kind = BackendKind::cuda;
                break;
            case dgtl::backend::driver::BackendDriverTarget::rocm:
                out_kind = BackendKind::rocm;
                break;
            case dgtl::backend::driver::BackendDriverTarget::macho:
                out_kind = BackendKind::macho;
                break;
            case dgtl::backend::driver::BackendDriverTarget::elfobj:
                out_kind = BackendKind::elfobj;
                break;
            case dgtl::backend::driver::BackendDriverTarget::staticlib:
                out_kind = BackendKind::staticlib;
                break;
            case dgtl::backend::driver::BackendDriverTarget::coffobj:
                out_kind = BackendKind::coffobj;
                break;
            case dgtl::backend::driver::BackendDriverTarget::cofflib:
                out_kind = BackendKind::cofflib;
                break;
            default:
                return false;
        }
        out_name = dgtl::backend::driver::backend_driver_target_name(driver_target);
        return true;
    }
    return false;
}

bool parse_polyhedral_mode(std::string mode_str, PolyhedralMode& out_mode, std::string& out_name) {
    return dgtl::backend::driver::shared::parse_polyhedral_mode(
        std::move(mode_str), out_mode, out_name);
}

bool parse_polyhedral_parallel_policy(std::string policy_str,
                                      PolyhedralParallelPolicy& out_policy,
                                      std::string& out_name) {
    return dgtl::backend::driver::shared::parse_polyhedral_parallel_policy(
        std::move(policy_str), out_policy, out_name);
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
        if (arg == "--arm64-binary-stream") {
            opts.arm64_binary_stream = true;
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