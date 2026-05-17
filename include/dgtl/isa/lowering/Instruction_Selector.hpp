/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 */

#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "dgtl/core/Target.hpp"
#include "dgtl/ir/base/IR_Stream.hpp"
#include "dgtl/isa/lowering/gen/X64_ISel_Generated.hpp"

namespace dgtl::isa::lowering {

struct ISelReplacement {
    bool matched = false;
    ir::IRNode node{};
    std::string_view rule_name{};
    uint32_t benefit = 0;
};

struct SelectedInstruction {
    MachineForm form = MachineForm::Unhandled;
    ir::IRNode node{};
    ir::OpCode semantic_opcode = ir::OpCode::NOP;
    std::string_view rule_name{};
    bool matched_rule = false;
};

namespace detail {

[[nodiscard]] inline std::string trim_copy(std::string_view value) {
    size_t begin = 0;
    size_t end = value.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return std::string(value.substr(begin, end - begin));
}

[[nodiscard]] inline std::string lower_ascii(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return out;
}

[[nodiscard]] inline std::vector<std::string> split_top_level(std::string_view text, char delimiter) {
    std::vector<std::string> parts;
    size_t start = 0;
    int depth = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (ch == '(') {
            ++depth;
        } else if (ch == ')') {
            --depth;
        } else if (ch == delimiter && depth == 0) {
            parts.push_back(trim_copy(text.substr(start, i - start)));
            start = i + 1;
        }
    }
    parts.push_back(trim_copy(text.substr(start)));
    return parts;
}

[[nodiscard]] inline std::optional<ir::OpCode> parse_opcode_name(std::string_view name) {
    static const std::unordered_map<std::string, ir::OpCode> kNames = {
        {"addf32", ir::OpCode::ADD_F32},
        {"mulf32", ir::OpCode::MUL_F32},
        {"fma_f32", ir::OpCode::FMA_F32},
        {"addi32", ir::OpCode::ADD_I32},
        {"subi32", ir::OpCode::SUB_I32},
        {"muli32", ir::OpCode::MUL_I32},
        {"addi64", ir::OpCode::ADD_I64},
        {"subi64", ir::OpCode::SUB_I64},
        {"muli64", ir::OpCode::MUL_I64},
        {"load", ir::OpCode::LOAD},
        {"store", ir::OpCode::STORE},
        {"ret", ir::OpCode::RET},
        {"branchcond", ir::OpCode::BRANCH_COND},
        {"branchcomparefused", ir::OpCode::BRANCH_COMPARE_FUSED},
        {"vecaddf32512", ir::OpCode::VEC_ADD_F32_512},
        {"vecmulf32512", ir::OpCode::VEC_MUL_F32_512},
        {"gatherf32512", ir::OpCode::GATHER_F32_512},
        {"storev512", ir::OpCode::STORE_V512}
    };

    const auto it = kNames.find(lower_ascii(name));
    if (it == kNames.end()) {
        return std::nullopt;
    }
    return it->second;
}

struct PatternTerm {
    std::string capture;
    std::optional<ir::OpCode> opcode;
    std::vector<PatternTerm> children;
};

[[nodiscard]] inline PatternTerm parse_term(std::string_view text) {
    const std::string token = trim_copy(text);
    if (!token.empty() && token[0] == '$') {
        PatternTerm term{};
        term.capture = token.substr(1);
        return term;
    }

    const size_t open = token.find('(');
    if (open == std::string::npos) {
        PatternTerm term{};
        term.opcode = parse_opcode_name(token);
        return term;
    }

    const size_t close = token.rfind(')');
    PatternTerm term{};
    term.opcode = parse_opcode_name(std::string_view(token).substr(0, open));
    if (close != std::string::npos && close > open + 1) {
        for (const std::string& part : split_top_level(std::string_view(token).substr(open + 1, close - open - 1), ',')) {
            term.children.push_back(parse_term(part));
        }
    }
    return term;
}

struct SelectionRule {
    std::string name;
    PatternTerm match;
    PatternTerm replace;
    std::vector<core::ArchType> targets;
    uint32_t benefit = 1;
    bool require_fma = false;
};

[[nodiscard]] inline std::optional<core::ArchType> parse_target_name(std::string_view value) {
    const std::string key = lower_ascii(trim_copy(value));
    if (key == "x86_64_avx512") return core::ArchType::X86_64_AVX512;
    if (key == "x86_64_avx2") return core::ArchType::X86_64_AVX2;
    if (key == "aarch64_sve" || key == "arm64" || key == "arm64_sve") return core::ArchType::AARCH64_SVE;
    if (key == "wasm_simd") return core::ArchType::WASM_SIMD;
    if (key == "fpga_verilog") return core::ArchType::FPGA_VERILOG;
    return std::nullopt;
}

[[nodiscard]] inline SelectionRule parse_rule_line(std::string_view line) {
    SelectionRule rule{};
    std::string text = trim_copy(line);
    const size_t rule_pos = text.find("rule ");
    if (rule_pos != 0) {
        return rule;
    }

    const size_t colon = text.find(':');
    const size_t arrow = text.find("->");
    const size_t bracket = text.find('[');
    if (colon == std::string::npos || arrow == std::string::npos || arrow <= colon) {
        return rule;
    }

    rule.name = trim_copy(std::string_view(text).substr(5, colon - 5));
    const size_t replace_end = (bracket == std::string::npos) ? text.size() : bracket;
    rule.match = parse_term(std::string_view(text).substr(colon + 1, arrow - colon - 1));
    rule.replace = parse_term(std::string_view(text).substr(arrow + 2, replace_end - arrow - 2));

    if (bracket != std::string::npos) {
        const size_t end_bracket = text.rfind(']');
        const std::string_view attrs = std::string_view(text).substr(bracket + 1, end_bracket - bracket - 1);
        for (const std::string& item : split_top_level(attrs, ',')) {
            const size_t eq = item.find('=');
            if (eq == std::string::npos) {
                continue;
            }
            const std::string key = lower_ascii(std::string_view(item).substr(0, eq));
            const std::string value = trim_copy(std::string_view(item).substr(eq + 1));
            if (key == "targets") {
                for (const std::string& target_name : split_top_level(value, '|')) {
                    const auto arch = parse_target_name(target_name);
                    if (arch.has_value()) {
                        rule.targets.push_back(*arch);
                    }
                }
            } else if (key == "benefit") {
                rule.benefit = static_cast<uint32_t>(std::max(1, std::stoi(value)));
            } else if (key == "require_fma") {
                const std::string lowered = lower_ascii(value);
                rule.require_fma = lowered == "true" || lowered == "1" || lowered == "yes";
            }
        }
    }

    return rule;
}

struct MatchContext {
    const ir::IRStream& stream;
    std::unordered_map<uint32_t, size_t> producer_by_result;

    explicit MatchContext(const ir::IRStream& ir_stream) : stream(ir_stream) {
        producer_by_result.reserve(ir_stream.size());
        for (size_t i = 0; i < ir_stream.size(); ++i) {
            const ir::IRNode node = ir_stream.get_node(i);
            if (node.result_id != ir::INVALID_SLOT && node.result_id != 0u) {
                producer_by_result.emplace(node.result_id, i);
            }
        }
    }

    [[nodiscard]] const ir::IRNode* lookup_producer(uint32_t slot) const {
        const auto it = producer_by_result.find(slot);
        if (it == producer_by_result.end()) {
            return nullptr;
        }
        return &stream.get_node_readonly(it->second);
    }
};

[[nodiscard]] inline bool arch_matches(const SelectionRule& rule, core::ArchType arch) {
    if (rule.targets.empty()) {
        return true;
    }
    return std::find(rule.targets.begin(), rule.targets.end(), arch) != rule.targets.end();
}

[[nodiscard]] inline bool supports_rule_requirements(const SelectionRule& rule, const core::TargetMachine& target) {
    if (rule.require_fma && !target.get_caps().has_fma) {
        return false;
    }
    return true;
}

[[nodiscard]] inline uint32_t operand_at(const ir::IRNode& node, size_t index) noexcept {
    switch (index) {
        case 0: return node.op_a;
        case 1: return node.op_b;
        case 2: return node.op_c;
        default: return ir::INVALID_SLOT;
    }
}

[[nodiscard]] inline bool match_term(const PatternTerm& term,
                                     const ir::IRNode& node,
                                     const MatchContext& ctx,
                                     std::unordered_map<std::string, uint32_t>& captures) {
    if (!term.capture.empty()) {
        captures[term.capture] = node.result_id;
        return true;
    }

    if (!term.opcode.has_value() || node.opcode != *term.opcode) {
        return false;
    }

    for (size_t i = 0; i < term.children.size(); ++i) {
        const PatternTerm& child = term.children[i];
        const uint32_t operand = operand_at(node, i);
        if (!child.capture.empty()) {
            captures[child.capture] = operand;
            continue;
        }
        const ir::IRNode* producer = ctx.lookup_producer(operand);
        if (producer == nullptr || !match_term(child, *producer, ctx, captures)) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] inline uint32_t lookup_capture(const std::unordered_map<std::string, uint32_t>& captures,
                                             const std::string& name) {
    const auto it = captures.find(name);
    return it == captures.end() ? ir::INVALID_SLOT : it->second;
}

[[nodiscard]] inline ir::IRNode build_replacement(const SelectionRule& rule,
                                                  const ir::IRNode& original,
                                                  const std::unordered_map<std::string, uint32_t>& captures) {
    ir::IRNode node = original;
    if (rule.replace.opcode.has_value()) {
        node.opcode = *rule.replace.opcode;
    }

    node.op_a = ir::INVALID_SLOT;
    node.op_b = ir::INVALID_SLOT;
    node.op_c = ir::INVALID_SLOT;
    for (size_t i = 0; i < rule.replace.children.size() && i < 3; ++i) {
        const PatternTerm& child = rule.replace.children[i];
        const uint32_t value = child.capture.empty() ? ir::INVALID_SLOT : lookup_capture(captures, child.capture);
        switch (i) {
            case 0: node.op_a = value; break;
            case 1: node.op_b = value; break;
            case 2: node.op_c = value; break;
            default: break;
        }
    }
    node.refresh_intent_hash();
    return node;
}

} // namespace detail

class InstructionSelectorProgram {
public:
    InstructionSelectorProgram() = default;

    [[nodiscard]] static InstructionSelectorProgram from_dsl(std::string_view dsl_text) {
        InstructionSelectorProgram program;
        size_t start = 0;
        while (start < dsl_text.size()) {
            size_t end = dsl_text.find('\n', start);
            if (end == std::string_view::npos) {
                end = dsl_text.size();
            }
            const std::string line = detail::trim_copy(dsl_text.substr(start, end - start));
            start = end + 1;
            if (line.empty() || line[0] == '#') {
                continue;
            }
            detail::SelectionRule rule = detail::parse_rule_line(line);
            if (!rule.name.empty() && rule.match.opcode.has_value() && rule.replace.opcode.has_value()) {
                program.rules_.push_back(std::move(rule));
            }
        }
        return program;
    }

    [[nodiscard]] static InstructionSelectorProgram from_rule_lines(std::span<const std::string_view> lines) {
        InstructionSelectorProgram program;
        for (const std::string_view line_view : lines) {
            const std::string line = detail::trim_copy(line_view);
            if (line.empty() || line[0] == '#') {
                continue;
            }
            detail::SelectionRule rule = detail::parse_rule_line(line);
            if (!rule.name.empty() && rule.match.opcode.has_value() && rule.replace.opcode.has_value()) {
                program.rules_.push_back(std::move(rule));
            }
        }
        return program;
    }

    [[nodiscard]] ISelReplacement select_node(const ir::IRStream& stream,
                                              size_t index,
                                              const ir::IRNode& node,
                                              const core::TargetMachine& target) const {
        (void)index;
        detail::MatchContext ctx(stream);
        ISelReplacement best{};
        for (const detail::SelectionRule& rule : rules_) {
            if (!detail::arch_matches(rule, target.get_arch())) {
                continue;
            }
            if (!detail::supports_rule_requirements(rule, target)) {
                continue;
            }
            if (!rule.match.opcode.has_value() || node.opcode != *rule.match.opcode) {
                continue;
            }

            std::unordered_map<std::string, uint32_t> captures;
            if (!detail::match_term(rule.match, node, ctx, captures)) {
                continue;
            }

            if (!best.matched || rule.benefit > best.benefit) {
                best.matched = true;
                best.rule_name = rule.name;
                best.benefit = rule.benefit;
                best.node = detail::build_replacement(rule, node, captures);
            }
        }
        return best;
    }

    [[nodiscard]] SelectedInstruction select_instruction(const ir::IRStream& stream,
                                                         size_t index,
                                                         const ir::IRNode& node,
                                                         const core::TargetMachine& target) const {
        const ISelReplacement replacement = select_node(stream, index, node, target);
        SelectedInstruction selected{};
        selected.node = replacement.matched ? replacement.node : node;
        selected.semantic_opcode = selected.node.opcode;
        selected.rule_name = replacement.rule_name;
        selected.matched_rule = replacement.matched;
        selected.form = generated::classify_machine_form(selected.node.opcode);
        return selected;
    }

    [[nodiscard]] bool empty() const noexcept {
        return rules_.empty();
    }

private:
    std::vector<detail::SelectionRule> rules_;
};

[[nodiscard]] inline InstructionSelectorProgram make_builtin_instruction_selector_program() {
    return InstructionSelectorProgram::from_rule_lines(generated::kX64CoreRuleDslLines);
}

} // namespace dgtl::isa::lowering
