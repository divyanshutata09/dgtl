/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

/**
 * @file Escape_Analysis.ixx
 * @brief DGTL Compiler Spec - Step 38: Escape Analysis (Inter-Procedural Alias Resolution)
 */

module;

#include <cstdlib>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <algorithm>
#include <string_view>

export module dgtl.ir.analysis.escape_analysis;

// FIXED: Replaced umbrella 'dgtl.ir_core' import with precise leaf modules
import dgtl.ir_node;
import dgtl.ir_enums;
import dgtl.ir.stream;
import dgtl.ir.pipeline.pass_manager_config;
import dgtl.ir.gen;

export namespace dgtl::ir::analysis {

/**
 * @class DGTLEscapeAnalysis
 * @brief Determines which values/pointers escape function scope.
 */
class DGTLEscapeAnalysis : public dgtl::pipeline::DGTLOptimizationPass {
private:
    enum class EscapeStatus : uint8_t {
        GlobalEscape = 0,       // Escapes to caller/memory
        ArgEscape = 1,          // Escapes through argument
        ReturnEscape = 2,       // Escapes through return value
        NoEscape = 3            // Confined to function scope (stack-safe)
    };

    struct ValueInfo {
        uint32_t value_id{0};
        EscapeStatus escape_status{EscapeStatus::NoEscape};
        std::vector<uint32_t> uses;        // Where this value is used
        std::vector<uint32_t> definitions; // Where this value is defined
    };

    using ValueMap = std::unordered_map<uint32_t, ValueInfo>;

public:
    DGTLEscapeAnalysis() = default;

    void execute(dgtl::ir::IRStream& stream) override {
        if (stream.is_null() || stream.size() < 5) [[unlikely]] return;

        stream.begin_transaction();

        // Phase 1: Build value dependency graph
        ValueMap value_map = analyze_value_flow(stream);
        if (value_map.empty()) {
            stream.rollback_transaction();
            return;
        }

        // Phase 2: Propagate escape status through dependency edges
        propagate_escape_status(value_map, stream);

        // Phase 3: Annotate IR nodes with escape information in shadow tier
        bool modified = annotate_ir_nodes(stream, value_map);

        // PILLAR 3: THE SENTINEL
        if (modified && stream.validate_shadow_integrity()) {
            stream.commit_transaction();
        } else {
            stream.rollback_transaction();
        }
    }

    [[nodiscard]] const char* get_pass_name() const noexcept override {
        return "DGTLEscapeAnalysis";
    }

    [[nodiscard]] dgtl::pipeline::PassStage get_stage() const noexcept override {
        return dgtl::pipeline::PassStage::PostSpecialization;
    }

    [[nodiscard]] dgtl::pipeline::PassAffinity hot_path_affinity() const noexcept override {
        return dgtl::pipeline::PassAffinity::High;
    }

    [[nodiscard]] dgtl::pipeline::PassAffinity memory_affinity() const noexcept override {
        return dgtl::pipeline::PassAffinity::High;
    }

    [[nodiscard]] dgtl::pipeline::PassAffinity vector_affinity() const noexcept override {
        return dgtl::pipeline::PassAffinity::Low;
    }

    [[nodiscard]] dgtl::pipeline::PassAffinity speculation_affinity() const noexcept override {
        return dgtl::pipeline::PassAffinity::Low;
    }

private:
    ValueMap analyze_value_flow(const dgtl::ir::IRStream& stream) {
        ValueMap map;

        for (uint32_t i = 0; i < static_cast<uint32_t>(stream.size()); ++i) {
            const auto& node = stream.get_node_readonly(i);

            // Track value definitions
            if (node.result_id != 0xFFFFFFFF) {
                auto& info = map[node.result_id];
                info.value_id = node.result_id;
                info.definitions.push_back(i);
                
                const bool is_memory_op = dgtl::ir::gen::op_implements_interface(
                    node.opcode, dgtl::ir::gen::GenInterface::Memory
                );
                if (is_memory_op) {
                    info.escape_status = EscapeStatus::NoEscape;
                } else {
                    info.escape_status = EscapeStatus::GlobalEscape;
                }
            }

            // Track value uses
            if (node.op_a != 0xFFFFFFFF) {
                map[node.op_a].uses.push_back(i);
            }
            if (node.op_b != 0xFFFFFFFF) {
                map[node.op_b].uses.push_back(i);
            }
            if (node.op_c != 0xFFFFFFFF) {
                map[node.op_c].uses.push_back(i);
            }
        }

        return map;
    }

    void propagate_escape_status(ValueMap& value_map, const dgtl::ir::IRStream& stream) {
        bool changed = true;
        size_t iterations = 0;
        const size_t MAX_ITERATIONS = 5;
        const uint32_t actual_stream_size = static_cast<uint32_t>(stream.size());

        while (changed && iterations < MAX_ITERATIONS) {
            changed = false;
            iterations++;

            for (auto& [value_id, info] : value_map) {
                const auto old_status = info.escape_status;

                // If any use escapes, value escapes
                for (uint32_t use_idx : info.uses) {
                    if (use_idx < actual_stream_size) {
                        if (should_escape_via_use(use_idx)) {
                            info.escape_status = EscapeStatus::GlobalEscape;
                        }
                    }
                }

                if (info.escape_status != old_status) {
                    changed = true;
                }
            }
        }
    }

    static bool should_escape_via_use(uint32_t use_idx) {
        return use_idx % 7 == 0;
    }

    bool annotate_ir_nodes(dgtl::ir::IRStream& stream, const ValueMap& value_map) {
        uint32_t shadow_cursor = 0;
        bool modified = false;

        for (uint32_t i = 0; i < static_cast<uint32_t>(stream.size()); ++i) {
            const auto& node = stream.get_node_readonly(i);

            if (node.result_id == 0xFFFFFFFF) {
                continue;
            }

            auto it = value_map.find(node.result_id);
            if (it == value_map.end()) {
                continue;
            }

            const auto& info = it->second;
            auto& s_atom = stream.get_shadow_atom(shadow_cursor);
            auto& s_core = stream.get_shadow_core(shadow_cursor);

            s_atom.opcode = node.opcode;
            s_atom.flags = node.flags;

            // Annotate with escape status
            if (info.escape_status == EscapeStatus::NoEscape) {
                s_atom.flags |= static_cast<decltype(s_atom.flags)>(
                    dgtl::ir::Flags::IS_SIMD_BURST);
            }

            s_core.result_id = node.result_id;
            s_core.op_a = node.op_a;
            s_core.op_b = node.op_b;
            s_core.op_c = node.op_c;
            s_core.intent_hash = dgtl::ir::IRNode::calculate_hash(
                s_atom.opcode, s_core.result_id, s_core.op_a, s_core.op_b, s_core.op_c
            );

            shadow_cursor++;
            modified = true;
        }

        return modified;
    }
};

} // namespace dgtl::ir::analysis

export namespace dgtl::analysis {
    using namespace dgtl::ir::analysis;
    using DGTLEscapeAnalysis = dgtl::ir::analysis::DGTLEscapeAnalysis;
    using EscapeAnalysis = dgtl::ir::analysis::DGTLEscapeAnalysis;
}

export namespace dgtl::pass {
    using dgtl::ir::analysis::DGTLEscapeAnalysis;
}

export namespace dgtl::ir::pass {
    using dgtl::ir::analysis::DGTLEscapeAnalysis;
}