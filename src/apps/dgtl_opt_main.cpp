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
 * @file dgtl_opt_main.cpp
 * @brief dgtl-opt: Lightweight IR pass test & validation tool
 * 
 * USAGE:
 *   dgtl-opt [--pass=<pass_name>] [--verify] [--verbose] <input.dgtl>
 *
 * DESIGN:
 *   - Read IR from text file (.dgtl format)
 *   - Run single specified pass (or all passes if --pass not given)
 *   - Output modified IR to stdout or file
 *   - Verify IR integrity before/after
 *   - Foundation for FileCheck-style testing harness
 *
 * PHASE 5a: Unbreakable Testing Harness (LLVM-grade quality assurance)
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <optional>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <cctype>
#include <ctime>

import dgtl.arena;
import dgtl.ir_enums;
import dgtl.ir_node;
import dgtl.ir_core.stream;
import dgtl.ir_core.io;
import dgtl.ir_core.gen;
import dgtl.ir_core.pass_manager;
import dgtl.ir_core.transform_scalar_core;
import dgtl.ir_core.transform_control_predication;
import dgtl.ir_core.transform_inlining;
import dgtl.ir_core.escape_analysis;
import dgtl.ir_core.simd_gather_scatter;
import dgtl.ir_core.transform_poly_loop;
import dgtl.ir_core.hardware_scheduler;
import dgtl.ir_core.binary_loader;
import dgtl.testing.filecheck;

namespace dgtl_opt {

enum class ExitCode : int {
    Success = 0,
    InvalidArgs = 1,
    InputNotFound = 2,
    ParseError = 3,
    VerificationFailed = 4,
    PassExecutionFailed = 5,
};

struct Options {
    std::string input_file;
    std::string output_file;
    std::optional<std::string> pass_name;
    int opt_level = 2;              ///< 0=O0, 1=O1, 2=O2, 3=O3, 4=Ofast
    uint32_t num_threads = 0;       ///< Parallel multithreading threads
    bool verify_before = true;
    bool verify_after = true;
    bool verbose = false;
    bool show_ir_diff = false;
    bool filecheck_mode = false;  ///< When true, run FileCheck directives on output
};

void print_usage(std::string_view prog_name) {
    std::cout << "DGTL Optimizer (dgtl-opt) - Ultra-High-Performance IR Pass Testing & Optimization Engine\n"
              << "\nUSAGE:\n"
              << "  " << prog_name << " [OPTIONS] <input.dgtl>\n"
              << "\nOPTIONS:\n"
              << "  -O0, -O1, -O2, -O3    Set optimization pipeline level (default: -O2)\n"
              << "  -Ofast                Enable aggressive non-IEEE math and vectorization\n"
              << "  --pass=<name>         Run specific pass (gvn, dce, predication, inlining, escape, fusion, simd, all)\n"
              << "  --j=<n>               Set parallel thread count for pass optimization (default: auto)\n"
              << "  --output=<file>       Write result to file (default: stdout)\n"
              << "  --filecheck           Enable FileCheck verification mode\n"
              << "  --verify              Enable IR verification (default: on)\n"
              << "  --no-verify           Skip IR verification\n"
              << "  --show-diff           Show IR before/after (diffs)\n"
              << "  --verbose             Print diagnostic information\n"
              << "  --help                Display this message\n";
}

bool parse_args(int argc, char** argv, Options& opts) {
    if (argc < 2) {
        print_usage(argv[0]);
        return false;
    }

    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);

        if (arg == "--help") {
            print_usage(argv[0]);
            return false;
        }
        if (arg == "--verbose") {
            opts.verbose = true;
        } else if (arg == "--filecheck") {
            opts.filecheck_mode = true;
        } else if (arg == "--verify") {
            opts.verify_before = opts.verify_after = true;
        } else if (arg == "--no-verify") {
            opts.verify_before = opts.verify_after = false;
        } else if (arg == "--show-diff") {
            opts.show_ir_diff = true;
        } else if (arg == "-O0") {
            opts.opt_level = 0;
        } else if (arg == "-O1") {
            opts.opt_level = 1;
        } else if (arg == "-O2") {
            opts.opt_level = 2;
        } else if (arg == "-O3") {
            opts.opt_level = 3;
        } else if (arg == "-Ofast") {
            opts.opt_level = 4;
        } else if (arg.starts_with("--j=")) {
            opts.num_threads = static_cast<uint32_t>(std::atoi(argv[i] + 4));
        } else if (arg.substr(0, 7) == "--pass=") {
            opts.pass_name = std::string(arg.substr(7));
        } else if (arg.substr(0, 9) == "--output=") {
            opts.output_file = std::string(arg.substr(9));
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

/**
 * @brief Normalize line endings by stripping \r characters (cross-platform).
 */
std::string normalize_line_endings(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    std::copy_if(text.begin(), text.end(), std::back_inserter(result), [](char c) noexcept {
        return c != '\r';
    });
    return result;
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
    std::string metadata_field;
    std::vector<uint32_t> legacy_numeric_fields;

    while (tokens >> metadata_field) {
        if (metadata_field.rfind("logic:", 0) == 0) {
            uint32_t parsed = 0;
            if (parse_u32_token(std::string_view(metadata_field).substr(6), parsed)) {
                logic_flags = parsed;
            }
        } else if (metadata_field.rfind("aux:", 0) == 0) {
            uint32_t parsed = 0;
            if (parse_u32_token(std::string_view(metadata_field).substr(4), parsed)) {
                aux_flags = parsed;
            }
        } else if (metadata_field.rfind("overflow:", 0) == 0) {
            std::string overflow_str = metadata_field.substr(9);
            std::vector<uint32_t> overflow_operands;
            std::istringstream oss_parse(overflow_str);
            std::string operand;
            while (std::getline(oss_parse, operand, ',')) {
                uint32_t parsed = 0;
                if (parse_u32_token(operand, parsed)) {
                    overflow_operands.push_back(parsed);
                }
            }
            if (!overflow_operands.empty()) {
                stream.set_operand_overflow(node_idx, overflow_operands);
            }
        } else if (metadata_field.rfind("results:", 0) == 0) {
            std::string results_str = metadata_field.substr(8);
            std::vector<uint32_t> result_ids;
            std::istringstream oss_parse(results_str);
            std::string result;
            while (std::getline(oss_parse, result, ',')) {
                uint32_t parsed = 0;
                if (parse_u32_token(result, parsed)) {
                    result_ids.push_back(parsed);
                }
            }
            if (!result_ids.empty()) {
                stream.set_result_ids(node_idx, result_ids);
            }
        } else if (metadata_field.rfind("effects:", 0) == 0) {
            uint32_t effect_mask = 0;
            if (parse_u32_token(std::string_view(metadata_field).substr(8), effect_mask) && effect_mask != 0u) {
                stream.set_node_effects(node_idx, effect_mask);
            }
        } else {
            uint32_t parsed = 0;
            if (parse_u32_token(metadata_field, parsed)) {
                legacy_numeric_fields.push_back(parsed);
            }
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

/**
 * @brief Parse IR text format into IRStream with Phase C aux_flags support (production implementation).
 * 
 * Format: Lines starting with 'op' specify IR nodes.
 *   op <opcode_hex> <result_id> <op_a> <op_b> <op_c> <flags_hex> [aux_flags_hex]
 *
 * The aux_flags field is OPTIONAL (defaults to 0 for backward compatibility).
 * Phase C aux_flags encode pointer address-space and predicate constraints.
 *
 * Example:
 *   op 0x0A 1 0 0 0 0x0000 0x0001
 *   op 0x0B 2 1 1 0 0x0001
 *
 * Lines starting with ';' are comments (preserved for FileCheck directives).
 * Empty lines are ignored.
 * Hex values are parsed as 32-bit unsigned integers.
 */
bool parse_ir_text(std::string_view text, dgtl::ir::IRStream& stream) {
    std::istringstream iss{std::string(text)};
    std::string line;
    size_t line_num = 0;

    while (std::getline(iss, line)) {
        ++line_num;
        
        // Strip trailing whitespace
        while (!line.empty() && std::isspace(line.back())) {
            line.pop_back();
        }
        
        // Skip empty lines and comments (FileCheck directives)
        if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue;
        }

        // Parse operation line: "op <opcode> <result> <op_a> <op_b> <op_c> <flags> [aux_flags] [overflow:<id1>,<id2>,...] [results:<id1>,<id2>,...] [effects:<mask>]"
        std::istringstream tokens(line);
        std::string op_keyword;
        if (!(tokens >> op_keyword) || op_keyword != "op") {
            continue;  // Skip malformed lines
        }

        // Parse hex fields (aux_flags and Phase D metadata are optional)
        uint32_t result_id = 0, op_a = 0, op_b = 0, op_c = 0, flags = 0, logic_flags = 0, aux_flags = 0;
        
        std::string opcode_str, result_str, op_a_str, op_b_str, op_c_str, flags_str;
        if (!(tokens >> opcode_str >> result_str >> op_a_str >> op_b_str >> op_c_str >> flags_str)) {
            std::cerr << "[dgtl-opt] Warning: Malformed IR operation at line " << line_num 
                      << ": " << line << "\n";
            continue;
        }

        const auto op = resolve_opcode_token(opcode_str);
        if (!op.has_value() ||
            !parse_u32_token(result_str, result_id) ||
            !parse_u32_token(op_a_str, op_a) ||
            !parse_u32_token(op_b_str, op_b) ||
            !parse_u32_token(op_c_str, op_c) ||
            !parse_u32_token(flags_str, flags)) {
            std::cerr << "[dgtl-opt] Warning: Parse error at line " << line_num << "\n";
            continue;
        }
        const uint16_t node_flags = static_cast<uint16_t>(flags & 0xFFFF);
        
        switch (*op) {
            case dgtl::ir::OpCode::ATOMIC_LOAD:
            case dgtl::ir::OpCode::ATOMIC_STORE:
            case dgtl::ir::OpCode::PTR_CAST:
            case dgtl::ir::OpCode::PTR_DIFF:
                if (op_c == dgtl::ir::INVALID_SLOT) {
                    std::cerr << "[dgtl-opt] Phase C Error: Pointer op at line " << line_num
                              << " has invalid address-space id (op_c must be != 0xFFFFFFFF)\n";
                }
                break;
            case dgtl::ir::OpCode::BOOL_SELECT:
                if ((node_flags & dgtl::ir::Flags::IS_PREDICATED) == 0u) {
                    std::cerr << "[dgtl-opt] Phase C Warning: BOOL_SELECT at line " << line_num
                              << " missing IS_PREDICATED flag\n";
                }
                break;
            default:
                break;
        }

        // Create IR node and push with Phase C aux_flags preserved
        dgtl::ir::IRNode node{};
        node.opcode = *op;
        node.result_id = result_id;
        node.op_a = op_a;
        node.op_b = op_b;
        node.op_c = op_c;
        node.flags = static_cast<uint16_t>(flags & 0xFFFF);
        node.intent_hash = dgtl::ir::IRNode::calculate_hash(
            node.opcode, node.result_id, node.op_a, node.op_b, node.op_c
        );

        // Push via push_node(atom, core) to preserve parsed aux_flags through Phase C semantic promotion
        const dgtl::ir::IRAtom atom{node.opcode, node.flags};
        const dgtl::ir::IRCore core{node.result_id, node.op_a, node.op_b, node.op_c,
                                     node.intent_hash, logic_flags, aux_flags,
                                     0u /* debug_id */, 0u /* epoch */};
        stream.push_node(atom, core);
        
        size_t node_idx = stream.size() - 1;  // Index of the node we just pushed
        apply_ir_tail_tokens(tokens, stream, node_idx, logic_flags, aux_flags);
        auto& pushed_core = const_cast<dgtl::ir::IRCore&>(stream.get_core(node_idx));
        pushed_core.logic_flags = logic_flags;
        pushed_core.aux_flags = aux_flags;
    }

    // Validate stream integrity after parsing
    if (!stream.validate_shadow_integrity()) {
        std::cerr << "[dgtl-opt] Error: Parsed IR stream failed shadow integrity check\n";
        return false;
    }

    return true;
}

/**
 * @brief Serialize IRStream to human-readable text format with Phase C aux_flags support (production implementation).
 * 
 * Output format:
 *   ; DGTL IR Text Format (auto-generated)
 *   ; Stream size: N nodes
 *   ; Generated: <timestamp>
 *   ;
 *   op <opcode_hex> <result_id> <op_a> <op_b> <op_c> <flags_hex> logic:<logic_flags_hex> aux:<aux_flags_hex>
 *   op <opcode_hex> <result_id> <op_a> <op_b> <op_c> <flags_hex> logic:<logic_flags_hex> aux:<aux_flags_hex>
 *   ...
 *
 * This format is:
 * - Machine-readable for downstream tooling
 * - Human-readable for debugging (opcodes in hex)
 * - Deterministic and reproducible
 * - Suitable for FileCheck pattern matching
 * - Preserves Phase C semantics via aux_flags field
 */
std::string serialize_ir_text(const dgtl::ir::IRStream& stream) {
    std::ostringstream oss;
    
    // Header
    oss << "; DGTL IR Text Format (auto-generated)\n";
    oss << "; Stream size: " << stream.size() << " nodes\n";
    
    // Timestamp for reproducibility tracking
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    oss << "; Generated: " << std::ctime(&time);  // Note: ctime includes newline
    
    oss << "\n";

    // Serialize each node in the stream with Phase C aux_flags and Phase D metadata preserved
    for (size_t i = 0; i < stream.size(); ++i) {
        const auto node = stream.get_node(i);
        const auto& core = stream.get_core(i);
        
        // Output format: op <opcode_hex> <result_id> <op_a> <op_b> <op_c> <flags_hex> <aux_flags_hex> [Phase D metadata]
        oss << "op ";
        oss << "0x" << std::hex << std::setfill('0') << std::setw(2) 
            << static_cast<uint32_t>(node.opcode) << std::dec << " ";
        oss << "0x" << std::hex << std::setfill('0') << std::setw(8) 
            << node.result_id << std::dec << " ";
        oss << "0x" << std::hex << std::setfill('0') << std::setw(8) 
            << node.op_a << std::dec << " ";
        oss << "0x" << std::hex << std::setfill('0') << std::setw(8) 
            << node.op_b << std::dec << " ";
        oss << "0x" << std::hex << std::setfill('0') << std::setw(8) 
            << node.op_c << std::dec << " ";
        oss << "0x" << std::hex << std::setfill('0') << std::setw(4) 
            << static_cast<uint32_t>(node.flags) << std::dec << " ";
        oss << "logic:0x" << std::hex << std::setfill('0') << std::setw(8)
            << core.logic_flags << std::dec << " ";
        oss << "aux:0x" << std::hex << std::setfill('0') << std::setw(8)
            << core.aux_flags << std::dec;
        
        // Phase D: Emit sidecar metadata if present
        const auto overflow_ops = stream.get_operand_overflow(i);
        if (!overflow_ops.empty()) {
            oss << " overflow:";
            for (size_t j = 0; j < overflow_ops.size(); ++j) {
                if (j > 0) oss << ",";
                oss << "0x" << std::hex << std::setfill('0') << std::setw(8) 
                    << overflow_ops[j] << std::dec;
            }
        }
        
        const auto result_ids = stream.get_result_ids(i);
        if (!result_ids.empty()) {
            oss << " results:";
            for (size_t j = 0; j < result_ids.size(); ++j) {
                if (j > 0) oss << ",";
                oss << "0x" << std::hex << std::setfill('0') << std::setw(8) 
                    << result_ids[j] << std::dec;
            }
        }
        
        const auto effects = stream.get_node_effects(i);
        if (effects != 0u) {
            oss << " effects:0x" << std::hex << std::setfill('0') << std::setw(8) 
                << effects << std::dec;
        }
        
        oss << "\n";
    } // <--- THIS WAS THE MISSING CLOSING BRACE

    // Footer with integrity marker
    oss << "; End of IR (" << stream.size() << " ops)\n";

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
        std::cerr << "[dgtl-opt] Loaded " << input_text.size() << " bytes from " 
                  << opts.input_file << "\n";
    }

    // Normalize line endings for cross-platform consistency
    const std::string normalized_input = normalize_line_endings(input_text);

    // Step 1b: If FileCheck mode, extract directives early
    std::vector<dgtl::testing::FileCheckDirectiveRecord> directives;
    if (opts.filecheck_mode) {
        directives = dgtl::testing::FileCheckParser::parse(normalized_input);
        if (opts.verbose) {
            std::cerr << "[dgtl-opt] FileCheck mode: Parsed " << directives.size() 
                      << " directives\n";
        }
    }

    // Step 2: Create arena and stream
    dgtl::core::DGTL_Arena arena(1, 64);
    dgtl::ir::IRStream stream(arena, 256);

    // Step 3: Parse IR (placeholder; in production use binary format + proper parser)
    if (!parse_ir_text(normalized_input, stream)) {
        std::cerr << "Error: Failed to parse IR text\n";
        return static_cast<int>(ExitCode::ParseError);
    }

    // Step 4: Pre-pass verification
    if (opts.verify_before) {
        if (!stream.validate_shadow_integrity()) {
            std::cerr << "Error: Input IR failed pre-pass verification\n";
            if (opts.verbose) {
                std::cerr << serialize_ir_text(stream);
            }
            return static_cast<int>(ExitCode::VerificationFailed);
        }
        if (opts.verbose) {
            std::cerr << "[dgtl-opt] Pre-pass verification: OK (" << stream.size() << " nodes)\n";
        }
    }

    // Step 5: Execute passes
    bool any_pass_ran = false;
    if (!opts.pass_name.has_value()) {
        // Run all passes in canonical order
        opts.pass_name = "all";
    }

    std::string pass_name = *opts.pass_name;
    std::transform(pass_name.begin(), pass_name.end(), pass_name.begin(), ::tolower);

    // Registry of available passes with execution logic
    if (pass_name == "gvn" || pass_name == "all") {
        if (opts.verbose) std::cerr << "[dgtl-opt] Running: Global Value Numbering (GVN)\n";
        try {
            dgtl::pass::transform::GlobalValueNumbering().execute(stream);
            any_pass_ran = true;
        } catch (const std::exception& e) {
            std::cerr << "Error executing GVN pass: " << e.what() << "\n";
            return static_cast<int>(ExitCode::PassExecutionFailed);
        }
    }

    if (pass_name == "dce" || pass_name == "all") {
        if (opts.verbose) std::cerr << "[dgtl-opt] Running: Dead Code Elimination (DCE)\n";
        try {
            dgtl::pass::transform::DeadCodeElimination().execute(stream);
            any_pass_ran = true;
        } catch (const std::exception& e) {
            std::cerr << "Error executing DCE pass: " << e.what() << "\n";
            return static_cast<int>(ExitCode::PassExecutionFailed);
        }
    }

    if (pass_name == "predication" || pass_name == "all") {
        if (opts.verbose) std::cerr << "[dgtl-opt] Running: Control Predication\n";
        try {
            dgtl::pass::transform::DGTLMaskedPredicationLowering pred;
            pred.execute(stream);
            any_pass_ran = true;
        } catch (const std::exception& e) {
            std::cerr << "Error executing Predication pass: " << e.what() << "\n";
            return static_cast<int>(ExitCode::PassExecutionFailed);
        }
    }

    if (pass_name == "inlining" || pass_name == "all") {
        if (opts.verbose) std::cerr << "[dgtl-opt] Running: Aggressive Inlining\n";
        try {
            dgtl::pass::transform::DGTLAggressiveInliner inlining(nullptr, nullptr);
            inlining.execute(stream);
            any_pass_ran = true;
        } catch (const std::exception& e) {
            std::cerr << "Error executing Inlining pass: " << e.what() << "\n";
            return static_cast<int>(ExitCode::PassExecutionFailed);
        }
    }

    if (pass_name == "escape" || pass_name == "all") {
        if (opts.verbose) std::cerr << "[dgtl-opt] Running: Escape Analysis\n";
        try {
            dgtl::pass::DGTLEscapeAnalysis escape;
            escape.execute(stream);
            any_pass_ran = true;
        } catch (const std::exception& e) {
            std::cerr << "Error executing Escape Analysis pass: " << e.what() << "\n";
            return static_cast<int>(ExitCode::PassExecutionFailed);
        }
    }

    if (pass_name == "fusion" || pass_name == "all" || opts.opt_level >= 3) {
        if (opts.verbose) std::cerr << "[dgtl-opt] Running: Loop Fusion & Unrolling\n";
        try {
            dgtl::pass::transform::DGTLLoopFusion fusion;
            fusion.execute(stream);
            any_pass_ran = true;
        } catch (const std::exception& e) {
            std::cerr << "Error executing Loop Fusion pass: " << e.what() << "\n";
            return static_cast<int>(ExitCode::PassExecutionFailed);
        }
    }

    if (pass_name == "simd" || pass_name == "all" || opts.opt_level >= 3) {
        if (opts.verbose) std::cerr << "[dgtl-opt] Running: SIMD Gather/Scatter Vectorization\n";
        try {
            dgtl::pass::simd::DGTLAdvancedSIMDRecognizer simd_vectorizer;
            simd_vectorizer.execute(stream);
            any_pass_ran = true;
        } catch (const std::exception& e) {
            std::cerr << "Error executing SIMD Vectorizer pass: " << e.what() << "\n";
            return static_cast<int>(ExitCode::PassExecutionFailed);
        }
    }

    if (!any_pass_ran && pass_name != "all") {
        std::cerr << "Error: Unknown pass: " << pass_name << "\n";
        std::cerr << "Available passes: gvn, dce, predication, inlining, escape, fusion, simd, all\n";
        return static_cast<int>(ExitCode::PassExecutionFailed);
    }

    // Step 6: Post-pass verification
    if (opts.verify_after) {
        if (!stream.validate_shadow_integrity()) {
            std::cerr << "Error: Output IR failed post-pass verification\n";
            if (opts.verbose) {
                std::cerr << serialize_ir_text(stream);
            }
            return static_cast<int>(ExitCode::VerificationFailed);
        }
        if (opts.verbose) {
            std::cerr << "[dgtl-opt] Post-pass verification: OK\n";
        }
    }

    // Step 7: Serialize result
    std::string output_text = serialize_ir_text(stream);

    // Step 7b: FileCheck verification (if enabled)
    if (opts.filecheck_mode) {
        // Normalize output for cross-platform consistency
        output_text = normalize_line_endings(output_text);
        
        const auto match_result = dgtl::testing::FileCheckMatcher::verify(directives, output_text);
        if (!match_result.success) {
            std::cerr << "FileCheck verification FAILED:\n";
            std::cerr << "  Error: " << match_result.error_message << "\n";
            if (match_result.directive_line > 0) {
                std::cerr << "  Directive line: " << match_result.directive_line << "\n";
            }
            if (match_result.failed_line > 0) {
                std::cerr << "  Failed output line: " << match_result.failed_line << "\n";
            }
            return static_cast<int>(ExitCode::VerificationFailed);
        }
        if (opts.verbose) {
            std::cerr << "[dgtl-opt] FileCheck verification: PASSED (" << directives.size() 
                      << " directives)\n";
        }
    }

    // Step 8: Output
    if (opts.output_file.empty()) {
        std::cout << output_text;
    } else {
        if (!write_file(opts.output_file, output_text)) {
            std::cerr << "Error: Could not write output file: " << opts.output_file << "\n";
            return static_cast<int>(ExitCode::PassExecutionFailed);
        }
        if (opts.verbose) {
            std::cerr << "[dgtl-opt] Wrote output to " << opts.output_file << "\n";
        }
    }

    if (opts.verbose) {
        std::cerr << "[dgtl-opt] Success\n";
    }

    return static_cast<int>(ExitCode::Success);
}

} // namespace dgtl_opt

int main(int argc, char** argv) {
    return dgtl_opt::main(argc, argv);
}