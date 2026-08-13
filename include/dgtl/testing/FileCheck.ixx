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
 * @file FileCheck.ixx
 * @brief FileCheck-style IR verification framework for DGTL passes.
 *
 * PHILOSOPHY:
 * No pass should be merged without accompanying FileCheck .dgtl test files.
 * Each test contains CHECK:, CHECK-NEXT:, and CHECK-NOT: annotations
 * that validate IR transformation behavior in a precise, reproducible manner.
 *
 * USAGE IN .dgtl FILES:
 * ; CHECK: This line must appear in the output
 * ; CHECK-NEXT: This must immediately follow the previous CHECK
 * ; CHECK-NOT: This line must NOT appear anywhere
 * ; CHECK-DAG: Order doesn't matter for DAG verification
 *
 * EXAMPLE TEST FILE:
 * ; RUN: dgtl-opt --pass=gvn %s
 * ; CHECK: ADD_I32
 * ; CHECK-NOT: redundant_value
 * ...
 */

module;

#include <cstdlib>

#include <string>
#include <string_view>
#include <sstream>
#include <vector>
#include <optional>
#include <regex>
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdio>

export module dgtl.testing.filecheck;

export namespace dgtl::testing {

[[nodiscard]] inline std::string trim_copy(std::string_view text) noexcept {
    std::size_t start = 0;
    std::size_t end = text.size();
    while (start < end && std::isspace(static_cast<unsigned char>(text[start])) != 0) {
        ++start;
    }
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return std::string(text.substr(start, end - start));
}

/**
 * @brief Normalize line endings by stripping \r characters.
 * Ensures tests pass identically on Windows (\r\n) and Unix (\n) systems.
 */
[[nodiscard]] inline std::string normalize_line_endings(std::string_view text) noexcept {
    std::string result;
    result.reserve(text.size());
    std::copy_if(text.begin(), text.end(), std::back_inserter(result), [](char c) noexcept {
        return c != '\r';  // Strip carriage returns
    });
    return result;
}

/**
 * @enum FileCheckDirective
 * @brief Supported FileCheck annotation types in IR test files.
 */
enum class FileCheckDirective {
    CHECK,       ///< Pattern must appear (next match)
    CHECK_NEXT,  ///< Pattern must appear on the very next line
    CHECK_NOT,   ///< Pattern must NOT appear
    CHECK_DAG,   ///< Patterns can match in any order (DAG semantics)
    CHECK_LABEL, ///< Label for multi-line block matching
    RUN,         ///< Command to execute (front-matter only)
};

/**
 * @struct FileCheckDirectiveRecord
 * @brief A single FileCheck directive from a test file.
 */
struct FileCheckDirectiveRecord {
    FileCheckDirective type{FileCheckDirective::CHECK};
    std::string pattern;      ///< Regex pattern to match
    uint32_t line_number{0};  ///< Line number in source file
    std::string label;        ///< Optional label for CHECK-LABEL
    bool is_regex = true;     ///< true = regex, false = literal string match
};

/**
 * @class FileCheckParser
 * @brief Parses FileCheck directives from IR text files.
 */
class FileCheckParser {
private:
    static constexpr std::string_view kCommentPrefix = "; ";
    static constexpr std::string_view kCheckPrefix = "CHECK:";
    static constexpr std::string_view kCheckNextPrefix = "CHECK-NEXT:";
    static constexpr std::string_view kCheckNotPrefix = "CHECK-NOT:";
    static constexpr std::string_view kCheckDagPrefix = "CHECK-DAG:";
    static constexpr std::string_view kCheckLabelPrefix = "CHECK-LABEL:";
    static constexpr std::string_view kRunPrefix = "RUN:";

public:
    /**
     * @brief Parse FileCheck directives from IR source text.
     * @param source The full IR file content
     * @return Vector of parsed FileCheck directives
     */
    [[nodiscard]] static std::vector<FileCheckDirectiveRecord> parse(
        std::string_view source
    ) noexcept {
        std::vector<FileCheckDirectiveRecord> directives;
        
        // Normalize line endings first (strip \r for cross-platform compatibility)
        const std::string normalized_source = normalize_line_endings(source);
        std::istringstream iss(normalized_source);
        std::string line;
        uint32_t line_number = 0;

        while (std::getline(iss, line)) {
            ++line_number;
            
            // Skip empty lines
            if (line.empty()) {
                continue;
            }

            // Look for comment prefix
            if (!std::string_view(line).starts_with(kCommentPrefix)) {
                continue;  // Not a comment line
            }

            // Extract comment body
            std::string_view comment = std::string_view(line).substr(kCommentPrefix.size());

            // Try to parse as FileCheck directive
            FileCheckDirectiveRecord record{FileCheckDirective::CHECK, "", line_number, "", true};
            bool is_directive = false;

            if (comment.starts_with(kCheckLabelPrefix)) {
                record.type = FileCheckDirective::CHECK_LABEL;
                record.label = trim_copy(comment.substr(kCheckLabelPrefix.size()));
                is_directive = true;
            } else if (comment.starts_with(kCheckNotPrefix)) {
                record.type = FileCheckDirective::CHECK_NOT;
                record.pattern = trim_copy(comment.substr(kCheckNotPrefix.size()));
                is_directive = true;
            } else if (comment.starts_with(kCheckNextPrefix)) {
                record.type = FileCheckDirective::CHECK_NEXT;
                record.pattern = trim_copy(comment.substr(kCheckNextPrefix.size()));
                is_directive = true;
            } else if (comment.starts_with(kCheckDagPrefix)) {
                record.type = FileCheckDirective::CHECK_DAG;
                record.pattern = trim_copy(comment.substr(kCheckDagPrefix.size()));
                is_directive = true;
            } else if (comment.starts_with(kCheckPrefix)) {
                record.type = FileCheckDirective::CHECK;
                record.pattern = trim_copy(comment.substr(kCheckPrefix.size()));
                is_directive = true;
            } else if (comment.starts_with(kRunPrefix)) {
                record.type = FileCheckDirective::RUN;
                record.pattern = trim_copy(comment.substr(kRunPrefix.size()));
                is_directive = true;
            }

            if (is_directive) {
                directives.push_back(record);
            }
        }

        return directives;
    }

private:
    FileCheckParser() = delete;
};

/**
 * @class FileCheckMatcher
 * @brief Matches IR output against FileCheck directives.
 */
class FileCheckMatcher {
public:
    struct MatchResult {
        bool success = false;
        std::string error_message;
        uint32_t failed_line = 0;
        uint32_t directive_line = 0;
    };

    /**
     * @brief Verify IR output against FileCheck directives.
     * @param directives Parsed FileCheck directives
     * @param output The IR output text to verify
     * @return MatchResult with success flag and diagnostic information
     */
    [[nodiscard]] static MatchResult verify(
        const std::vector<FileCheckDirectiveRecord>& directives,
        std::string_view output
    ) noexcept {
        MatchResult result;
        result.success = true;

        // Normalize output for cross-platform consistency (strip \r)
        const std::string normalized_output = normalize_line_endings(output);
        std::istringstream iss(normalized_output);
        std::string output_line;
        std::vector<std::string> output_lines;

        // Read all output lines
        while (std::getline(iss, output_line)) {
            output_lines.push_back(output_line);
        }

        // Process each directive
        uint32_t next_output_idx = 0;

        for (const auto& directive : directives) {
            if (directive.type == FileCheckDirective::RUN) {
                continue;  // RUN directives are meta; skip during verification
            }

            if (directive.type == FileCheckDirective::CHECK_NOT) {
                // CHECK-NOT: ensure pattern does NOT appear
                const bool found = std::any_of(output_lines.begin(), output_lines.end(),
                    [&](const auto& line) {
                        return matches_pattern(line, directive.pattern, directive.is_regex);
                    });
                if (found) {
                    result.success = false;
                    result.error_message = "CHECK-NOT pattern found but should not exist: " + directive.pattern;
                    result.directive_line = directive.line_number;
                    return result;
                }
            } else if (directive.type == FileCheckDirective::CHECK_NEXT) {
                // CHECK-NEXT: must match the immediate next line
                if (next_output_idx >= output_lines.size() ||
                    !matches_pattern(output_lines[next_output_idx], directive.pattern, directive.is_regex)) {
                    result.success = false;
                    result.error_message = "CHECK-NEXT pattern not matched on line " +
                                           std::to_string(next_output_idx + 1) +
                                           ": " + directive.pattern;
                    result.directive_line = directive.line_number;
                    result.failed_line = next_output_idx + 1;
                    return result;
                }
                ++next_output_idx;
            } else if (directive.type == FileCheckDirective::CHECK ||
                      directive.type == FileCheckDirective::CHECK_DAG) {
                // CHECK: find next matching line (skip intermediate lines)
                auto it = std::find_if(output_lines.begin() + next_output_idx, output_lines.end(),
                    [&](const auto& line) {
                        return matches_pattern(line, directive.pattern, directive.is_regex);
                    });

                if (it == output_lines.end()) {
                    result.success = false;
                    result.error_message = "CHECK pattern not found: " + directive.pattern;
                    result.directive_line = directive.line_number;
                    result.failed_line = static_cast<uint32_t>(output_lines.size());
                    return result;
                }
                next_output_idx = static_cast<uint32_t>(std::distance(output_lines.begin(), it) + 1);
            } else if (directive.type == FileCheckDirective::CHECK_LABEL) {
                // CHECK-LABEL: find label line
                auto it = std::find_if(output_lines.begin() + next_output_idx, output_lines.end(),
                    [&](const auto& line) {
                        return line.find(directive.label) != std::string::npos;
                    });

                if (it != output_lines.end()) {
                    next_output_idx = static_cast<uint32_t>(std::distance(output_lines.begin(), it) + 1);
                } else {
                    result.success = false;
                    result.error_message = "CHECK-LABEL not found: " + directive.label;
                    result.directive_line = directive.line_number;
                    return result;
                }
            }
        }

        return result;  // All directives matched
    }

private:
    /**
     * @brief Check if a line matches a pattern (regex or literal).
     */
    [[nodiscard]] static bool matches_pattern(
        std::string_view line,
        std::string_view pattern,
        bool is_regex
    ) noexcept {
        if (!is_regex) {
            // Literal string match
            return line.find(pattern) != std::string_view::npos;
        }

        // Regex match
        try {
            std::regex regex_pattern{std::string(pattern)};
            const std::string line_text(line);
            return std::regex_search(line_text, regex_pattern);
        } catch (...) {
            return false;  // Regex compilation error
        }
    }

    FileCheckMatcher() = delete;
};

/**
 * @class FileCheckTestRunner
 * @brief Execute and verify FileCheck tests end-to-end.
 */
class FileCheckTestRunner {
public:
    struct TestResult {
        bool passed = false;
        std::string test_name;
        std::string error_message;
        uint32_t directive_line = 0;
        uint32_t failed_output_line = 0;
    };

    /**
     * @brief Run a FileCheck test from IR source text.
     * @param test_name Name of the test (for reporting)
     * @param source IR source file content
     * @param ir_output Output from dgtl-opt or other tool
     * @return TestResult with pass/fail status and diagnostics
     */
    [[nodiscard]] static TestResult run_test(
        std::string_view test_name,
        std::string_view source,
        std::string_view ir_output
    ) noexcept {
        TestResult result;
        result.test_name = std::string(test_name);

        // Parse directives
        const auto directives = FileCheckParser::parse(source);

        // Match output against directives
        const auto match_result = FileCheckMatcher::verify(directives, ir_output);

        if (!match_result.success) {
            result.passed = false;
            result.error_message = match_result.error_message;
            result.directive_line = match_result.directive_line;
            result.failed_output_line = match_result.failed_line;
            return result;
        }

        result.passed = true;
        return result;
    }

    /**
     * @brief Print test result in human-readable format.
     */
    static void print_result(const TestResult& result, bool verbose = false) noexcept {
        if (result.passed) {
            std::printf("âœ“ %s PASSED\n", result.test_name.c_str());
        } else {
            std::printf("âœ— %s FAILED\n", result.test_name.c_str());
            std::printf("  Error: %s\n", result.error_message.c_str());
            if (verbose) {
                if (result.directive_line > 0) {
                    std::printf("  Directive line: %u\n", result.directive_line);
                }
                if (result.failed_output_line > 0) {
                    std::printf("  Failed output line: %u\n", result.failed_output_line);
                }
            }
        }
    }
};

} // namespace dgtl::testing