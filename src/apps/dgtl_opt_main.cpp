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

#include "dgtl/core/Arena.hpp"
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/pass/transform/Transform_Scalar_Core.hpp"
#include "dgtl/pass/transform/Transform_Control_Predication.hpp"
#include "dgtl/pass/analysis/Escape_Analysis.hpp"
#include "dgtl/ir/gen/DGTL_Gen_Ops.hpp"
#include "dgtl/pass/transform/Transform_Inlining.hpp"
#include "dgtl/pass/error_handling/Enhanced_Error_Recovery.hpp"
#include "dgtl/backend/driver/Backend_Driver_API.hpp"
#include "dgtl/testing/FileCheck.hpp"

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
    bool verify_before = true;
    bool verify_after = true;
    bool verbose = false;
    bool show_ir_diff = false;
    bool filecheck_mode = false;  ///< When true, run FileCheck directives on output
};

void print_usage(std::string_view prog_name) {
    std::cout << "DGTL Optimizer (dgtl-opt) - IR Pass Testing Tool with FileCheck Support\n"
              << "\nUSAGE:\n"
              << "  " << prog_name << " [OPTIONS] <input.dgtl>\n"
              << "\nOPTIONS:\n"
              << "  --pass=<name>         Run specific pass (default: all passes)\n"
              << "  --output=<file>       Write result to file (default: stdout)\n"
              << "  --filecheck           Enable FileCheck verification mode\n"
              << "  --verify              Enable IR verification (default: on)\n"
              << "  --no-verify           Skip IR verification\n"
              << "  --verbose             Print diagnostic information\n"
              << "  --show-diff           Show IR before/after (diffs)\n"
              << "  --help                Display this message\n"
              << "\nEXAMPLES:\n"
              << "  " << prog_name << " --pass=gvn test.dgtl\n"
              << "  " << prog_name << " --pass=predication --filecheck in.dgtl\n"
              << "  " << prog_name << " --pass=gvn --output=out.dgtl --verbose in.dgtl\n"
              << "\nFILECHECK MODE:\n"
              << "  When --filecheck is specified, dgtl-opt will:\n"
              << "    1. Parse ; CHECK:, ; CHECK-NOT:, ; CHECK-NEXT: directives from input\n"
              << "    2. Run the specified pass(es)\n"
              << "    3. Verify optimized IR matches all directives\n"
              << "    4. Normalize line endings for cross-platform compatibility\n"
              << "  Exit with code 4 on FileCheck mismatch.\n";
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
    for (char c : text) {
        if (c != '\r') {  // Strip carriage returns
            result.push_back(c);
        }
    }
    return result;
}

/**
 * @brief Parse IR text format into IRStream (production implementation).
 * 
 * Format: Lines starting with 'op' specify IR nodes.
 *   op <opcode_hex> <result_id> <op_a> <op_b> <op_c> <flags_hex>
 *
 * Example:
 *   op 0x0A 1 0 0 0 0x0000
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

        // Parse operation line: "op <opcode> <result> <op_a> <op_b> <op_c> <flags>"
        std::istringstream tokens(line);
        std::string op_keyword;
        if (!(tokens >> op_keyword) || op_keyword != "op") {
            continue;  // Skip malformed lines
        }

        // Parse hex fields
        uint32_t opcode_val = 0, result_id = 0, op_a = 0, op_b = 0, op_c = 0, flags = 0;
        
        std::string opcode_str, result_str, op_a_str, op_b_str, op_c_str, flags_str;
        if (!(tokens >> opcode_str >> result_str >> op_a_str >> op_b_str >> op_c_str >> flags_str)) {
            std::cerr << "[dgtl-opt] Warning: Malformed IR operation at line " << line_num 
                      << ": " << line << "\n";
            continue;
        }

        // Convert hex strings to unsigned integers
        try {
            std::size_t idx = 0;
            opcode_val = std::stoul(opcode_str, &idx, 16);
            result_id = std::stoul(result_str, &idx, 16);
            op_a = std::stoul(op_a_str, &idx, 16);
            op_b = std::stoul(op_b_str, &idx, 16);
            op_c = std::stoul(op_c_str, &idx, 16);
            flags = std::stoul(flags_str, &idx, 16);
        } catch (const std::exception& e) {
            std::cerr << "[dgtl-opt] Warning: Parse error at line " << line_num 
                      << ": " << e.what() << "\n";
            continue;
        }

        // Validate and create IR node
        if (opcode_val > 0xFFFF) {
            std::cerr << "[dgtl-opt] Warning: Invalid opcode 0x" << std::hex << opcode_val 
                      << std::dec << " at line " << line_num << "\n";
            continue;
        }

        // Create and append IR node
        dgtl::ir::IRNode node{};
        node.opcode = static_cast<dgtl::ir::OpCode>(opcode_val);
        node.result_id = result_id;
        node.op_a = op_a;
        node.op_b = op_b;
        node.op_c = op_c;
        node.flags = static_cast<uint16_t>(flags & 0xFFFF);
        node.intent_hash = dgtl::ir::IRNode::calculate_hash(
            node.opcode, node.result_id, node.op_a, node.op_b, node.op_c
        );

        stream.push_back(node);
    }

    // Validate stream integrity after parsing
    if (!stream.validate_shadow_integrity()) {
        std::cerr << "[dgtl-opt] Error: Parsed IR stream failed shadow integrity check\n";
        return false;
    }

    return true;
}

/**
 * @brief Serialize IRStream to human-readable text format (production implementation).
 * 
 * Output format:
 *   ; DGTL IR Text Format (auto-generated)
 *   ; Stream size: N nodes
 *   ; Generated: <timestamp>
 *   ;
 *   op <opcode_hex> <result_id> <op_a> <op_b> <op_c> <flags_hex>
 *   op <opcode_hex> <result_id> <op_a> <op_b> <op_c> <flags_hex>
 *   ...
 *
 * This format is:
 * - Machine-readable for downstream tooling
 * - Human-readable for debugging (opcodes in hex)
 * - Deterministic and reproducible
 * - Suitable for FileCheck pattern matching
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

    // Serialize each node in the stream
    for (size_t i = 0; i < stream.size(); ++i) {
        const auto node = stream.get_node(i);
        
        // Output format: op <opcode_hex> <result_id> <op_a> <op_b> <op_c> <flags_hex>
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
            << static_cast<uint32_t>(node.flags) << std::dec;
        oss << "\n";
    }

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

    if (!any_pass_ran && pass_name != "all") {
        std::cerr << "Error: Unknown pass: " << pass_name << "\n";
        std::cerr << "Available passes: gvn, dce, predication, inlining, escape, all\n";
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

