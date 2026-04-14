#include <cstdint>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
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

bool build_smoke_ir(dgtl::ir::IRStream& stream) {
    stream.begin_transaction();
    stream.push_node(dgtl::ir::OpCode::LOAD_IMM, dgtl::ir::Flags::IS_CONST, 1, 0, 21, 0, 0);
    stream.push_node(dgtl::ir::OpCode::LOAD_IMM, dgtl::ir::Flags::IS_CONST, 2, 0, 21, 0, 0);
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 3, 1, 2, 0, 0);
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 4, 3, 3, 0, 0);
    stream.push_node(dgtl::ir::OpCode::RET, dgtl::ir::Flags::IS_RETURN, 0, 4, 0, 0, 0);
    stream.commit_transaction();
    return stream.size() != 0;
}

void run_core_optimizations(dgtl::ir::IRStream& stream) {
    stream.perform_gvn();
    stream.perform_dead_code_elimination();
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
        dgtl::ir::IRStream stream(arena, 256);

        if (!build_smoke_ir(stream)) {
            std::cerr << "[DGTL] Failed to build initial IR\n";
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

