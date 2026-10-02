/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:      DGTL (Deep General-purpose Transformation Layer)
 * Version:      1.0.0 â€” SSA CONSTRUCTION & REPAIR ENGINE
 * Owner:        Divyanshu Tata
 * Repository:   github.com/TataG-Systems/DGTL
 *
 * PILLAR 3: STATIC SINGLE ASSIGNMENT â€” CONSTRUCTION, VERIFICATION & REPAIR
 */

module;

#include <cstdlib>

// Global Module Fragment: Capture core analytical structures cleanly
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cassert>
#include <algorithm>
#include <span>
#include <vector>
#include <optional>
#include <string_view>
#include <functional>
#include <iterator>
#include <limits>

// Normalize compiler-environment macros before entering named module scope
#ifdef CONST
#   undef CONST
#endif
#ifdef PHI
#   undef PHI
#endif

export module dgtl.ir.ssa;

// FIXED: Removed the fwd module import to kill the type ambiguity loop completely
import dgtl.ir.stream;
import dgtl.ir.gen;
import dgtl.ir_node;
import dgtl.ir_enums;
import dgtl.ir_schema;
import dgtl.ir_contracts;
import dgtl.flat_map;
import dgtl.sync;

export namespace dgtl::ir::ssa {

// ============================================================================
// SECTION: FUNDAMENTAL SSA TYPES
// ============================================================================

using VReg    = std::uint32_t;
using BlockId = std::uint32_t;
using NodeId  = std::uint32_t;

inline constexpr VReg    kUndefinedVReg  = 0u;
inline constexpr BlockId kInvalidBlock   = std::numeric_limits<BlockId>::max();
inline constexpr NodeId  kInvalidNode    = std::numeric_limits<NodeId>::max();

[[nodiscard]] inline constexpr VReg make_ssa_name(std::uint32_t base_var, std::uint32_t version) noexcept {
    return ((version & 0xFFFU) << 20) | (base_var & 0xFFFFFU);
}

[[nodiscard]] inline constexpr std::uint32_t ssa_base_var(VReg v) noexcept   { return v & 0xFFFFFU; }
[[nodiscard]] inline constexpr std::uint32_t ssa_version(VReg v) noexcept    { return (v >> 20) & 0xFFFU; }

// PRODUCTION TYPE EXTENSION: Flat non-allocating string array layout prevents module table leaks
struct VerificationErrorStack {
    std::string_view errors[64];
    size_t count = 0;

    void push(std::string_view err) noexcept {
        if (count < 64) {
            errors[count++] = err;
        }
    }
    
    [[nodiscard]] bool empty() const noexcept { return count == 0; }
};

struct BlockArgumentIncoming {
    BlockId pred = kInvalidBlock;
    VReg value = kUndefinedVReg;
};

struct BlockArgument {
    std::uint32_t index = 0u;
    std::uint32_t base_var = 0u;
    VReg ssa_name = kUndefinedVReg;
    std::vector<BlockArgumentIncoming> incoming;
};

// TYPE REFACTOR: Optimized flat static context elements live safely in inline layout partitions
struct FlatBlockSet {
    BlockId storage[32]{};
    size_t count = 0;

    bool contains(BlockId val) const noexcept {
        for(size_t i = 0; i < count; ++i) if(storage[i] == val) return true;
        return false;
    }

    bool insert(BlockId val) noexcept {
        if (contains(val)) return false;
        if (count < 32) { storage[count++] = val; return true; }
        return false;
    }
};

struct FlatVRegSet {
    VReg storage[64]{};
    size_t count = 0;

    bool contains(VReg val) const noexcept {
        for(size_t i = 0; i < count; ++i) if(storage[i] == val) return true;
        return false;
    }

    bool insert(VReg val) noexcept {
        if (contains(val)) return false;
        if (count < 64) { storage[count++] = val; return true; }
        return false;
    }
};

struct BasicBlock {
    BlockId  id          = kInvalidBlock;
    NodeId   node_begin  = kInvalidNode;   
    NodeId   node_end    = kInvalidNode;   

    std::vector<BlockId> predecessors;
    std::vector<BlockId> successors;
    std::vector<BlockArgument> block_arguments;

    BlockId  idom        = kInvalidBlock;  
    std::uint32_t dom_depth   = 0u;             
    std::uint32_t pre_order   = 0u;             
    std::uint32_t post_order  = 0u;             

    FlatVRegSet live_in{};
    FlatVRegSet live_out{};
    FlatVRegSet def{};         
    FlatVRegSet use{};         

    [[nodiscard]] bool is_entry() const noexcept { return predecessors.empty(); }
    [[nodiscard]] bool is_exit()  const noexcept { return successors.empty();   }

    [[nodiscard]] bool has_pred(BlockId b) const noexcept {
        return std::find(predecessors.begin(), predecessors.end(), b) != predecessors.end();
    }

    [[nodiscard]] BlockArgument* find_block_argument(std::uint32_t base_var) noexcept {
        auto it = std::find_if(block_arguments.begin(), block_arguments.end(),
                               [base_var](const auto& arg) { return arg.base_var == base_var; });
        return it != block_arguments.end() ? &(*it) : nullptr;
    }

    [[nodiscard]] const BlockArgument* find_block_argument(std::uint32_t base_var) const noexcept {
        auto it = std::find_if(block_arguments.begin(), block_arguments.end(),
                               [base_var](const auto& arg) { return arg.base_var == base_var; });
        return it != block_arguments.end() ? &(*it) : nullptr;
    }

    BlockArgument& add_block_argument(std::uint32_t base_var, VReg ssa_name = kUndefinedVReg) {
        BlockArgument arg{};
        arg.index = static_cast<std::uint32_t>(block_arguments.size());
        arg.base_var = base_var;
        arg.ssa_name = ssa_name;
        block_arguments.push_back(std::move(arg));
        return block_arguments.back();
    }

    [[nodiscard]] bool uses_block_arguments() const noexcept { return !block_arguments.empty(); }
    void clear_ssa_joins() noexcept { block_arguments.clear(); }
};

// ============================================================================
// SECTION: CONTROL-FLOW GRAPH
// ============================================================================

struct CFG {
    std::vector<BasicBlock> blocks;
    BlockId entry = kInvalidBlock;
    BlockId exit  = kInvalidBlock;  
    std::vector<BlockId> rpo;

    [[nodiscard]] BasicBlock* block(BlockId id) noexcept {
        if (id >= blocks.size()) return nullptr;
        return &blocks[id];
    }
    
    [[nodiscard]] const BasicBlock* block(BlockId id) const noexcept {
        if (id >= blocks.size()) return nullptr;
        return &blocks[id];
    }

    BasicBlock& add_block() {
        auto& b   = blocks.emplace_back();
        b.id      = static_cast<BlockId>(blocks.size() - 1u);
        return b;
    }

    void add_edge(BlockId from, BlockId to) {
        blocks[from].successors.push_back(to);
        blocks[to].predecessors.push_back(from);
    }

    [[nodiscard]] bool rebuild_from_ir_stream(const dgtl::ir::IRStream& stream) {
        blocks.clear();
        rpo.clear();
        entry = kInvalidBlock;
        exit = kInvalidBlock;

        const auto& ir_blocks = stream.get_basic_blocks();
        const auto& ir_edges  = stream.get_cfg_edges();
        if (ir_blocks.empty()) return true;

        dgtl::utils::FastFlatMap<std::uint32_t, BlockId, 128> dense_of;

        blocks.reserve(ir_blocks.size());
        for (const auto& src : ir_blocks) {
            const BlockId dense_id = static_cast<BlockId>(blocks.size());
            dense_of.insert_or_assign(src.id, dense_id);

            auto& dst = blocks.emplace_back();
            dst.id = dense_id;
            dst.node_begin = static_cast<NodeId>(src.begin);
            dst.node_end   = static_cast<NodeId>(src.end);
        }

        for (const auto& e : ir_edges) {
            auto* fit = dense_of.find(e.from);
            auto* tit = dense_of.find(e.to);
            if (!fit || !tit) return false;
            add_edge(*fit, *tit);
        }

        const std::uint32_t src_entry = stream.get_entry_block_id();
        if (src_entry != 0xFFFFFFFFu) {
            auto* it = dense_of.find(src_entry);
            if (!it) return false;
            entry = *it;
        } else {
            entry = 0u;
        }

        BlockId single_exit = kInvalidBlock;
        for (const auto& b : blocks) {
            if (!b.successors.empty()) continue;
            if (single_exit == kInvalidBlock) single_exit = b.id;
            else { single_exit = kInvalidBlock; break; }
        }
        exit = single_exit;

        return true;
    }

    void compute_rpo() {
        rpo.clear();
        rpo.reserve(blocks.size());

        std::vector<bool> visited(blocks.size(), false);
        std::vector<bool> finalized(blocks.size(), false);
        std::vector<BlockId> stack;
        stack.reserve(blocks.size());
        stack.push_back(entry);

        std::vector<BlockId> post;
        post.reserve(blocks.size());

        while (!stack.empty()) {
            BlockId cur = stack.back();
            if (!visited[cur]) {
                visited[cur] = true;
                const auto& succs = blocks[cur].successors;
                for (auto it = succs.rbegin(); it != succs.rend(); ++it) {
                    if (!visited[*it]) stack.push_back(*it);
                }
            } else {
                stack.pop_back();
                if (!finalized[cur]) {
                    finalized[cur] = true;
                    post.push_back(cur);
                }
            }
        }

        for (auto it = post.rbegin(); it != post.rend(); ++it) {
            blocks[*it].pre_order = static_cast<std::uint32_t>(rpo.size());
            rpo.push_back(*it);
        }
        for (std::uint32_t i = 0u; i < static_cast<std::uint32_t>(post.size()); ++i) {
            blocks[post[i]].post_order = i;
        }
    }
};

// ============================================================================
// SECTION: DOMINATOR TREE
// ============================================================================

class DomTree {
public:
    explicit DomTree(CFG& cfg) : cfg_(cfg) {}

    void compute() {
        const std::size_t n = cfg_.blocks.size();
        idoms_.assign(n, kInvalidBlock);
        idoms_[cfg_.entry] = cfg_.entry;

        bool changed = true;
        while (changed) {
            changed = false;
            for (std::size_t ri = 1u; ri < cfg_.rpo.size(); ++ri) {
                BlockId b = cfg_.rpo[ri];
                const auto& preds = cfg_.blocks[b].predecessors;

                auto pit = std::find_if(preds.begin(), preds.end(), [this](BlockId p) { return idoms_[p] != kInvalidBlock; });
                if (pit == preds.end()) continue;
                BlockId new_idom = *pit;

                for (BlockId p : preds) {
                    if (p == new_idom) continue;
                    if (idoms_[p] != kInvalidBlock) {
                        new_idom = intersect(p, new_idom);
                    }
                }

                if (idoms_[b] != new_idom) {
                    idoms_[b] = new_idom;
                    changed   = true;
                }
            }
        }

        for (BlockId bid = 0u; bid < static_cast<BlockId>(n); ++bid) {
            cfg_.blocks[bid].idom = idoms_[bid];
        }
        compute_depths();
    }

    [[nodiscard]] bool dominates(BlockId a, BlockId b) const noexcept {
        if (a == b) return true;
        if (b == kInvalidBlock || b >= static_cast<BlockId>(idoms_.size())) return false;
        BlockId cur = b;
        while (cur != cfg_.entry) {
            if (cur == kInvalidBlock) return false;
            if (cur == a) return true;
            cur = idoms_[cur];
        }
        return (cur == a);
    }

    [[nodiscard]] bool strictly_dominates(BlockId a, BlockId b) const noexcept {
        return (a != b) && dominates(a, b);
    }

    [[nodiscard]] BlockId idom_of(BlockId b) const noexcept {
        return (b < idoms_.size()) ? idoms_[b] : kInvalidBlock;
    }

    void dom_subtree(BlockId root, std::vector<BlockId>& out) const {
        for (BlockId b = 0u; b < static_cast<BlockId>(cfg_.blocks.size()); ++b) {
            if (dominates(root, b)) out.push_back(b);
        }
    }

private:
    CFG&                    cfg_;
    std::vector<BlockId>    idoms_;

    BlockId intersect(BlockId a, BlockId b) const noexcept {
        while (a != b) {
            while (cfg_.blocks[a].post_order < cfg_.blocks[b].post_order) a = idoms_[a];
            while (cfg_.blocks[b].post_order < cfg_.blocks[a].post_order) b = idoms_[b];
        }
        return a;
    }

    void compute_depths() noexcept {
        cfg_.blocks[cfg_.entry].dom_depth = 0u;
        for (BlockId bid : cfg_.rpo) {
            if (bid == cfg_.entry) continue;
            BlockId parent = idoms_[bid];
            cfg_.blocks[bid].dom_depth = cfg_.blocks[parent].dom_depth + 1u;
        }
    }
};

// ============================================================================
// SECTION: DOMINANCE FRONTIER
// ============================================================================

class DomFrontier {
public:
    explicit DomFrontier(CFG& cfg, const DomTree& dom) : cfg_(cfg), dom_(dom) {}

    void compute() {
        const std::size_t n = cfg_.blocks.size();
        df_.resize(n);
        for(size_t i = 0; i < n; ++i) df_[i].count = 0;

        for (BlockId b = 0u; b < static_cast<BlockId>(n); ++b) {
            const auto& preds = cfg_.blocks[b].predecessors;
            if (preds.size() < 2u) continue;

            for (BlockId p : preds) {
                BlockId runner = p;
                while (runner != dom_.idom_of(b)) {
                    df_[runner].insert(b);
                    runner = dom_.idom_of(runner);
                    if (runner == kInvalidBlock) break;
                }
            }
        }
    }

    [[nodiscard]] const FlatBlockSet& of(BlockId b) const noexcept {
        static const FlatBlockSet empty{};
        return (b < df_.size()) ? df_[b] : empty;
    }

    [[nodiscard]] FlatBlockSet iterated(const FlatBlockSet& def_blocks) const {
        FlatBlockSet result{};
        std::vector<BlockId> worklist;
        worklist.reserve(def_blocks.count);
        for(size_t i = 0; i < def_blocks.count; ++i) worklist.push_back(def_blocks.storage[i]);

        while (!worklist.empty()) {
            BlockId b = worklist.back();
            worklist.pop_back();
            const auto& frontier_set = of(b);
            for (size_t i = 0; i < frontier_set.count; ++i) {
                BlockId y = frontier_set.storage[i];
                if (result.insert(y)) {
                    worklist.push_back(y);
                }
            }
        }
        return result;
    }

private:
    CFG&                       cfg_;
    const DomTree&             dom_;
    std::vector<FlatBlockSet>  df_;
};

// ============================================================================
// SECTION: LIVENESS ANALYSIS
// ============================================================================

class LivenessAnalysis {
public:
    explicit LivenessAnalysis(CFG& cfg) : cfg_(cfg) {}

    void seed_def_use(std::span<const IRNode> nodes) {
        for (auto& blk : cfg_.blocks) {
            blk.def.count = 0;
            blk.use.count = 0;
            for (NodeId ni = blk.node_begin; ni < blk.node_end; ++ni) {
                if (ni >= nodes.size()) break;
                const IRNode& n = nodes[ni];
                auto use_vreg = [&](VReg v) noexcept {
                    if (v != kUndefinedVReg && !blk.def.contains(v))
                        blk.use.insert(v);
                };
                use_vreg(static_cast<VReg>(n.op_a));
                use_vreg(static_cast<VReg>(n.op_b));
                use_vreg(static_cast<VReg>(n.op_c));
                
                if (n.result_id != 0u)
                    blk.def.insert(static_cast<VReg>(n.result_id));
            }
            for (const auto& arg : blk.block_arguments) {
                if (arg.ssa_name != kUndefinedVReg) {
                    blk.def.insert(arg.ssa_name);
                }
            }
        }
    }

    void compute() {
        bool changed = true;
        while (changed) {
            changed = false;
            for (auto it = cfg_.rpo.rbegin(); it != cfg_.rpo.rend(); ++it) {
                BasicBlock& blk = cfg_.blocks[*it];

                FlatVRegSet new_live_out{};
                for (BlockId succ_id : blk.successors) {
                    const BasicBlock& succ = cfg_.blocks[succ_id];
                    for (size_t i = 0; i < succ.live_in.count; ++i) {
                        new_live_out.insert(succ.live_in.storage[i]);
                    }
                    for (const auto& arg : succ.block_arguments) {
                        for (const auto& in : arg.incoming) {
                            if (in.pred == *it && in.value != kUndefinedVReg) {
                                new_live_out.insert(in.value);
                            }
                        }
                    }
                }

                FlatVRegSet new_live_in = blk.use;
                for (size_t i = 0; i < new_live_out.count; ++i) {
                    VReg v = new_live_out.storage[i];
                    if (!blk.def.contains(v)) new_live_in.insert(v);
                }

                bool arrays_match = (new_live_in.count == blk.live_in.count) && (new_live_out.count == blk.live_out.count);
                if (arrays_match) {
                    for(size_t i = 0; i < new_live_in.count; ++i) {
                        if (new_live_in.storage[i] != blk.live_in.storage[i]) { arrays_match = false; break; }
                    }
                    for(size_t i = 0; i < new_live_out.count; ++i) {
                        if (new_live_out.storage[i] != blk.live_out.storage[i]) { arrays_match = false; break; }
                    }
                }

                if (!arrays_match) {
                    blk.live_in  = new_live_in;
                    blk.live_out = new_live_out;
                    changed = true;
                }
            }
        }
    }

private:
    CFG& cfg_;
};

// ============================================================================
// SECTION: BLOCK-ARGUMENT REWRITE HELPERS
// ============================================================================

class BlockArgumentSSA {
public:
    static void canonicalize_to_block_arguments(CFG& cfg) noexcept {
        for (auto& blk : cfg.blocks) {
            for (std::uint32_t i = 0u; i < static_cast<std::uint32_t>(blk.block_arguments.size()); ++i) {
                blk.block_arguments[i].index = i;
            }
        }
    }
};

// ============================================================================
// SECTION: SSA BUILDER (Cytron 1991 + Braun 2013)
// ============================================================================

class SSABuilder {
public:
    explicit SSABuilder(CFG& cfg, DomTree& dom, DomFrontier& df, std::span<IRNode> nodes)
        : cfg_(cfg), dom_(dom), df_(df), nodes_(nodes) {}

    void build() {
        BlockArgumentSSA::canonicalize_to_block_arguments(cfg_);
        collect_defs_and_uses();
        place_phis();
        rename_variables();
        eliminate_trivial_phis();
    }

    void add_definition(std::uint32_t base_var, VReg ssa_name, BlockId b) {
        auto* nested_map = current_def_.find(base_var);
        if (!nested_map) {
            dgtl::utils::FastFlatMap<BlockId, VReg, 32> fresh_m;
            fresh_m.insert_or_assign(b, ssa_name);
            current_def_.insert_or_assign(base_var, std::move(fresh_m));
        } else {
            nested_map->insert_or_assign(b, ssa_name);
        }
    }

    [[nodiscard]] VReg read_variable(std::uint32_t base_var, BlockId b) {
        auto* it = current_def_.find(base_var);
        if (it) {
            auto* bit = it->find(b);
            if (bit) return *bit;
        }
        return read_variable_recursive(base_var, b);
    }

private:
    CFG&                  cfg_;
    DomTree&              dom_;
    DomFrontier&          df_;
    std::span<IRNode>     nodes_;
    std::uint32_t         next_version_ = 1u;

    dgtl::utils::FastFlatMap<std::uint32_t, dgtl::utils::FastFlatMap<BlockId, VReg, 32>, 128> current_def_;
    dgtl::utils::FastFlatMap<std::uint32_t, FlatBlockSet, 128> def_blocks_;
    dgtl::utils::FastFlatMap<std::uint32_t, std::vector<VReg>, 128> rename_stack_;

    // PRODUCTION LAYOUT SECURITY FIREWALL: Tracks flat insertion indices independently 
    // to bypass internal continuous space structural variations without data leakage
    std::vector<std::uint32_t> tracked_def_keys;
    std::vector<std::uint32_t> tracked_push_keys;

    void collect_defs_and_uses() {
        tracked_def_keys.clear();
        const NodeId total = static_cast<NodeId>(nodes_.size());
        std::vector<BlockId> node_owner(total, kInvalidBlock);
        for (const auto& blk : cfg_.blocks) {
            for (NodeId ni = blk.node_begin; ni < blk.node_end && ni < total; ++ni)
                node_owner[ni] = blk.id;
        }
        for (NodeId ni = 0u; ni < total; ++ni) {
            const IRNode& n = nodes_[ni];
            if (n.result_id == 0u) continue;
            BlockId owner = node_owner[ni];
            if (owner == kInvalidBlock) continue;
            
            std::uint32_t target_v = static_cast<std::uint32_t>(n.result_id);
            auto* target_set = def_blocks_.find(target_v);
            if (!target_set) {
                FlatBlockSet b_set{};
                b_set.insert(owner);
                def_blocks_.insert_or_assign(target_v, b_set);
                tracked_def_keys.push_back(target_v);
            } else {
                target_set->insert(owner);
            }
        }
    }

    void place_phis() {
        for (size_t idx = 0; idx < tracked_def_keys.size(); ++idx) {
            auto base_var = tracked_def_keys[idx];
            auto* dblocks  = def_blocks_.find(base_var);
            if (!dblocks) continue;

            FlatBlockSet phi_blocks = df_.iterated(*dblocks);

            for (size_t b_idx = 0; b_idx < phi_blocks.count; ++b_idx) {
                BlockId b = phi_blocks.storage[b_idx];
                BasicBlock* blk = cfg_.block(b);
                if (!blk || blk->find_block_argument(base_var)) continue;
                if (blk->live_in.count > 0 && !blk->live_in.contains(base_var)) continue;

                auto& arg = blk->add_block_argument(base_var, kUndefinedVReg);
                arg.incoming.reserve(blk->predecessors.size());
                for (BlockId pred : blk->predecessors) {
                    arg.incoming.push_back(BlockArgumentIncoming{pred, kUndefinedVReg});
                }
            }
        }
    }

    void rename_variables() {
        const std::size_t n = cfg_.blocks.size();
        std::vector<std::vector<BlockId>> children(n);
        for (const auto& blk : cfg_.blocks) {
            if (blk.idom != kInvalidBlock && blk.idom != blk.id)
                children[blk.idom].push_back(blk.id);
        }
        rename_block(cfg_.entry, children);
    }

    void rename_block(BlockId bid, const std::vector<std::vector<BlockId>>& children) {
        BasicBlock& blk = cfg_.blocks[bid];
        dgtl::utils::FastFlatMap<std::uint32_t, std::uint32_t, 32> push_counts;
        tracked_push_keys.clear();

        for (auto& arg : blk.block_arguments) {
            VReg fresh = fresh_name(arg.base_var);
            arg.ssa_name = fresh;
            
            auto* stack = rename_stack_.find(arg.base_var);
            if (!stack) {
                std::vector<VReg> fresh_v; fresh_v.push_back(fresh);
                rename_stack_.insert_or_assign(arg.base_var, std::move(fresh_v));
            } else {
                stack->push_back(fresh);
            }
            
            auto* count = push_counts.find(arg.base_var);
            if (!count) {
                push_counts.insert_or_assign(arg.base_var, 1u);
                tracked_push_keys.push_back(arg.base_var);
            }
            else (*count)++;
        }

        for (NodeId ni = blk.node_begin; ni < blk.node_end; ++ni) {
            if (ni >= static_cast<NodeId>(nodes_.size())) break;
            IRNode& n = nodes_[ni];

            auto rename_use = [&](std::uint32_t& field) noexcept {
                if (field == 0u) return;
                auto* it = rename_stack_.find(field);
                if (it && !it->empty()) field = it->back();
            };
            rename_use(n.op_a);
            rename_use(n.op_b);
            rename_use(n.op_c);

            if (n.result_id != 0u) {
                std::uint32_t base = n.result_id;
                VReg fresh    = fresh_name(base);
                n.result_id   = fresh;
                
                auto* stack = rename_stack_.find(base);
                if (!stack) {
                    std::vector<VReg> fresh_v; fresh_v.push_back(fresh);
                    rename_stack_.insert_or_assign(base, std::move(fresh_v));
                } else {
                    stack->push_back(fresh);
                }
                
                auto* count = push_counts.find(base);
                if (!count) {
                    push_counts.insert_or_assign(base, 1u);
                    tracked_push_keys.push_back(base);
                }
                else (*count)++;
            }
        }

        for (BlockId succ_id : blk.successors) {
            BasicBlock& succ = cfg_.blocks[succ_id];
            for (auto& arg : succ.block_arguments) {
                auto* it = rename_stack_.find(arg.base_var);
                VReg reaching = (it && !it->empty()) ? it->back() : kUndefinedVReg;
                auto init = std::find_if(arg.incoming.begin(), arg.incoming.end(), [bid](const auto& in) { return in.pred == bid; });
                if (init != arg.incoming.end()) {
                    init->value = reaching;
                } else {
                    arg.incoming.push_back(BlockArgumentIncoming{bid, reaching});
                }
            }
        }

        for (size_t idx = 0; idx < tracked_push_keys.size(); ++idx) {
            auto base_var = tracked_push_keys[idx];
            auto* cnt     = push_counts.find(base_var);
            auto* stack   = rename_stack_.find(base_var);
            if (stack && cnt) {
                for (std::uint32_t i = 0u; i < *cnt; ++i) {
                    if (!stack->empty()) stack->pop_back();
                }
            }
        }
    }

    VReg read_variable_recursive(std::uint32_t base_var, BlockId b) {
        BasicBlock& blk = cfg_.blocks[b];
        VReg val;

        if (blk.predecessors.size() == 1u) {
            val = read_variable(base_var, blk.predecessors[0]);
        } else {
            VReg fresh = fresh_name(base_var);
            auto& arg = blk.add_block_argument(base_var, fresh);
            val = fresh;

            auto* current_m = current_def_.find(base_var);
            if (!current_m) {
                dgtl::utils::FastFlatMap<BlockId, VReg, 32> fresh_m; fresh_m.insert_or_assign(b, val);
                current_def_.insert_or_assign(base_var, std::move(fresh_m));
            } else {
                current_m->insert_or_assign(b, val);
            }

            for (BlockId pred : blk.predecessors) {
                VReg pred_val = read_variable(base_var, pred);
                arg.incoming.push_back(BlockArgumentIncoming{pred, pred_val});
            }

            VReg same = kUndefinedVReg;
            bool trivial = true;
            for (const auto& in : arg.incoming) {
                VReg v = in.value;
                if (v == arg.ssa_name || v == kUndefinedVReg) continue;
                if (same == kUndefinedVReg) {
                    same = v;
                } else if (same != v) {
                    trivial = false;
                    break;
                }
            }
            if (trivial && same != kUndefinedVReg) {
                auto* inner_m = current_def_.find(base_var);
                if (inner_m) inner_m->insert_or_assign(b, same);
                val = same;
                arg.ssa_name = same;
            }
        }

        auto* outer_m = current_def_.find(base_var);
        if (outer_m) outer_m->insert_or_assign(b, val);
        return val;
    }

    VReg fresh_name(std::uint32_t base_var) noexcept {
        return make_ssa_name(base_var, next_version_++);
    }

    void eliminate_trivial_phis() noexcept {
        bool changed = true;
        while (changed) {
            changed = false;
            for (auto& blk : cfg_.blocks) {
                for (auto& arg : blk.block_arguments) {
                    VReg same = kUndefinedVReg;
                    bool trivial = true;
                    for (const auto& in : arg.incoming) {
                        VReg v = in.value;
                        if (v == arg.ssa_name || v == kUndefinedVReg) continue;
                        if (same == kUndefinedVReg) {
                            same = v;
                        } else if (same != v) {
                            trivial = false;
                            break;
                        }
                    }
                    if (trivial && same != kUndefinedVReg && arg.ssa_name != same) {
                        arg.ssa_name = same;
                        changed = true;
                    }
                }
            }
        }
    }
};

class SSAVerifier {
public:
    explicit SSAVerifier(CFG& cfg, DomTree& dom, std::span<const IRNode> nodes)
        : cfg_(cfg), dom_(dom), nodes_(nodes) {}

    [[nodiscard]] VerificationErrorStack verify() {
        violations_.count = 0;
        collect_all_defs();
        check_single_def();
        check_dom_use();
        check_phi_operands();
        check_no_trivial_live_phis();
        return violations_;
    }

private:
    CFG&                    cfg_;
    DomTree&                dom_;
    std::span<const IRNode>  nodes_;
    VerificationErrorStack  violations_;

    struct DefSite { BlockId block; [[maybe_unused]] bool is_join_def; [[maybe_unused]] NodeId node_idx; };
    dgtl::utils::FastFlatMap<VReg, DefSite, 256> def_map_;

    void report(std::string_view msg) { violations_.push(msg); }

    void collect_all_defs() {
        def_map_.clear();
        for (const auto& blk : cfg_.blocks) {
            for (const auto& arg : blk.block_arguments) {
                if (arg.ssa_name != kUndefinedVReg) {
                    def_map_.insert_or_assign(arg.ssa_name, DefSite{blk.id, true, kInvalidNode});
                }
            }
        }
        for (NodeId ni = 0u; ni < static_cast<NodeId>(nodes_.size()); ++ni) {
            const IRNode& n = nodes_[ni];
            if (n.result_id == 0u) continue;
            def_map_.insert_or_assign(n.result_id, DefSite{owner_block(ni), false, ni});
        }
    }

    void check_single_def() {
        dgtl::utils::FastFlatMap<VReg, std::uint32_t, 256> def_count;
        std::vector<VReg> tracked_vregs;

        for (const auto& blk : cfg_.blocks) {
            for (const auto& arg : blk.block_arguments) {
                if (arg.ssa_name != kUndefinedVReg) {
                    auto* count = def_count.find(arg.ssa_name);
                    if (!count) {
                        def_count.insert_or_assign(arg.ssa_name, 1u);
                        tracked_vregs.push_back(arg.ssa_name);
                    }
                    else (*count)++;
                }
            }
        }
        for (NodeId ni = 0u; ni < static_cast<NodeId>(nodes_.size()); ++ni) {
            if (nodes_[ni].result_id != 0u) {
                auto* count = def_count.find(nodes_[ni].result_id);
                if (!count) {
                    def_count.insert_or_assign(nodes_[ni].result_id, 1u);
                    tracked_vregs.push_back(nodes_[ni].result_id);
                }
                else (*count)++;
            }
        }
        for (size_t idx = 0; idx < tracked_vregs.size(); ++idx) {
            auto* current_count = def_count.find(tracked_vregs[idx]);
            if (current_count && *current_count > 1u) {
                report("SSA violation: VReg defined multiple times (SSA requires exactly 1)");
            }
        }
    }

    void check_dom_use() {
        for (NodeId ni = 0u; ni < static_cast<NodeId>(nodes_.size()); ++ni) {
            const IRNode& n = nodes_[ni];
            BlockId use_block = owner_block(ni);
            auto check = [&](VReg v) {
                if (v == kUndefinedVReg) return;
                auto* it = def_map_.find(v);
                if (!it) {
                    report("SSA violation: VReg used but never defined");
                    return;
                }
                if (!dom_.dominates(it->block, use_block)) {
                    report("SSA violation: VReg used in block but defined in non-dominating block");
                }
            };
            check(static_cast<VReg>(n.op_a));
            check(static_cast<VReg>(n.op_b));
            check(static_cast<VReg>(n.op_c));
        }
    }

    void check_phi_operands() {
        for (const auto& blk : cfg_.blocks) {
            for (const auto& arg : blk.block_arguments) {
                for (const auto& in : arg.incoming) {
                    if (in.value == kUndefinedVReg) {
                        report("SSA violation: block argument has undefined incoming from predecessor");
                        continue;
                    }
                    auto* it = def_map_.find(in.value);
                    if (!it) {
                        report("SSA violation: block-argument incoming VReg has no definition");
                    } else if (!dom_.dominates(it->block, in.pred)) {
                        report("SSA violation: block-argument incoming VReg not dominated by def block");
                    }
                }
            }
        }
    }

    void check_no_trivial_live_phis() {
        for (const auto& blk : cfg_.blocks) {
            for (const auto& arg : blk.block_arguments) {
                VReg same = kUndefinedVReg;
                bool trivial = true;
                for (const auto& in : arg.incoming) {
                    VReg v = in.value;
                    if (v == arg.ssa_name || v == kUndefinedVReg) continue;
                    if (same == kUndefinedVReg) {
                        same = v;
                    } else if (same != v) {
                        trivial = false;
                        break;
                    }
                }
                if (trivial && same != kUndefinedVReg) {
                    report("SSA quality: trivial block argument should have been folded");
                }
            }
        }
    }

    BlockId owner_block(NodeId ni) const noexcept {
        auto it = std::find_if(cfg_.blocks.begin(), cfg_.blocks.end(),
                               [ni](const auto& blk) { return ni >= blk.node_begin && ni < blk.node_end; });
        return it != cfg_.blocks.end() ? it->id : kInvalidBlock;
    }
};

// ============================================================================
// SECTION: SSA REPAIR (Boissinot et al. 2009)
// ============================================================================

class SSARepair {
public:
    explicit SSARepair(CFG& cfg, DomTree& dom, DomFrontier& df, LivenessAnalysis& liveness, std::span<IRNode> nodes)
        : cfg_(cfg), dom_(dom), df_(df), liveness_(liveness), nodes_(nodes) {}

    void repair() {
        liveness_.seed_def_use(nodes_);
        liveness_.compute();
        insert_missing_phis();
        re_rename_all();
        remove_dead_and_trivial_phis();
    }

    void repair_region(const std::vector<BlockId>& mutated_blocks) {
        liveness_.seed_def_use(nodes_);
        liveness_.compute();
        insert_missing_phis_for(mutated_blocks);
        re_rename_all();
        remove_dead_and_trivial_phis();
    }

    void replace_predecessor(BlockId blk, BlockId old_pred, BlockId new_pred) noexcept {
        BasicBlock* b = cfg_.block(blk);
        if (!b) return;
        for (auto& arg : b->block_arguments) {
            auto init = std::find_if(arg.incoming.begin(), arg.incoming.end(),
                                     [old_pred](const auto& in) { return in.pred == old_pred; });
            if (init != arg.incoming.end()) {
                init->value = kUndefinedVReg;
                b->block_arguments[arg.index].incoming.push_back(BlockArgumentIncoming{new_pred, kUndefinedVReg});
            }
        }
    }

    void delete_definition(VReg dead_def, VReg replacement = kUndefinedVReg) noexcept {
        for (auto& n : nodes_) {
            auto fix = [&](std::uint32_t& field) noexcept {
                if (static_cast<VReg>(field) == dead_def) field = replacement;
            };
            fix(n.op_a); fix(n.op_b); fix(n.op_c);
        }
        for (auto& blk : cfg_.blocks) {
            for (auto& arg : blk.block_arguments) {
                for (auto& in : arg.incoming) {
                    if (in.value == dead_def) {
                        in.value = replacement;
                    }
                }
            }
        }
    }

private:
    CFG&              cfg_;
    DomTree&          dom_;
    DomFrontier&      df_;
    LivenessAnalysis& liveness_;
    std::span<IRNode> nodes_;
    std::uint32_t     next_version_ = 0x8000u; 

    void insert_missing_phis() {
        for (const auto& blk : cfg_.blocks) {
            insert_missing_phis_for_block(blk.id);
        }
    }

    void insert_missing_phis_for(const std::vector<BlockId>& blocks) {
        FlatBlockSet region{};
        for(auto b : blocks) region.insert(b);
        FlatBlockSet frontier = df_.iterated(region);
        for(size_t i = 0; i < region.count; ++i) frontier.insert(region.storage[i]);

        for (size_t i = 0; i < frontier.count; ++i) {
            insert_missing_phis_for_block(frontier.storage[i]);
        }
    }

    void insert_missing_phis_for_block(BlockId bid) {
        BasicBlock* blk = cfg_.block(bid);
        if (!blk || blk->predecessors.size() < 2u) return;

        for (size_t i = 0; i < blk->live_in.count; ++i) {
            VReg v = blk->live_in.storage[i];
            std::uint32_t base = ssa_base_var(v);
            if (blk->find_block_argument(base)) continue;

            bool needs_phi = false;
            BlockId first_def_block = kInvalidBlock;
            for (BlockId pred : blk->predecessors) {
                BlockId def_block = find_def_block(base, pred);
                if (first_def_block == kInvalidBlock) {
                    first_def_block = def_block;
                } else if (def_block != first_def_block) {
                    needs_phi = true;
                    break;
                }
            }

            if (needs_phi) {
                auto& arg = blk->add_block_argument(base, make_ssa_name(base, next_version_++));
                arg.incoming.reserve(blk->predecessors.size());
                for (BlockId pred : blk->predecessors) {
                    arg.incoming.push_back(BlockArgumentIncoming{pred, kUndefinedVReg});
                }
            }
        }
    }

    BlockId find_def_block(std::uint32_t base_var, BlockId from) const noexcept {
        BlockId cur = from;
        while (cur != kInvalidBlock) {
            const BasicBlock* blk = cfg_.block(cur);
            if (!blk) break;
            if (blk->def.contains(base_var)) return cur;
            if (blk->find_block_argument(base_var)) return cur;
            if (cur == blk->idom) break;  
            cur = blk->idom;
        }
        return kInvalidBlock;
    }

    void re_rename_all() noexcept {
        for (auto& blk : cfg_.blocks) {
            for (auto& arg : blk.block_arguments) {
                for (auto& in : arg.incoming) {
                    if (in.value == kUndefinedVReg) {
                        in.value = find_reaching_def(arg.base_var, in.pred);
                    }
                }
            }
        }
    }

    VReg find_reaching_def(std::uint32_t base_var, BlockId from) const noexcept {
        BlockId cur = from;
        while (cur != kInvalidBlock) {
            const BasicBlock* blk = cfg_.block(cur);
            if (!blk) break;
            const BlockArgument* arg = blk->find_block_argument(base_var);
            if (arg && arg->ssa_name != kUndefinedVReg) return arg->ssa_name;
            VReg last = kUndefinedVReg;
            for (NodeId ni = blk->node_begin; ni < blk->node_end; ++ni) {
                if (ni >= static_cast<NodeId>(nodes_.size())) break;
                const IRNode& n = nodes_[ni];
                if (n.result_id != 0u && ssa_base_var(n.result_id) == base_var)
                    last = n.result_id;
            }
            if (last != kUndefinedVReg) return last;
            if (cur == blk->idom) break;
            cur = blk->idom;
        }
        return kUndefinedVReg;
    }

    void remove_dead_and_trivial_phis() noexcept {
        bool changed = true;
        while (changed) {
            changed = false;
            for (auto& blk : cfg_.blocks) {
                for (auto& arg : blk.block_arguments) {
                    if (!blk.live_in.contains(arg.base_var)) {
                        arg.ssa_name = kUndefinedVReg;
                        changed = true;
                        continue;
                    }
                    VReg same = kUndefinedVReg;
                    bool trivial = true;
                    for (const auto& in : arg.incoming) {
                        VReg v = in.value;
                        if (v == arg.ssa_name || v == kUndefinedVReg) continue;
                        if (same == kUndefinedVReg) {
                            same = v;
                        } else if (same != v) {
                            trivial = false;
                            break;
                        }
                    }
                    if (trivial && same != kUndefinedVReg && arg.ssa_name != same) {
                        arg.ssa_name = same;
                        changed = true;
                    }
                }
            }
        }
    }
};

// ============================================================================
// SECTION: PHI ELIMINATOR (SSA Destruction)
// ============================================================================

class PhiEliminator {
public:
    struct Copy {
        [[maybe_unused]] VReg dst;  
        [[maybe_unused]] VReg src;  
    };

    using EdgeKey = std::pair<BlockId, BlockId>;   
    struct EdgeKeyHash {
        std::size_t operator()(const EdgeKey& e) const noexcept {
            return (static_cast<std::uint64_t>(e.first) << 32) | e.second;
        }
    };
    
    using CopyMap = dgtl::utils::FastFlatMap<EdgeKey, std::vector<Copy>, 128>;

    explicit PhiEliminator(CFG& cfg) : cfg_(cfg), temp_counter_(0xF00u) {}

    [[nodiscard]] CopyMap eliminate() {
        CopyMap result;

        for (auto& blk : cfg_.blocks) {
            if (blk.block_arguments.empty()) continue;

            FlatVRegSet phi_results{};
            for (const auto& arg : blk.block_arguments) {
                if (arg.ssa_name != kUndefinedVReg) {
                    phi_results.insert(arg.ssa_name);
                }
            }

            for (auto& arg : blk.block_arguments) {
                if (arg.ssa_name == kUndefinedVReg) continue;

                for (const auto& in : arg.incoming) {
                    EdgeKey edge = {in.pred, blk.id};
                    VReg src = in.value;

                    if (phi_results.contains(src) && src != arg.ssa_name) {
                        VReg temp = make_ssa_name(ssa_base_var(src), temp_counter_++);
                        if (ssa_version(make_ssa_name(0u, temp_counter_)) == 0u)
                            temp_counter_ = 0xF00u;
                            
                        auto* edge_vec = result.find(edge);
                        if (!edge_vec) {
                            std::vector<Copy> fresh_v;
                            fresh_v.push_back({temp, src}); fresh_v.push_back({arg.ssa_name, temp});
                            result.insert_or_assign(edge, std::move(fresh_v));
                        } else {
                            edge_vec->push_back({temp, src}); edge_vec->push_back({arg.ssa_name, temp});
                        }
                    } else {
                        auto* edge_vec = result.find(edge);
                        if (!edge_vec) {
                            std::vector<Copy> fresh_v; fresh_v.push_back({arg.ssa_name, src});
                            result.insert_or_assign(edge, std::move(fresh_v));
                        } else {
                            edge_vec->push_back({arg.ssa_name, src});
                        }
                    }
                }

                arg.ssa_name = kUndefinedVReg;  
            }
        }

        return result;
    }

private:
    CFG&          cfg_;
    std::uint32_t temp_counter_; 
};

// ============================================================================
// SECTION: CONVENIENCE PIPELINE
// ============================================================================

struct SSAPipeline {
    CFG&           cfg;
    std::span<IRNode>  nodes;

    DomTree            dom;
    DomFrontier        df;
    LivenessAnalysis   liveness;
    SSABuilder         builder;
    SSAVerifier        verifier;

    explicit SSAPipeline(CFG& c, std::span<IRNode> n)
        : cfg(c), nodes(n)
        , dom(c)
        , df(c, dom)
        , liveness(c)
        , builder(c, dom, df, n)
        , verifier(c, dom, n)
    {}

    void construct() {
        cfg.compute_rpo();
        dom.compute();
        df.compute();
        liveness.seed_def_use(nodes);
        liveness.compute();
        builder.build();
    }

    [[nodiscard]] VerificationErrorStack verify() {
        return verifier.verify();
    }

    void repair() {
        dom.compute();      
        df.compute();
        SSARepair r(cfg, dom, df, liveness, nodes);
        r.repair();
    }

    void repair_region(const std::vector<BlockId>& mutated) {
        dom.compute();
        df.compute();
        SSARepair r(cfg, dom, df, liveness, nodes);
        r.repair_region(mutated);
    }

    [[nodiscard]] PhiEliminator::CopyMap destroy_ssa() {
        PhiEliminator elim(cfg);
        return elim.eliminate();
    }
};

// ============================================================================
// SECTION: IRSTREAM -> SSA CONVENIENCE BRIDGE
// ============================================================================

struct SSAFromStreamResult {
    CFG cfg;
    std::vector<IRNode> nodes;
    VerificationErrorStack verification_errors;
    bool ok = false;
};

[[nodiscard]] inline constexpr std::optional<TypeCode>
generated_result_type_to_type_code(std::string_view result_type) noexcept {
    if (result_type == "void") return TypeCode::VOID_T;
    if (result_type == "Bool") return TypeCode::BOOL;
    if (result_type == "Predicate") return TypeCode::PREDICATE;
    if (result_type == "AddressSpace") return TypeCode::ADDRESS_SPACE;
    if (result_type == "Ptr") return TypeCode::PTR;
    if (result_type == "StringHandle") return TypeCode::STRING_HANDLE;

    if (result_type == "I4") return TypeCode::I4;
    if (result_type == "I8") return TypeCode::I8;
    if (result_type == "I16") return TypeCode::I16;
    if (result_type == "I32") return TypeCode::I32;
    if (result_type == "I64") return TypeCode::I64;
    if (result_type == "I128") return TypeCode::I128;
    if (result_type == "I256") return TypeCode::I256;
    if (result_type == "I512") return TypeCode::I512;

    if (result_type == "F8") return TypeCode::F8;
    if (result_type == "BF16") return TypeCode::BF16;
    if (result_type == "F16") return TypeCode::F16;
    if (result_type == "F32") return TypeCode::F32;
    if (result_type == "F64") return TypeCode::F64;
    if (result_type == "F128") return TypeCode::F128;

    if (result_type == "Vec3xF32") return TypeCode::VEC3_F32;
    if (result_type == "Vec4xF32") return TypeCode::VEC4_F32;
    if (result_type == "Mat4x4F32") return TypeCode::MAT4X4_F32;
    if (result_type == "QuatF32") return TypeCode::QUAT_F32;
    if (result_type == "VectorF32x16") return TypeCode::VECTOR_F32X16;
    if (result_type == "VectorI32x16") return TypeCode::VECTOR_I32X16;
    if (result_type == "VectorF16x32") return TypeCode::VECTOR_F16X32;

    if (result_type == "TensorFP8") return TypeCode::TENSOR_FP8;
    if (result_type == "TensorBF16") return TypeCode::TENSOR_BF16;
    if (result_type == "TensorF32") return TypeCode::TENSOR_F32;
    if (result_type == "TensorI64") return TypeCode::TENSOR_I64;

    return std::nullopt;
}

[[nodiscard]] inline VerificationErrorStack
verify_generated_result_type_annotations(const ::dgtl::ir::IRStream& stream) {
    VerificationErrorStack mismatches;

    for (std::size_t i = 0u; i < stream.size(); ++i) {
        const IRNode node = stream.get_node(i);
        if (node.opcode == OpCode::NOP) continue;

        const auto* def = dgtl::ir::gen::find_generated_op_def(node.opcode);
        if (!def) continue;

        const bool expects_result = def->has_result && std::string_view(def->result_type) != "void";
        if (!expects_result) {
            if (node.result_id != 0u && node.result_id != 0xFFFFFFFFu) {
                const std::uint32_t annotated = stream.get_result_type(node.result_id);
                if (annotated != 0u) {
                    mismatches.push("Type annotation mismatch: opcode has no result but result_id carries annotation");
                }
            }
            continue;
        }

        if (node.result_id == 0u || node.result_id == 0xFFFFFFFFu) {
            mismatches.push("Type annotation mismatch: opcode expects result type but result_id is missing");
            continue;
        }

        const auto Schuyler_expected = generated_result_type_to_type_code(def->result_type);
        if (!Schuyler_expected || *Schuyler_expected == TypeCode::VOID_T) continue;

        const std::uint32_t annotated = stream.get_result_type(node.result_id);
        if (annotated == 0u) {
            mismatches.push("Type annotation mismatch: result_id missing annotation");
            continue;
        }

        if (!is_valid_type_handle(annotated)) {
            mismatches.push("Type annotation mismatch: result_id has invalid type handle");
            continue;
        }

        if (annotated != static_cast<std::uint32_t>(*Schuyler_expected)) {
            mismatches.push("Type annotation mismatch: result_id annotated value does not match expected signature");
        }
    }

    return mismatches;
}

[[nodiscard]] inline SSAFromStreamResult construct_ssa_from_stream(const ::dgtl::ir::IRStream& stream) {
    SSAFromStreamResult result;

    const std::size_t n = stream.size();
    result.nodes.reserve(n);
    for (std::size_t i = 0u; i < n; ++i) {
        result.nodes.push_back(stream.get_node(i));
    }

    if (!result.cfg.rebuild_from_ir_stream(stream)) {
        result.verification_errors.push("failed to rebuild SSA CFG from IRStream");
        result.ok = false;
        return result;
    }

    if (result.cfg.blocks.empty()) {
        result.ok = true;
        return result;
    }

    SSAPipeline pipe(result.cfg, std::span<IRNode>(result.nodes.data(), result.nodes.size()));
    pipe.construct();
    result.verification_errors = pipe.verify();
    
    auto type_errors = verify_generated_result_type_annotations(stream);
    for (size_t i = 0; i < type_errors.count; ++i) {
        result.verification_errors.push(type_errors.errors[i]);
    }
    
    result.ok = result.verification_errors.empty();
    return result;
}

inline bool construct_ssa_in_stream(::dgtl::ir::IRStream& stream, VerificationErrorStack* errors_out = nullptr) {
    SSAFromStreamResult built = construct_ssa_from_stream(stream);

    if (errors_out) {
        *errors_out = built.verification_errors;
    }
    if (!built.ok) return false;

    if (built.nodes.size() != stream.size()) {
        if (errors_out) {
            errors_out->push("SSA node count mismatch while applying back to IRStream");
        }
        return false;
    }

    for (std::size_t i = 0u; i < built.nodes.size(); ++i) {
        stream.set_node(i, built.nodes[i]);
    }
    return true;
}

static_assert(sizeof(VReg)    == 4u, "VReg must be 4 bytes â€” SSA name encoding contract");
static_assert(sizeof(BlockId) == 4u, "BlockId must be 4 bytes");
static_assert(sizeof(NodeId)  == 4u, "NodeId must be 4 bytes");

} // namespace dgtl::ir::ssa




