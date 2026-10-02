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

/*
 * Unified tuning header:
 * - Auto tuner
 * - Microarchitecture tuning
 * - Profile-guided optimization
 */

module;

#include <cstdlib>

#include <chrono>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <memory>
#include <cstring>
#include <vector>
#include <unordered_map>
#include <algorithm>

export module dgtl.ir.pass.tuning_optimize;

import dgtl.ir;

// import dgtl.ir.gen;

// import dgtl.ir_formal_verifier;
// import dgtl.ir.rewrite;
import dgtl.ir.pipeline.pass_manager_config;
import dgtl.jit.executor;

export namespace dgtl::ir::pass {

struct TuningResult {
    uint32_t best_unroll_factor;
    uint64_t cycles_per_iter;

    [[nodiscard]] uint32_t get_best_unroll_factor() const noexcept { return best_unroll_factor; }
    [[nodiscard]] uint64_t get_cycles_per_iter() const noexcept { return cycles_per_iter; }
};

class DGTLAutoTuner {
    dgtl::runtime::DGTLExecutor& _engine;

public:
    explicit DGTLAutoTuner(dgtl::runtime::DGTLExecutor& engine) : _engine(engine) {}

    TuningResult tune_loop_nest(const ir::IRStream& base_stream) {
        if (base_stream.size() == 0) {
            return {1, 0};
        }

        (void)_engine;

        constexpr uint32_t factors[] = {1, 2, 4, 8};
        uint32_t best_factor = 1;
        uint64_t min_cycles = UINT64_MAX;

        for (uint32_t factor : factors) {
            ir::IRStream shadow_candidate = clone_and_unroll_transactional(base_stream, factor);

            if (shadow_candidate.size() == 0) [[unlikely]] continue;

            if (!shadow_candidate.validate_shadow_integrity()) {
                continue;
            }

            const uint64_t duration = estimate_cycles_fallback(shadow_candidate, factor);

            if (duration < min_cycles) {
                min_cycles = duration;
                best_factor = factor;
            }
        }

        return { best_factor, min_cycles };
    }

private:
    static inline uint64_t read_hardware_counter_start() noexcept {
#if defined(__x86_64__)
        uint32_t lo, hi;
        __asm__ __volatile__ ("cpuid\n\t" "rdtsc\n\t" : "=a"(lo), "=d"(hi) : "a"(0) : "%ebx", "%ecx");
        return (static_cast<uint64_t>(hi) << 32) | lo;
#elif defined(__aarch64__)
        uint64_t val;
        __asm__ __volatile__("isb\n\t" "mrs %0, cntvct_el0" : "=r"(val));
        return val;
#else
        return std::chrono::high_resolution_clock::now().time_since_epoch().count();
#endif
    }

    static inline uint64_t read_hardware_counter_end() noexcept {
#if defined(__x86_64__)
        uint32_t lo, hi;
        __asm__ __volatile__ ("rdtscp\n\t" "mov %%edx, %0\n\t" "mov %%eax, %1\n\t" "cpuid\n\t"
                              : "=r"(hi), "=r"(lo) : : "%rax", "%rbx", "%rcx", "%rdx");
        return (static_cast<uint64_t>(hi) << 32) | lo;
#elif defined(__aarch64__)
        uint64_t val;
        __asm__ __volatile__("mrs %0, cntvct_el0\n\t" "isb" : "=r"(val));
        return val;
#else
        return std::chrono::high_resolution_clock::now().time_since_epoch().count();
#endif
    }

    ir::IRStream clone_and_unroll_transactional(const ir::IRStream& src, uint32_t factor) {
        const size_t src_size = src.size();
        if (factor == 0) {
            return ir::IRStream::allocate_shadow(1);
        }

        if (src_size > (std::numeric_limits<size_t>::max() / factor)) {
            return ir::IRStream::allocate_shadow(src_size);
        }

        ir::IRStream shadow = ir::IRStream::allocate_shadow(src_size * factor);

        if (factor == 1) {
            shadow.copy_from(src);
            return shadow;
        }

        for (uint32_t f = 0; f < factor; ++f) {
            for (uint32_t i = 0; i < static_cast<uint32_t>(src_size); ++i) {
                ir::IRNode node = src.get_node_readonly(i);

                const bool is_term = dgtl::ir::gen::op_has_trait(node.opcode, dgtl::ir::gen::GenTrait::IsTerminator);

                if (!is_term || (f == factor - 1)) {
                    const uint32_t reg_offset = (f * 0x4000);

                    if (node.result_id != ir::INVALID_SLOT) node.result_id += reg_offset;
                    if (node.op_a != ir::INVALID_SLOT) node.op_a += reg_offset;
                    if (node.op_b != ir::INVALID_SLOT) node.op_b += reg_offset;
                    if (node.op_c != ir::INVALID_SLOT) node.op_c += reg_offset;

                    node.flags |= ir::Flags::IS_SIMD_BURST;
                    node.logic_flags |= ir::HintFlags::IsHosted;

                    node.intent_hash = ir::IRNode::calculate_hash(
                        node.opcode,
                        node.result_id,
                        node.op_a,
                        node.op_b,
                        node.op_c
                    );

                    shadow.push_back(node);
                }
            }
        }
        return shadow;
    }

    static uint64_t estimate_cycles_fallback(const ir::IRStream& stream, uint32_t factor) noexcept {
        const uint64_t t0 = read_hardware_counter_start();
        const uint64_t node_cost = static_cast<uint64_t>(stream.size()) * 64ULL;
        const uint64_t unroll_penalty = static_cast<uint64_t>(factor) * 8ULL;
        const uint64_t logic_mix = stream.generate_logic_hash() & 0x3FFULL;
        const uint64_t t1 = read_hardware_counter_end();
        return node_cost + unroll_penalty + logic_mix + (t1 > t0 ? (t1 - t0) & 0xFFULL : 0ULL);
    }
};

class DGTLMicroarchitectureTuning : public dgtl::pipeline::DGTLOptimizationPass {
private:
    enum class MemoryPattern : uint8_t {
        StrideAccess = 0,
        TemporalReuse = 1,
        SpatialReuse = 2,
        RandomAccess = 3
    };

    struct MemoryAccessInfo {
        uint32_t load_store_idx;
        MemoryPattern pattern;
        int64_t stride;
        uint32_t reuse_distance;
    };

public:
    DGTLMicroarchitectureTuning() = default;

    void execute(dgtl::ir::IRStream& stream) override {
        if (stream.is_null() || stream.size() < 10) [[unlikely]] return;

        stream.begin_transaction();

        std::vector<MemoryAccessInfo> mem_accesses = analyze_memory_patterns(stream);
        if (mem_accesses.empty()) {
            stream.rollback_transaction();
            return;
        }

        std::vector<uint32_t> prefetch_positions = identify_prefetch_opportunities(stream, mem_accesses);

        bool modified = apply_tuning(stream, prefetch_positions);

        if (modified && stream.validate_shadow_integrity()) {
            stream.commit_transaction();
        } else {
            stream.rollback_transaction();
        }
    }

    [[nodiscard]] const char* get_pass_name() const noexcept override {
        return "DGTLMicroarchitectureTuning";
    }

    [[nodiscard]] dgtl::pipeline::PassStage get_stage() const noexcept override {
        return dgtl::pipeline::PassStage::PostInlining;
    }

    [[nodiscard]] dgtl::pipeline::PassAffinity hot_path_affinity() const noexcept override {
        return dgtl::pipeline::PassAffinity::High;
    }

    [[nodiscard]] dgtl::pipeline::PassAffinity memory_affinity() const noexcept override {
        return dgtl::pipeline::PassAffinity::High;
    }

    [[nodiscard]] dgtl::pipeline::PassAffinity vector_affinity() const noexcept override {
        return dgtl::pipeline::PassAffinity::Medium;
    }

    [[nodiscard]] dgtl::pipeline::PassAffinity speculation_affinity() const noexcept override {
        return dgtl::pipeline::PassAffinity::Low;
    }

private:
    std::vector<MemoryAccessInfo> analyze_memory_patterns(const dgtl::ir::IRStream& stream) {
        std::vector<MemoryAccessInfo> accesses;

        for (uint32_t i = 0; i + 2 < static_cast<uint32_t>(stream.size()); ++i) {
            const auto& node = stream.get_node_readonly(i);

            if (!dgtl::ir::gen::op_implements_interface(node.opcode, dgtl::ir::gen::GenInterface::Memory)) {
                continue;
            }

            MemoryAccessInfo info{
                .load_store_idx = i,
                .pattern = MemoryPattern::RandomAccess,
                .stride = 0,
                .reuse_distance = 0
            };

            if (i + 1 < static_cast<uint32_t>(stream.size())) {
                const auto& next = stream.get_node_readonly(i + 1);
                if ((next.opcode == dgtl::ir::OpCode::ADD_I64 ||
                     next.opcode == dgtl::ir::OpCode::ADD_I32) &&
                    next.op_a == node.result_id) {
                    info.pattern = MemoryPattern::StrideAccess;
                    info.stride = 1;
                }
            }

            accesses.push_back(info);
        }

        return accesses;
    }

    std::vector<uint32_t> identify_prefetch_opportunities(
        const dgtl::ir::IRStream& stream,
        const std::vector<MemoryAccessInfo>& accesses) {

        std::vector<uint32_t> prefetch_positions;
        const uint32_t PREFETCH_LOOKAHEAD = 8;
        uint32_t stream_size = static_cast<uint32_t>(stream.size());

        for (const auto& access : accesses) {
            if (access.load_store_idx >= stream_size) continue;

            if (access.pattern == MemoryPattern::StrideAccess ||
                access.pattern == MemoryPattern::SpatialReuse) {

                uint32_t prefetch_pos = (access.load_store_idx >= PREFETCH_LOOKAHEAD)
                    ? (access.load_store_idx - PREFETCH_LOOKAHEAD)
                    : 0;

                prefetch_positions.push_back(prefetch_pos);
            }
        }

        std::sort(prefetch_positions.begin(), prefetch_positions.end());
        prefetch_positions.erase(
            std::unique(prefetch_positions.begin(), prefetch_positions.end()),
            prefetch_positions.end()
        );

        return prefetch_positions;
    }

    bool apply_tuning(dgtl::ir::IRStream& stream,
                     const std::vector<uint32_t>& prefetch_positions) {

        uint32_t shadow_cursor = 0;
        size_t prefetch_idx = 0;
        bool modified = false;

        for (uint32_t i = 0; i < static_cast<uint32_t>(stream.size()); ++i) {
            if (prefetch_idx < prefetch_positions.size() &&
                i == prefetch_positions[prefetch_idx]) {

                auto& p_atom = stream.get_shadow_atom(shadow_cursor);
                auto& p_core = stream.get_shadow_core(shadow_cursor);

                p_atom.opcode = dgtl::ir::OpCode::PREFETCH;
                p_atom.flags = static_cast<decltype(p_atom.flags)>(
                    dgtl::ir::Flags::IS_SIMD_BURST
                );
                p_core.result_id = 0xFFFFFFFF;
                p_core.op_a = 0;
                p_core.op_b = 0;
                p_core.op_c = 0;
                p_core.intent_hash = dgtl::ir::IRNode::calculate_hash(
                    p_atom.opcode, p_core.result_id, p_core.op_a, p_core.op_b, p_core.op_c
                );

                shadow_cursor++;
                prefetch_idx++;
            }

            const auto& node = stream.get_node_readonly(i);
            auto& s_atom = stream.get_shadow_atom(shadow_cursor);
            auto& s_core = stream.get_shadow_core(shadow_cursor);

            s_atom.opcode = node.opcode;
            s_atom.flags = node.flags | static_cast<decltype(s_atom.flags)>(
                dgtl::ir::Flags::IS_OPTIMIZED
            );
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

class DGTLProfileGuidedOptimization : public dgtl::pipeline::DGTLOptimizationPass {
private:
    struct ProfileData {
        uint32_t node_idx;
        uint64_t execution_count;
        double hotness_score;

        [[nodiscard]] double get_hotness() const noexcept { return hotness_score; }
    };

    struct BasicBlockProfile {
        uint32_t start_idx{0};
        uint32_t end_idx{0};
        uint64_t execution_count{0};
        std::vector<uint32_t> successor_idxs;

        [[nodiscard]] uint32_t block_span() const noexcept { return end_idx >= start_idx ? (end_idx - start_idx) : 0; }
        [[nodiscard]] bool has_successors() const noexcept { return !successor_idxs.empty(); }
    };

public:
    explicit DGTLProfileGuidedOptimization(
        const std::vector<ProfileData>* profile_ptr = nullptr) noexcept
        : profile_data_ptr(profile_ptr) {}

    void execute(dgtl::ir::IRStream& stream) override {
        if (stream.is_null() || stream.size() < 10) [[unlikely]] return;
        if (!profile_data_ptr || profile_data_ptr->empty()) [[unlikely]] return;

        stream.begin_transaction();

        std::vector<double> hotness = compute_hotness(stream, *profile_data_ptr);

        auto [hot_paths, cold_paths] = identify_paths(stream, hotness);

        bool modified = apply_pgo_optimizations(stream, hotness, hot_paths, cold_paths);

        if (modified && stream.validate_shadow_integrity()) {
            stream.commit_transaction();
        } else {
            stream.rollback_transaction();
        }
    }

    [[nodiscard]] const char* get_pass_name() const noexcept override {
        return "DGTLProfileGuidedOptimization";
    }

    [[nodiscard]] dgtl::pipeline::PassStage get_stage() const noexcept override {
        return dgtl::pipeline::PassStage::PostInlining;
    }

    [[nodiscard]] dgtl::pipeline::PassAffinity hot_path_affinity() const noexcept override {
        return dgtl::pipeline::PassAffinity::High;
    }

    [[nodiscard]] dgtl::pipeline::PassAffinity memory_affinity() const noexcept override {
        return dgtl::pipeline::PassAffinity::Medium;
    }

    [[nodiscard]] dgtl::pipeline::PassAffinity vector_affinity() const noexcept override {
        return dgtl::pipeline::PassAffinity::Low;
    }

    [[nodiscard]] dgtl::pipeline::PassAffinity speculation_affinity() const noexcept override {
        return dgtl::pipeline::PassAffinity::High;
    }

private:
    const std::vector<ProfileData>* profile_data_ptr;

    std::vector<double> compute_hotness(
        const dgtl::ir::IRStream& stream,
        const std::vector<ProfileData>& profile_data) {

        std::vector<double> hotness(stream.size(), 0.0);

        std::unordered_map<uint32_t, const ProfileData*> profile_map;
        uint64_t max_count = 1;

        for (const auto& prof : profile_data) {
            profile_map[prof.node_idx] = &prof;
            max_count = std::max(max_count, prof.execution_count);
        }

        for (uint32_t i = 0; i < static_cast<uint32_t>(stream.size()); ++i) {
            auto it = profile_map.find(i);
            if (it != profile_map.end()) {
                hotness[i] = static_cast<double>(it->second->execution_count) / max_count;
            }
        }

        return hotness;
    }

    std::pair<std::vector<uint32_t>, std::vector<uint32_t>> identify_paths(
        const dgtl::ir::IRStream& stream,
        const std::vector<double>& hotness) {

        std::vector<uint32_t> hot_paths;
        std::vector<uint32_t> cold_paths;

        const double HOT_THRESHOLD = 0.7;
        const double COLD_THRESHOLD = 0.2;

        for (uint32_t i = 0; i < static_cast<uint32_t>(stream.size()); ++i) {
            if (hotness[i] >= HOT_THRESHOLD) {
                hot_paths.push_back(i);
            } else if (hotness[i] <= COLD_THRESHOLD) {
                cold_paths.push_back(i);
            }
        }

        return {hot_paths, cold_paths};
    }

    bool apply_pgo_optimizations(
        dgtl::ir::IRStream& stream,
        const std::vector<double>& hotness,
        const std::vector<uint32_t>& hot_paths,
        const std::vector<uint32_t>& cold_paths) {

        uint32_t shadow_cursor = 0;
        bool modified = false;

        if (hotness.size() != stream.size()) {
            return false;
        }

        for (uint32_t i = 0; i < static_cast<uint32_t>(stream.size()); ++i) {
            const auto& node = stream.get_node_readonly(i);
            auto& s_atom = stream.get_shadow_atom(shadow_cursor);
            auto& s_core = stream.get_shadow_core(shadow_cursor);

            [[maybe_unused]] double node_hotness = hotness[i];

            s_atom.opcode = node.opcode;
            s_atom.flags = node.flags;

            if (std::binary_search(hot_paths.begin(), hot_paths.end(), i)) {
                s_atom.flags |= static_cast<decltype(s_atom.flags)>(
                    dgtl::ir::Flags::IS_SIMD_BURST
                );
            }

            if (std::binary_search(cold_paths.begin(), cold_paths.end(), i)) {
                s_atom.flags |= static_cast<decltype(s_atom.flags)>(
                    dgtl::ir::Flags::IS_SIMD_BURST
                );
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

} // namespace dgtl::ir::pass

export namespace dgtl::pass {
    using namespace dgtl::ir::pass;
}