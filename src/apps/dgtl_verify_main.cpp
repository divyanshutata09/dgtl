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
 * @file dgtl_verify_main.cpp
 * @brief Formal verification CLI tool for DGTL optimizations
 *
 * USAGE:
 *   dgtl-verify --original=before.dgtl --optimized=after.dgtl --pass=gvn [--solver=z3] [--level=full]
 *
 * DESIGN:
 *   - Load pre-optimized IR pair
 *   - Export to SMT-LIB2 format
 *   - Invoke external SMT solver to prove equivalence
 *   - Generate audit report with proof status
 *   - Suitable for CI/CD pipeline integration
 *
 * PHASE 5: Production Formal Verification Tool
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
#include <iomanip>

import dgtl.arena;
import dgtl.ir_enums;
import dgtl.ir_node;
import dgtl.ir_core.stream;
import dgtl.ir_core.io;
import dgtl.ir_core.gen;
import dgtl.ir_core.formal_verification_smt;

namespace dgtl_verify {

enum class ExitCode : int {
    Success                    = 0,
    InvalidArgs                = 1,
    FileNotFound               = 2,
    ParseError                 = 3,
    VerificationFailed         = 4,
    EquivalenceNotProven       = 5,
    SolverNotFound             = 6,
    UndefinedBehaviorDetected  = 7,
};

enum class VerificationLevel {
    Basic,       // Symbolic equivalence only
    Standard,    // Symbolic + bit patterns
    Full,        // SMT solver verification
    Exhaustive,  // Full + undecidable property checks
};

struct Options {
    std::string original_file;
    std::string optimized_file;
    std::string pass_name = "unknown";
    std::string solver_name = "z3";
    VerificationLevel level = VerificationLevel::Standard;
    std::string output_report;
    bool verbose = false;
    bool generate_smtlib = false;
    int timeout_seconds = 30;
};

void print_usage(std::string_view prog_name) {
    std::cout << "DGTL Formal Verification Tool (dgtl-verify)\n"
              << "\nUSAGE:\n"
              << "  " << prog_name << " --original=<before.dgtl> --optimized=<after.dgtl> [OPTIONS]\n"
              << "\nREQUIRED:\n"
              << "  --original=<file>       Original IR before optimization\n"
              << "  --optimized=<file>      Optimized IR after pass\n"
              << "\nOPTIONAL:\n"
              << "  --pass=<name>           Pass name for reporting (default: unknown)\n"
              << "  --solver=<solver>       SMT solver (z3, cvc4, cvc5, yices2; default: z3)\n"
              << "  --level=<level>         Verification level (basic, standard, full, exhaustive; default: standard)\n"
              << "  --output=<file>         Write report to file (default: stdout)\n"
              << "  --smtlib=<file>         Export SMT-LIB2 script to file\n"
              << "  --timeout=<seconds>     Solver timeout (default: 30)\n"
              << "  --verbose               Print diagnostic information\n"
              << "  --help                  Display this message\n"
              << "\nEXAMPLES:\n"
              << "  " << prog_name << " --original=test_before.dgtl --optimized=test_after.dgtl --pass=gvn\n"
              << "  " << prog_name << " --original=before.dgtl --optimized=after.dgtl --level=full --solver=cvc5\n"
              << "  " << prog_name << " --original=in.dgtl --optimized=out.dgtl --output=report.txt --smtlib=proof.smt2\n";
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
        } else if (arg.substr(0, 11) == "--original=") {
            opts.original_file = std::string(arg.substr(11));
        } else if (arg.substr(0, 12) == "--optimized=") {
            opts.optimized_file = std::string(arg.substr(12));
        } else if (arg.substr(0, 7) == "--pass=") {
            opts.pass_name = std::string(arg.substr(7));
        } else if (arg.substr(0, 9) == "--solver=") {
            opts.solver_name = std::string(arg.substr(9));
        } else if (arg.substr(0, 9) == "--output=") {
            opts.output_report = std::string(arg.substr(9));
        } else if (arg.substr(0, 9) == "--smtlib=") {
            opts.generate_smtlib = true;
            opts.output_report = std::string(arg.substr(9));
        } else if (arg.substr(0, 8) == "--level=") {
            const auto level_str = arg.substr(8);
            if (level_str == "basic") {
                opts.level = VerificationLevel::Basic;
            } else if (level_str == "standard") {
                opts.level = VerificationLevel::Standard;
            } else if (level_str == "full") {
                opts.level = VerificationLevel::Full;
            } else if (level_str == "exhaustive") {
                opts.level = VerificationLevel::Exhaustive;
            } else {
                std::cerr << "Unknown verification level: " << level_str << "\n";
                return false;
            }
        } else if (arg.substr(0, 10) == "--timeout=") {
            try {
                opts.timeout_seconds = std::stoi(std::string(arg.substr(10)));
            } catch (...) {
                std::cerr << "Invalid timeout value\n";
                return false;
            }
        } else if (arg[0] == '-') {
            std::cerr << "Unknown option: " << arg << "\n";
            return false;
        }
    }

    if (opts.original_file.empty() || opts.optimized_file.empty()) {
        std::cerr << "Error: Both --original and --optimized required\n";
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

bool parse_ir_text(std::string_view text, dgtl::ir::IRStream& stream, std::string_view label) {
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
            std::cerr << "[dgtl-verify] Error: Malformed " << label << " IR at line " << line_num << "\n";
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
            std::cerr << "[dgtl-verify] Error: Parse failure in " << label << " IR at line "
                      << line_num << "\n";
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

std::string level_to_string(VerificationLevel level) {
    switch (level) {
        case VerificationLevel::Basic:      return "basic";
        case VerificationLevel::Standard:   return "standard";
        case VerificationLevel::Full:       return "full";
        case VerificationLevel::Exhaustive: return "exhaustive";
        default:                            return "unknown";
    }
}

dgtl::formal::VerificationLevel map_level(VerificationLevel level) {
    switch (level) {
        case VerificationLevel::Basic:      return dgtl::formal::VerificationLevel::Basic;
        case VerificationLevel::Standard:   return dgtl::formal::VerificationLevel::Standard;
        case VerificationLevel::Full:       return dgtl::formal::VerificationLevel::Full;
        case VerificationLevel::Exhaustive: return dgtl::formal::VerificationLevel::Exhaustive;
        default:                            return dgtl::formal::VerificationLevel::Standard;
    }
}

int main(int argc, char** argv) {
    Options opts;
    if (!parse_args(argc, argv, opts)) {
        return static_cast<int>(ExitCode::InvalidArgs);
    }

    if (opts.verbose) {
        std::cerr << "[dgtl-verify] Configuration:\n";
        std::cerr << "  Original:   " << opts.original_file << "\n";
        std::cerr << "  Optimized:  " << opts.optimized_file << "\n";
        std::cerr << "  Pass:       " << opts.pass_name << "\n";
        std::cerr << "  Level:      " << level_to_string(opts.level) << "\n";
        std::cerr << "  Solver:     " << opts.solver_name << "\n";
        std::cerr << "  Timeout:    " << opts.timeout_seconds << " sec\n";
    }

    // Step 1: Read IR files
    std::string orig_text = read_file(opts.original_file);
    if (orig_text.empty()) {
        std::cerr << "Error: Could not read original IR file: " << opts.original_file << "\n";
        return static_cast<int>(ExitCode::FileNotFound);
    }

    std::string opt_text = read_file(opts.optimized_file);
    if (opt_text.empty()) {
        std::cerr << "Error: Could not read optimized IR file: " << opts.optimized_file << "\n";
        return static_cast<int>(ExitCode::FileNotFound);
    }

    if (opts.verbose) {
        std::cerr << "[dgtl-verify] Loaded " << orig_text.size() << " bytes (original) + "
                  << opt_text.size() << " bytes (optimized)\n";
    }

    // Step 2: Create IR streams (minimal parsing for demo)
    dgtl::core::DGTL_Arena arena(1, 64);
    dgtl::ir::IRStream original_ir(arena, 256);
    dgtl::ir::IRStream optimized_ir(arena, 256);

    if (!parse_ir_text(orig_text, original_ir, "original")) {
        return static_cast<int>(ExitCode::ParseError);
    }
    if (!parse_ir_text(opt_text, optimized_ir, "optimized")) {
        return static_cast<int>(ExitCode::ParseError);
    }

    if (opts.verbose) {
        std::cerr << "[dgtl-verify] IR streams created: original=" << original_ir.size()
                  << " optimized=" << optimized_ir.size() << "\n";
    }

    // Step 3: Perform formal verification
    if (opts.verbose) {
        std::cerr << "[dgtl-verify] Starting formal verification...\n";
    }

    dgtl::formal::DGTLFormalVerificationSMT verifier;
    auto result = verifier.verify_pass_equivalence(
        original_ir,
        optimized_ir,
        opts.pass_name,
        map_level(opts.level)
    );

    if (opts.verbose) {
        std::cerr << "[dgtl-verify] Verification complete\n";
    }

    // Step 4: Generate report
    std::string report = verifier.generate_verification_report(result, original_ir, optimized_ir);

    if (opts.generate_smtlib) {
        // Write SMT-LIB2 script
        if (!opts.output_report.empty()) {
            if (write_file(opts.output_report, result.smt_script)) {
                std::cout << "SMT-LIB2 script written to: " << opts.output_report << "\n";
            } else {
                std::cerr << "Error: Could not write SMT-LIB2 file\n";
                return static_cast<int>(ExitCode::EquivalenceNotProven);
            }
        }
    } else {
        // Write report
        if (!opts.output_report.empty()) {
            if (write_file(opts.output_report, report)) {
                std::cout << "Report written to: " << opts.output_report << "\n";
            } else {
                std::cerr << "Error: Could not write report file\n";
                return static_cast<int>(ExitCode::EquivalenceNotProven);
            }
        } else {
            std::cout << report;
        }
    }

    // Step 5: Return appropriate exit code
    switch (result.status) {
        case dgtl::formal::VerificationStatus::Equivalent:
            if (opts.verbose) {
                std::cerr << "[dgtl-verify] PASS: Equivalence proven\n";
            }
            return static_cast<int>(ExitCode::Success);

        case dgtl::formal::VerificationStatus::NotEquivalent:
            if (opts.verbose) {
                std::cerr << "[dgtl-verify] FAIL: Equivalence not proven\n";
            }
            return static_cast<int>(ExitCode::EquivalenceNotProven);

        case dgtl::formal::VerificationStatus::UndefinedBehavior:
            if (opts.verbose) {
                std::cerr << "[dgtl-verify] FAIL: Undefined behavior detected\n";
            }
            return static_cast<int>(ExitCode::UndefinedBehaviorDetected);

        case dgtl::formal::VerificationStatus::Timeout:
            if (opts.verbose) {
                std::cerr << "[dgtl-verify] TIMEOUT: Solver exceeded time limit\n";
            }
            return static_cast<int>(ExitCode::VerificationFailed);

        default:
            if (opts.verbose) {
                std::cerr << "[dgtl-verify] ERROR: " << result.error_message << "\n";
            }
            return static_cast<int>(ExitCode::VerificationFailed);
    }
}

} // namespace dgtl_verify

int main(int argc, char** argv) {
    return dgtl_verify::main(argc, argv);
}
