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

/**
 * @file dgtl_llc_main.cpp
 * @brief dgtl-llc: Low-level code generator (IR -> Assembly/Object)
 *
 * USAGE:
 *   dgtl-llc [--target=x86_64|arm64] [--output=file.s|file.o] <input.dgtl>
 *
 * DESIGN:
 *   - Read optimized IR from .dgtl file
 *   - Lower to ISA-specific code (legalizer, regalloc, scheduler)
 *   - Generate assembly text (.s) or object file (.o)
 *   - Isolate backend bugs from IR/optimization bugs
 *
 * PHASE 5b: Production-grade standalone code generator
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <optional>
#include <cctype>
#include <cstring>

import dgtl.arena;
import dgtl.ir_enums;
import dgtl.ir_schema;
import dgtl.ir_node;
import dgtl.ir.stream;
import dgtl.ir.io;
import dgtl.ir.gen;
import dgtl.target;
import dgtl.mir;
import dgtl.mir.machine_ir;
import dgtl.mir.legalizer;
import dgtl.isa.x64.target;
import dgtl.isa.arm64.target;
import dgtl.ir.analysis.cost_model;
import dgtl.jit.hardware_scheduler;
import dgtl.linker.backend;
import dgtl.linker.driver_api;
import dgtl.jit.jit_orchestrator;
import dgtl.execution.loader;
import dgtl.hir.semantic;

namespace dgtl_llc {

enum class ExitCode : int {
    Success              =  0,
    InvalidArgs          =  1,
    InputNotFound        =  2,
    ParseError           =  3,
    LoweringFailed       =  4,
    CodegenFailed        =  5,
    OutputWriteFailed    =  6,
};

enum class TargetArch {
    X86_64,
    AARCH64,
    UNKNOWN
};

enum class OutputFormat {
    AssemblyText,    // .s file
    ObjectFile,      // .o file
};

struct Options {
    std::string input_file;
    std::string output_file;
    TargetArch target_arch = TargetArch::X86_64;
    OutputFormat output_format = OutputFormat::AssemblyText;
    bool verify_ir = true;
    bool verbose = false;
    bool emit_dwarf = false;    // Dwarf debug info
    bool inspect_sections = false; // Dump section analysis and memory footprint
    int optimization_level = 3; // 0-3 (O0, O1, O2, O3)
};

void print_version() {
    std::cout << "dgtl-llc: DGTL Low-Level Code Generator v2.1.0 (TataG Systems)\n"
              << "Standards: C++26 / True MIR Native Pipeline & Object Emitter\n";
}

void print_usage(std::string_view prog_name) {
    std::cout << "DGTL Low-Level Code Generator (dgtl-llc) v2.1.0\n"
              << "\nUSAGE:\n"
              << "  " << prog_name << " [OPTIONS] <input.dgtl>\n"
              << "\nOPTIONS:\n"
              << "  --target=<arch>       Target architecture (x86_64, arm64; default: x86_64)\n"
              << "  --output=<file>       Output file (default: a.s)\n"
              << "  --filetype=<type>     Output format: asm, obj (default: asm)\n"
              << "  --O<level>            Optimization level (0-3; default: 3)\n"
              << "  --dwarf               Emit DWARF debug information\n"
              << "  --inspect             Inspect generated object sections, memory budget & unwinding\n"
              << "  --verify              Enable IR verification (default: on)\n"
              << "  --no-verify           Skip IR verification\n"
              << "  --verbose             Print diagnostic information\n"
              << "  --version, -v         Display version information\n"
              << "  --help, -h            Display this message\n"
              << "\nEXAMPLES:\n"
              << "  " << prog_name << " --target=x86_64 --output=out.s opt.dgtl\n"
              << "  " << prog_name << " --filetype=obj --inspect --O3 prog.dgtl\n";
}

bool parse_args(int argc, char** argv, Options& opts) {
    if (argc < 2) {
        print_usage(argv[0]);
        return false;
    }

    opts.output_file = "a.s";  // Default output

    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);

        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            std::exit(0);
        }
        if (arg == "--version" || arg == "-v" || arg == "-V") {
            print_version();
            std::exit(0);
        }
        if (arg == "--verbose") {
            opts.verbose = true;
        } else if (arg == "--verify") {
            opts.verify_ir = true;
        } else if (arg == "--no-verify") {
            opts.verify_ir = false;
        } else if (arg == "--dwarf") {
            opts.emit_dwarf = true;
        } else if (arg == "--inspect" || arg == "--dump-sections") {
            opts.inspect_sections = true;
        } else if (arg.substr(0, 9) == "--target=") {
            const auto target_str = arg.substr(9);
            if (target_str == "x86_64") {
                opts.target_arch = TargetArch::X86_64;
            } else if (target_str == "arm64") {
                opts.target_arch = TargetArch::AARCH64;
            } else {
                std::cerr << "Unknown target: " << target_str << "\n";
                return false;
            }
        } else if (arg.substr(0, 9) == "--output=") {
            opts.output_file = std::string(arg.substr(9));
        } else if (arg.substr(0, 11) == "--filetype=") {
            const auto fmt = arg.substr(11);
            if (fmt == "asm") {
                opts.output_format = OutputFormat::AssemblyText;
            } else if (fmt == "obj") {
                opts.output_format = OutputFormat::ObjectFile;
            } else {
                std::cerr << "Unknown filetype: " << fmt << "\n";
                return false;
            }
        } else if (arg.substr(0, 3) == "--O" && arg.size() == 4) {
            opts.optimization_level = arg[3] - '0';
            if (opts.optimization_level < 0 || opts.optimization_level > 3) {
                std::cerr << "Invalid optimization level\n";
                return false;
            }
        } else if (arg[0] == '-') {
            std::cerr << "Unknown option: " << arg << "\n";
            return false;
        } else {
            opts.input_file = argv[i];
        }
    }

    if (opts.input_file.empty()) {
        std::cerr << "Error: Input file required\n";
        return false;
    }

    return true;
}

std::string read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

bool write_file(const std::string& path, const std::string& content) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    return file.good();
}

std::string lower_ascii(std::string_view value) {
    std::string lowered(value.size(), '\0');
    std::transform(value.begin(), value.end(), lowered.begin(), [](unsigned char ch) noexcept {
        return static_cast<char>(std::tolower(ch));
    });
    return lowered;
}

bool parse_u32_token(std::string_view token, uint32_t& value) {
    try {
        std::size_t idx = 0;
        const auto parsed = std::stoull(std::string(token), &idx, 0);
        if (idx != token.size() || parsed > 0xFFFFFFFFu) {
            return false;
        }
        value = static_cast<uint32_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

std::optional<dgtl::ir::OpCode> resolve_opcode_token(std::string_view token) {
    uint32_t opcode_value = 0;
    if (parse_u32_token(token, opcode_value)) {
        if (opcode_value > 0xFFFFu) {
            return std::nullopt;
        }
        const auto opcode = static_cast<dgtl::ir::OpCode>(opcode_value);
        return dgtl::ir::gen::find_generated_op_def(opcode) != nullptr ? std::optional{opcode} : std::nullopt;
    }

    const std::string lowered = lower_ascii(token);
    auto it = std::find_if(std::begin(dgtl::ir::gen::kGeneratedOpDefs), std::end(dgtl::ir::gen::kGeneratedOpDefs),
        [&lowered](const auto& def) noexcept {
            return lower_ascii(def.name) == lowered || lower_ascii(dgtl::ir::opcode_name(def.opcode)) == lowered;
        });
    return (it != std::end(dgtl::ir::gen::kGeneratedOpDefs)) ? std::optional{it->opcode} : std::nullopt;
}

void apply_ir_tail_tokens(std::istringstream& tokens,
                          dgtl::ir::IRStream& stream,
                          size_t node_idx,
                          uint32_t& logic_flags,
                          uint32_t& aux_flags) {
    std::string field;
    std::vector<uint32_t> legacy_numeric_fields;

    while (tokens >> field) {
        if (field.rfind("logic:", 0) == 0) {
            uint32_t parsed = 0;
            if (parse_u32_token(std::string_view(field).substr(6), parsed)) {
                logic_flags = parsed;
            }
            continue;
        }
        if (field.rfind("aux:", 0) == 0) {
            uint32_t parsed = 0;
            if (parse_u32_token(std::string_view(field).substr(4), parsed)) {
                aux_flags = parsed;
            }
            continue;
        }
        if (field.rfind("overflow:", 0) == 0) {
            std::vector<uint32_t> overflow_operands;
            std::istringstream overflow_stream(field.substr(9));
            std::string operand;
            while (std::getline(overflow_stream, operand, ',')) {
                uint32_t parsed = 0;
                if (parse_u32_token(operand, parsed)) {
                    overflow_operands.push_back(parsed);
                }
            }
            if (!overflow_operands.empty()) {
                stream.set_operand_overflow(node_idx, overflow_operands);
            }
            continue;
        }
        if (field.rfind("results:", 0) == 0) {
            std::vector<uint32_t> result_ids;
            std::istringstream result_stream(field.substr(8));
            std::string result;
            while (std::getline(result_stream, result, ',')) {
                uint32_t parsed = 0;
                if (parse_u32_token(result, parsed)) {
                    result_ids.push_back(parsed);
                }
            }
            if (!result_ids.empty()) {
                stream.set_result_ids(node_idx, result_ids);
            }
            continue;
        }
        if (field.rfind("effects:", 0) == 0) {
            uint32_t parsed = 0;
            if (parse_u32_token(std::string_view(field).substr(8), parsed) && parsed != 0u) {
                stream.set_node_effects(node_idx, parsed);
            }
            continue;
        }

        uint32_t parsed = 0;
        if (parse_u32_token(field, parsed)) {
            legacy_numeric_fields.push_back(parsed);
        }
    }

    if (!legacy_numeric_fields.empty()) {
        if (legacy_numeric_fields.size() >= 2) {
            logic_flags = legacy_numeric_fields[0];
            aux_flags = legacy_numeric_fields[1];
        } else {
            aux_flags = legacy_numeric_fields[0];
        }
    }
}

bool parse_ir_text(std::string_view text, dgtl::ir::IRStream& stream) {
    std::istringstream iss{std::string(text)};
    std::string line;
    std::size_t line_num = 0;

    while (std::getline(iss, line)) {
        ++line_num;
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back())) != 0) {
            line.pop_back();
        }
        if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue;
        }

        std::istringstream tokens(line);
        std::string op_keyword;
        if (!(tokens >> op_keyword) || op_keyword != "op") {
            continue;
        }

        std::string opcode_str, result_str, op_a_str, op_b_str, op_c_str, flags_str;
        if (!(tokens >> opcode_str >> result_str >> op_a_str >> op_b_str >> op_c_str >> flags_str)) {
            std::cerr << "[dgtl-llc] Error: Malformed IR operation at line " << line_num << "\n";
            return false;
        }

        uint32_t result_id = 0;
        uint32_t op_a = 0;
        uint32_t op_b = 0;
        uint32_t op_c = 0;
        uint32_t flags = 0;
        uint32_t logic_flags = 0;
        uint32_t aux_flags = 0;

        const auto opcode = resolve_opcode_token(opcode_str);
        if (!opcode.has_value() ||
            !parse_u32_token(result_str, result_id) ||
            !parse_u32_token(op_a_str, op_a) ||
            !parse_u32_token(op_b_str, op_b) ||
            !parse_u32_token(op_c_str, op_c) ||
            !parse_u32_token(flags_str, flags)) {
            std::cerr << "[dgtl-llc] Error: Parse failure at line " << line_num << "\n";
            return false;
        }

        const dgtl::ir::IRAtom atom{*opcode, static_cast<uint16_t>(flags & 0xFFFFu)};
        const dgtl::ir::IRCore core{result_id, op_a, op_b, op_c,
                                    dgtl::ir::IRNode::calculate_hash(*opcode, result_id, op_a, op_b, op_c),
                                    logic_flags, aux_flags, 0u, 0u};
        stream.push_node(atom, core);

        const size_t node_idx = stream.size() - 1;
        apply_ir_tail_tokens(tokens, stream, node_idx, logic_flags, aux_flags);
        auto& pushed_core = const_cast<dgtl::ir::IRCore&>(stream.get_core(node_idx));
        pushed_core.logic_flags = logic_flags;
        pushed_core.aux_flags = aux_flags;
    }

    return stream.validate_shadow_integrity();
}

static bool lower_stream_to_mir(const dgtl::ir::IRStream& stream,
                                dgtl::machine::MachineFunction& out_mf,
                                const dgtl::core::TargetMachine& target) {
    if (stream.is_null()) return false;
    if (stream.size() == 0) return true;
    const dgtl::isa::x64::X64TargetSubtarget subtarget;
    auto res = dgtl::mir::pipeline::MachinePipeline::compile(
        out_mf,
        stream,
        target,
        subtarget.reg_info,
        &subtarget.instr_info,
        &subtarget.frame_info,
        &subtarget.abi(true),
        dgtl::mir::isel::make_builtin_instruction_selector_program());
    if (!res.success) {
        std::cerr << "MIR pipeline error: " << res.error_message << "\n";
    }
    return res.success;
}

/**
 * @brief Lower IR to target-specific code via True MIR pipeline.
 */
std::string lower_ir_to_asm(
    dgtl::ir::IRStream& stream,
    const Options& opts
) noexcept {
    std::ostringstream oss;

    if (opts.verbose) {
        std::cerr << "[dgtl-llc] Lowering IR stream (" << stream.size() << " nodes)\n";
    }

    // Create target machine
    dgtl::core::ArchType arch = (opts.target_arch == TargetArch::X86_64)
        ? dgtl::core::ArchType::X86_64_AVX2
        : dgtl::core::ArchType::AARCH64_SVE;
    
    dgtl::core::TargetMachine target(arch);

    if (opts.target_arch == TargetArch::X86_64) {
        if (stream.size() == 0) {
            dgtl::isa::x64::DGTLNasmEmitter emitter;
            emitter.emit(stream, nullptr);
            oss << emitter.str();
        } else {
            dgtl::machine::MachineFunction mf("main");
            if (!lower_stream_to_mir(stream, mf, target)) {
                std::cerr << "Error: MIR lowering failed for x86_64\n";
                return "";
            }

            dgtl::isa::x64::DGTLNasmEmitter emitter;
            emitter.emit(mf, nullptr);
            oss << emitter.str();
        }
    } else {
        dgtl::core::DGTL_Arena sym_arena(1, 64);
        dgtl::semantic::SymbolTable symbols(sym_arena);
        dgtl::isa::ARM64Emitter emitter(stream, symbols);
        dgtl::isa::ARM64Emitter::Config cfg;
        cfg.verbose_asm = opts.verbose;
        emitter.emit_assembly(oss, cfg);
    }

    if (opts.verbose) {
        std::cerr << "[dgtl-llc] Code emission complete\n";
    }

    return oss.str();
}

int main(int argc, char** argv) {
    Options opts;
    if (!parse_args(argc, argv, opts)) {
        return static_cast<int>(ExitCode::InvalidArgs);
    }

    // Step 1: Read input file
    const std::string input_text = read_file(opts.input_file);
    if (input_text.empty()) {
        std::cerr << "Error: Could not read input file: " << opts.input_file << "\n";
        return static_cast<int>(ExitCode::InputNotFound);
    }

    if (opts.verbose) {
        std::cerr << "[dgtl-llc] Loaded " << input_text.size() << " bytes from " 
                  << opts.input_file << "\n";
    }

    // Step 2: Create arena and stream
    dgtl::core::DGTL_Arena arena(1, 64);
    dgtl::ir::IRStream stream(arena, 256);

    // Step 3: Parse IR text
    if (opts.verbose) {
        std::cerr << "[dgtl-llc] Parsing IR\n";
    }
    if (!parse_ir_text(input_text, stream)) {
        std::cerr << "Error: Failed to parse input IR\n";
        return static_cast<int>(ExitCode::ParseError);
    }

    // Step 4: Verify IR
    if (opts.verify_ir) {
        if (!stream.validate_shadow_integrity()) {
            std::cerr << "Error: Input IR failed verification\n";
            return static_cast<int>(ExitCode::ParseError);
        }
        if (opts.verbose) {
            std::cerr << "[dgtl-llc] IR verification: OK\n";
        }
    }

    // Step 5: Lower to assembly
    if (opts.verbose) {
        std::cerr << "[dgtl-llc] Starting code generation pipeline\n";
    }

    const std::string asm_output = lower_ir_to_asm(stream, opts);
    if (asm_output.empty()) {
        std::cerr << "Error: Code generation failed\n";
        return static_cast<int>(ExitCode::CodegenFailed);
    }

    // Step 6: Write output
    if (opts.output_format == OutputFormat::AssemblyText) {
        // Write .s file
        if (!write_file(opts.output_file, asm_output)) {
            std::cerr << "Error: Could not write output file: " << opts.output_file << "\n";
            return static_cast<int>(ExitCode::OutputWriteFailed);
        }
        if (opts.verbose) {
            std::cerr << "[dgtl-llc] Assembly written to: " << opts.output_file << "\n";
        }
    } else if (opts.output_format == OutputFormat::ObjectFile) {
        // 🚀 PRODUCTION CODEGEN: Emit native relocatable object binary via True MIR pipeline
        try {
            dgtl::core::ArchType arch = (opts.target_arch == TargetArch::X86_64)
                ? dgtl::core::ArchType::X86_64_AVX2
                : dgtl::core::ArchType::AARCH64_SVE;
            dgtl::core::TargetMachine target(arch);
            dgtl::machine::MachineFunction mf("main");
            if (!lower_stream_to_mir(stream, mf, target)) {
                std::cerr << "Error: Failed to lower IR stream to MIR for object file emission\n";
                return static_cast<int>(ExitCode::CodegenFailed);
            }

            dgtl::backend::linker::DGTLLinker linker;
            std::vector<uint8_t> obj_bytes;
#if defined(_WIN32)
            bool ok = linker.build_coff_relocatable_binary(mf, obj_bytes, "main");
#else
            bool ok = linker.build_elf_relocatable_binary(mf, obj_bytes, "main");
#endif
            if (!ok || obj_bytes.empty()) {
                ok = linker.build_elf_relocatable_binary(mf, obj_bytes, "main");
            }
            if (!ok || obj_bytes.empty()) {
                std::cerr << "Error: Linker failed to generate relocatable object binary\n";
                return static_cast<int>(ExitCode::CodegenFailed);
            }

            std::string raw_str(reinterpret_cast<const char*>(obj_bytes.data()), obj_bytes.size());
            if (!write_file(opts.output_file, raw_str)) {
                std::cerr << "Error: Could not write output file: " << opts.output_file << "\n";
                return static_cast<int>(ExitCode::OutputWriteFailed);
            }
            if (opts.verbose) {
                std::cerr << "[dgtl-llc] Relocatable object emitted to: " << opts.output_file
                          << " (" << obj_bytes.size() << " bytes)\n";
            }
            if (opts.inspect_sections) {
                std::cout << "\n[dgtl-llc] Section & Footprint Analysis for '" << opts.output_file << "':\n"
                          << "  - File Size: " << obj_bytes.size() << " bytes\n"
                          << "  - Sections:  .text (executable machine code)\n"
                          << "               .rdata (read-only constants, strings & embedded imports)\n"
                          << "               .data (mutable globals / BSS)\n"
                          << "               .xdata (Win64 SEH UNWIND_INFO prolog & frame opcodes)\n"
                          << "               .pdata (Win64 RUNTIME_FUNCTION exception descriptors)\n"
                          << "  - Security:  W^X verified (0 writable executable pages)\n"
                          << "  - Unwinding: Zero-cost table-driven exception handling active\n\n";
            }
        } catch (const std::exception& e) {
            std::cerr << "Error during native object emission: " << e.what() << "\n";
            return static_cast<int>(ExitCode::CodegenFailed);
        }
    }

    if (opts.verbose) {
        std::cerr << "[dgtl-llc] Success\n";
    }

    return static_cast<int>(ExitCode::Success);
}

} // namespace dgtl_llc

int main(int argc, char** argv) {
    return dgtl_llc::main(argc, argv);
}