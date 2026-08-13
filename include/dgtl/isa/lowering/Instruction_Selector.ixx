/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdlib>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

export module dgtl.ir_core.lowering;

// Import native compiler base interfaces and sibling partitions
import dgtl.ir_enums;
import dgtl.ir_traits;
import dgtl.ir_node;
import dgtl.ir_core.runtime_policy;
import dgtl.ir_core.isel_rules;
import dgtl.ir_core.isel_gen;

// FIXED: Explicitly import target descriptions to resolve ArchType and TargetMachine
import dgtl.target;
import dgtl.ir_core;

// FIXED: Removed conflicting manual forward declaration of DGTLAICostModel 
// since it is already fully imported via dgtl.ir_core!

/**
 * @brief Instruction Selector (ISel)
 *
 * Implements tree-pattern matching and instruction selection using a DSL-driven
 * rule engine. Matches high-level target-independent IR patterns and lowers them
 * to optimal target-specific machine-level IR representations (MachineForm). Uses a cost/benefit
 * benefit heuristic to select the most efficient instruction forms (e.g. FMA fuses,
 * AVX vectorization lanes) based on target machine capabilities.
 */
export namespace dgtl::isa::lowering {

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

[[nodiscard]] inline std::string_view trim_copy(std::string_view value) noexcept {
    size_t begin = 0;
    size_t end = value.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return value.substr(begin, end - begin);
}

[[nodiscard]] inline bool ascii_equal_case_insensitive(std::string_view lhs, std::string_view rhs) noexcept {
    if (lhs.size() != rhs.size()) return false;
    for (size_t i = 0; i < lhs.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(lhs[i])) != std::tolower(static_cast<unsigned char>(rhs[i]))) {
            return false;
        }
    }
    return true;
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
            parts.push_back(std::string(trim_copy(text.substr(start, i - start))));
            start = i + 1;
        }
    }
    parts.push_back(std::string(trim_copy(text.substr(start))));
    return parts;
}

[[nodiscard]] inline std::optional<ir::OpCode> parse_opcode_name(std::string_view name) noexcept {
    struct OpCodeEntry { std::string_view str_name; ir::OpCode code; };
    static constexpr OpCodeEntry kOpcodeLookupMap[] = {
        // Constants
        {"const", ir::OpCode::CONST},
        {"const_f64", ir::OpCode::CONST},
        {"immediate", ir::OpCode::IMMEDIATE},
        {"immediate64", ir::OpCode::LOAD_IMM},
        
        // Memory Operations
        {"load", ir::OpCode::LOAD},
        {"load32", ir::OpCode::LOAD},
        {"store", ir::OpCode::STORE},
        {"select", ir::OpCode::SELECT},
        {"getelementptr", ir::OpCode::GEP},
        {"gep", ir::OpCode::GEP},
        {"ptrtoint", ir::OpCode::PTR_TO_INT},
        {"inttoptr", ir::OpCode::INT_TO_PTR},
        
        // Integer 32-bit
        {"addi32", ir::OpCode::ADD_I32},
        {"subi32", ir::OpCode::SUB_I32},
        {"muli32", ir::OpCode::MUL_I32},
        {"divi32", ir::OpCode::DIV_I32},
        {"modi32", ir::OpCode::MOD_I32},
        {"andi32", ir::OpCode::AND_I32},
        {"ori32", ir::OpCode::OR_I32},
        {"xori32", ir::OpCode::XOR_I32},
        {"shli32", ir::OpCode::SHL_I32},
        {"shri32", ir::OpCode::SHR_I32},
        
        // Integer 64-bit
        {"addi64", ir::OpCode::ADD_I64},
        {"subi64", ir::OpCode::SUB_I64},
        {"muli64", ir::OpCode::MUL_I64},
        {"divi64", ir::OpCode::DIV_I64},
        {"modi64", ir::OpCode::MOD_I64},
        {"andi64", ir::OpCode::AND_I64},
        {"ori64", ir::OpCode::OR_I64},
        {"xori64", ir::OpCode::XOR_I64},
        {"shli64", ir::OpCode::SHL_I64},
        {"shri64", ir::OpCode::SHR_I64},
        
        // Floating Point 32-bit
        {"addf32", ir::OpCode::ADD_F32},
        {"subf32", ir::OpCode::SUB_F32},
        {"mulf32", ir::OpCode::MUL_F32},
        {"divf32", ir::OpCode::DIV_F32},
        {"fmaf32", ir::OpCode::FMA_F32},
        
        // Floating Point 64-bit
        {"addf64", ir::OpCode::ADD_F64},
        {"subf64", ir::OpCode::SUB_F64},
        {"mulf64", ir::OpCode::MUL_F64},
        {"divf64", ir::OpCode::DIV_F64},
        
        // Comparison
        {"cmpeqi32", ir::OpCode::CMP_EQ_I32},
        {"cmpnei32", ir::OpCode::CMP_NE_I32},
        {"cmplti32", ir::OpCode::CMP_LT_I32},
        {"cmplei32", ir::OpCode::CMP_LE_I32},
        {"cmpgti32", ir::OpCode::CMP_GT_I32},
        {"cmpgei32", ir::OpCode::CMP_GE_I32},
        
        // Vector Operations (256-bit AVX2)
        {"vecaddf32x256", ir::OpCode::VEC_ADD_F32_512},
        {"vecsubf32x256", ir::OpCode::VEC_ADD_F32_512},
        {"vecmulf32x256", ir::OpCode::VEC_MUL_F32_512},
        {"vecdivf32x256", ir::OpCode::VEC_ADD_F32_512},
        {"vecload256", ir::OpCode::VEC_LOAD},
        {"vecstore256", ir::OpCode::VEC_STORE},
        
        // Vector Operations (512-bit AVX512)
        {"vecaddf32x512", ir::OpCode::VEC_ADD_F32_512},
        {"vecmulf32x512", ir::OpCode::VEC_MUL_F32_512},
        {"gatherf32x512", ir::OpCode::GATHER_F32_512},
        {"storev512", ir::OpCode::STORE_V512},
        
        // SIMD FMA
        {"simdcomplexfma256", ir::OpCode::SIMD_COMPLEX_FMA},
        {"simdcomplexfma512", ir::OpCode::SIMD_COMPLEX_FMA},
        
        // Control Flow & Calls
        {"label", ir::OpCode::LABEL},
        {"goto", ir::OpCode::GOTO},
        {"branch", ir::OpCode::BRANCH},
        {"call", ir::OpCode::CALL},
        {"call_variadic", ir::OpCode::CALL_VARIADIC},
        {"syscall", ir::OpCode::SYSCALL},
        {"return", ir::OpCode::RET},
        {"branchcond", ir::OpCode::BRANCH_COND},
        {"branchcomparefused", ir::OpCode::BRANCH_COMPARE_FUSED},

        // Runtime string intrinsics
        {"string_length", ir::OpCode::STRING_LENGTH},
        {"string_compare", ir::OpCode::STRING_COMPARE},
        {"string_concat", ir::OpCode::STRING_CONCAT},
        {"string_substring", ir::OpCode::STRING_SUBSTRING},

        // Runtime-backed opcodes
        {"acquirefence", ir::OpCode::ACQUIRE_FENCE},
        {"assume", ir::OpCode::ASSUME},
        {"asyncawait", ir::OpCode::ASYNC_AWAIT},
        {"asynclaunch", ir::OpCode::ASYNC_LAUNCH},
        {"asyncspawn", ir::OpCode::ASYNC_SPAWN},
        {"atomicfence", ir::OpCode::ATOMIC_FENCE},
        {"atomicload", ir::OpCode::ATOMIC_LOAD},
        {"atomicsnapshot", ir::OpCode::ATOMIC_SNAPSHOT},
        {"atomicstore", ir::OpCode::ATOMIC_STORE},
        {"barriersync", ir::OpCode::BARRIER_SYNC},
        {"boolselect", ir::OpCode::BOOL_SELECT},
        {"chanrecv", ir::OpCode::CHAN_RECV},
        {"chansend", ir::OpCode::CHAN_SEND},
        {"deopttrigger", ir::OpCode::DEOPT_TRIGGER},
        {"dsaallocate", ir::OpCode::DSA_ALLOCATE},
        {"dsadeallocate", ir::OpCode::DSA_DEALLOCATE},
        {"fcvtlf16tof32", ir::OpCode::FCVTL_F16_TO_F32},
        {"fcvtnf32tof16", ir::OpCode::FCVTN_F32_TO_F16},
        {"fiberyield", ir::OpCode::FIBER_YIELD},
        {"fmawidenf16f32", ir::OpCode::FMA_WIDEN_F16_F32},
        {"fusiongroup", ir::OpCode::FUSION_GROUP},
        {"gather16", ir::OpCode::GATHER_16},
        {"gpulaunch", ir::OpCode::GPU_LAUNCH},
        {"graphbuild", ir::OpCode::GRAPH_BUILD},
        {"graphtraverse", ir::OpCode::GRAPH_TRAVERSE},
        {"guardalias", ir::OpCode::GUARD_ALIAS},
        {"guardvalue", ir::OpCode::GUARD_VALUE},
        {"lockacquire", ir::OpCode::LOCK_ACQUIRE},
        {"lockrelease", ir::OpCode::LOCK_RELEASE},
        {"memoryfence", ir::OpCode::MEMORY_FENCE},
        {"ownershipacquire", ir::OpCode::OWNERSHIP_ACQUIRE},
        {"ownershiprelease", ir::OpCode::OWNERSHIP_RELEASE},
        {"ownershiptransfer", ir::OpCode::OWNERSHIP_TRANSFER},
        {"phimulti", ir::OpCode::PHI_MULTI},
        {"phinode", ir::OpCode::PHI_NODE},
        {"ptrcast", ir::OpCode::PTR_CAST},
        {"ptrdiff", ir::OpCode::PTR_DIFF},
        {"regionbegin", ir::OpCode::REGION_BEGIN},
        {"regionend", ir::OpCode::REGION_END},
        {"releasefence", ir::OpCode::RELEASE_FENCE},
        {"scopanalyze", ir::OpCode::SCOP_ANALYZE},
        {"selectmulti", ir::OpCode::SELECT_MULTI},
        {"simdbslpredicate", ir::OpCode::SIMD_BSL_PREDICATE},
        {"stringconst", ir::OpCode::STRING_CONST},
        {"switchdispatch", ir::OpCode::SWITCH_DISPATCH},
        {"syncbarrier", ir::OpCode::SYNC_BARRIER},
        {"tensorbroadcast", ir::OpCode::TENSOR_BROADCAST},
        {"tensorconcat", ir::OpCode::TENSOR_CONCAT},
        {"tensorconv", ir::OpCode::TENSOR_CONV},
        {"tensorgemm", ir::OpCode::TENSOR_GEMM},
        {"tensorpool", ir::OpCode::TENSOR_POOL},
        {"tensorreduce", ir::OpCode::TENSOR_REDUCE},
        {"tensorreshape", ir::OpCode::TENSOR_RESHAPE},
        {"tensorslice", ir::OpCode::TENSOR_SLICE},
        {"tensorsoftmax", ir::OpCode::TENSOR_SOFTMAX},
        {"tensortranspose", ir::OpCode::TENSOR_TRANSPOSE},
        {"threadspawn", ir::OpCode::THREAD_SPAWN},
        {"readtsc", ir::OpCode::READ_TSC},
        {"spinacquire", ir::OpCode::SPIN_ACQUIRE},
        {"ringpushfree", ir::OpCode::RING_PUSH_FREE},
        {"wasminvoke", ir::OpCode::WASM_INVOKE},
        {"fiberyieldloop", ir::OpCode::FIBER_YIELD_LOOP},
        {"suspendctx", ir::OpCode::SUSPEND_CTX},
        {"resumectx", ir::OpCode::RESUME_CTX},
        {"neonvaddq", ir::OpCode::NEON_VADD_Q},
        {"matmulbf16", ir::OpCode::MATMUL_BF16},
        {"matmulfp8", ir::OpCode::MATMUL_FP8},
        {"sdpaf16", ir::OpCode::SDPA_F16},
        {"softmaxfast", ir::OpCode::SOFTMAX_FAST},
        {"layernorm", ir::OpCode::LAYER_NORM},
        {"ropeembed", ir::OpCode::ROPE_EMBED},
        {"fastinvsqrt", ir::OpCode::FAST_INV_SQRT},
        {"vec3cross", ir::OpCode::VEC3_CROSS},
        {"vec4dot", ir::OpCode::VEC4_DOT},
        {"mat4mul", ir::OpCode::MAT4_MUL},
        {"quatslerp", ir::OpCode::QUAT_SLERP},
        {"rayaabbisect", ir::OpCode::RAY_AABB_ISECT},
        {"branchlikely", ir::OpCode::BRANCH_LIKELY},
        {"branchunlikely", ir::OpCode::BRANCH_UNLIKELY},
        {"cachelineflush", ir::OpCode::CLFLUSH},
        {"cachelineprefetch", ir::OpCode::PREFETCH_T0},
        {"treebuild", ir::OpCode::TREE_BUILD},
        {"treetraverse", ir::OpCode::TREE_TRAVERSE},
        {"triebuild", ir::OpCode::TRIE_BUILD},
        {"trieinsert", ir::OpCode::TRIE_INSERT},
        {"trielookup", ir::OpCode::TRIE_LOOKUP},
        {"tuplepack", ir::OpCode::TUPLE_PACK},
        {"tupleunpack", ir::OpCode::TUPLE_UNPACK},
        {"vecload", ir::OpCode::VEC_LOAD},
        {"vecreduceadd", ir::OpCode::VEC_REDUCE_ADD},
        {"vecshuffle", ir::OpCode::VEC_SHUFFLE},
        {"vecstore", ir::OpCode::VEC_STORE},
        {"volatileload", ir::OpCode::VOLATILE_LOAD},
        {"volatilestore", ir::OpCode::VOLATILE_STORE}
    };

    const std::string_view cleaned_name = trim_copy(name);
    const auto end_it = std::end(kOpcodeLookupMap);
    const auto it = std::find_if(std::begin(kOpcodeLookupMap), end_it,
                                 [cleaned_name](const auto& entry) {
                                     return ascii_equal_case_insensitive(entry.str_name, cleaned_name);
                                 });
    if (it != end_it) return it->code;
    return std::nullopt;
}

struct PatternTerm {
    std::string capture;
    std::optional<ir::OpCode> opcode;
    std::vector<PatternTerm> children;
};

[[nodiscard]] inline PatternTerm parse_term(std::string_view text) {
    const std::string_view token = trim_copy(text);
    if (!token.empty() && token[0] == '$') {
        PatternTerm term{};
        term.capture = std::string(token.substr(1));
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
    term.opcode = parse_opcode_name(token.substr(0, open));
    if (close != std::string::npos && close > open + 1) {
        for (const std::string& part : split_top_level(token.substr(open + 1, close - open - 1), ',')) {
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

[[nodiscard]] inline std::optional<core::ArchType> parse_target_name(std::string_view value) noexcept {
    const std::string_view key = trim_copy(value);
    if (ascii_equal_case_insensitive(key, "x86_64_avx512")) return core::ArchType::X86_64_AVX512;
    if (ascii_equal_case_insensitive(key, "x86_64_avx2")) return core::ArchType::X86_64_AVX2;
    if (ascii_equal_case_insensitive(key, "aarch64_sve") || ascii_equal_case_insensitive(key, "arm64") || ascii_equal_case_insensitive(key, "arm64_sve")) return core::ArchType::AARCH64_SVE;
    if (ascii_equal_case_insensitive(key, "wasm_simd")) return core::ArchType::WASM_SIMD;
    if (ascii_equal_case_insensitive(key, "fpga_verilog")) return core::ArchType::FPGA_VERILOG;
    return std::nullopt;
}

[[nodiscard]] inline std::string lower_ascii(std::string_view value) {
    std::string out(value.size(), '\0');
    std::transform(value.begin(), value.end(), out.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return out;
}

[[nodiscard]] inline SelectionRule parse_rule_line(std::string_view line) {
    SelectionRule rule{};
    std::string_view text = trim_copy(line);
    if (!text.starts_with("rule ")) {
        return rule;
    }

    const size_t colon = text.find(':');
    const size_t arrow = text.find("->");
    const size_t bracket = text.find('[');
    if (colon == std::string::npos || arrow == std::string::npos || arrow <= colon) {
        return rule;
    }

    rule.name = std::string(trim_copy(text.substr(5, colon - 5)));
    const size_t replace_end = (bracket == std::string::npos) ? text.size() : bracket;
    rule.match = parse_term(text.substr(colon + 1, arrow - colon - 1));
    rule.replace = parse_term(text.substr(arrow + 2, replace_end - arrow - 2));

    if (bracket != std::string::npos) {
        const size_t end_bracket = text.rfind(']');
        const std::string_view attrs = text.substr(bracket + 1, end_bracket - bracket - 1);
        for (const std::string& item : split_top_level(attrs, ',')) {
            const size_t eq = item.find('=');
            if (eq == std::string::npos) {
                continue;
            }
            const std::string key = lower_ascii(trim_copy(item.substr(0, eq)));
            const std::string_view value = trim_copy(item.substr(eq + 1));
            if (key == "targets") {
                for (const std::string& target_name : split_top_level(value, '|')) {
                    const auto arch = parse_target_name(target_name);
                    if (arch.has_value()) {
                        rule.targets.push_back(*arch);
                    }
                }
            } else if (key == "benefit") {
                rule.benefit = static_cast<uint32_t>(std::max(1, std::stoi(std::string(value))));
            } else if (key == "require_fma") {
                rule.require_fma = ascii_equal_case_insensitive(value, "true") || value == "1" || ascii_equal_case_insensitive(value, "yes");
            }
        }
    }

    return rule;
}

struct MatchContext {
    const ir::IRStream& stream;
    std::vector<size_t> producer_by_result;

    explicit MatchContext(const ir::IRStream& ir_stream) : stream(ir_stream) {
        size_t max_slot = 0;
        for (size_t i = 0; i < ir_stream.size(); ++i) {
            const ir::IRNode node = ir_stream.get_node(i);
            if (node.result_id != ir::INVALID_SLOT && node.result_id < 1000000 && node.result_id > max_slot) {
                max_slot = node.result_id;
            }
        }
        producer_by_result.assign(max_slot + 1, static_cast<size_t>(-1));
        for (size_t i = 0; i < ir_stream.size(); ++i) {
            const ir::IRNode node = ir_stream.get_node(i);
            if (node.result_id != ir::INVALID_SLOT && node.result_id != 0u && node.result_id <= max_slot) {
                producer_by_result[node.result_id] = i;
            }
        }
    }

    [[nodiscard]] const ir::IRNode* lookup_producer(uint32_t slot) const noexcept {
        if (slot >= producer_by_result.size()) return nullptr;
        const size_t index = producer_by_result[slot];
        if (index == static_cast<size_t>(-1)) return nullptr;
        return &stream.get_node_readonly(index);
    }
};

[[nodiscard]] inline bool arch_matches(const SelectionRule& rule, core::ArchType arch) noexcept {
    if (rule.targets.empty()) {
        return true;
    }
    return std::find(rule.targets.begin(), rule.targets.end(), arch) != rule.targets.end();
}

[[nodiscard]] inline bool supports_rule_requirements(const SelectionRule& rule, const core::TargetMachine& target) noexcept {
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
                                             const std::string& name) noexcept {
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

[[nodiscard]] inline MachineForm classify_machine_form_with_fallback(ir::OpCode opcode) noexcept {
    MachineForm form = generated::classify_machine_form(opcode);
    if (form != MachineForm::Unhandled) [[likely]] {
        return form;
    }

    if (runtime_policy::should_route_unhandled_to_runtime_call(opcode)) {
        return MachineForm::Call;
    }

    switch (opcode) {
        case ir::OpCode::LABEL:
            return MachineForm::NoOp;
        case ir::OpCode::BRANCH:
            return MachineForm::Goto;
        case ir::OpCode::CALL_VARIADIC:
            return MachineForm::Call;
        case ir::OpCode::STRING_LENGTH:
        case ir::OpCode::STRING_COMPARE:
        case ir::OpCode::STRING_CONCAT:
        case ir::OpCode::STRING_SUBSTRING:
            return MachineForm::Call;
        default:
            return MachineForm::Unhandled;
    }
}

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
            const std::string_view line = detail::trim_copy(dsl_text.substr(start, end - start));
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
            const std::string_view line = detail::trim_copy(line_view);
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
        selected.form = classify_machine_form_with_fallback(selected.node.opcode);
        return selected;
    }

    [[nodiscard]] bool empty() const noexcept {
        return rules_.empty();
    }

private:
    std::vector<detail::SelectionRule> rules_;
};

[[nodiscard]] inline const InstructionSelectorProgram& make_builtin_instruction_selector_program() {
    static const InstructionSelectorProgram program = InstructionSelectorProgram::from_rule_lines(generated::kX64CoreRuleDslLines);
    return program;
}

} // namespace dgtl::isa::lowering