/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Dominator Trees, Dominance Frontiers & Natural Loops
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>
#include <unordered_map>
#include <queue>
#include <stack>

export module dgtl.mir.machine_ir:analysis;

import :types;
import :target;
import :function;

export namespace dgtl::machine {

enum class MachineLoopKind : std::uint8_t {
    For,
    While,
    DoWhile,
    Infinite
};

struct MachineInductionVariable {
    MachineRegister iv_reg{};
    std::int64_t init_value = 0;
    std::int64_t step_value = 1;
    std::int64_t limit_value = 0;
    MachineSemanticKind compare_kind = MachineSemanticKind::Compare;
    bool is_constant_trip_count = false;
    std::uint64_t trip_count = 0;
};

class MachineLoop {
public:
    BlockId header = INVALID_BLOCK_ID;
    BlockId preheader = INVALID_BLOCK_ID;
    BlockId latch = INVALID_BLOCK_ID;
    std::vector<BlockId> latches;
    std::vector<BlockId> blocks;
    std::vector<BlockId> exit_blocks;
    std::vector<BlockId> exiting_blocks;
    MachineLoopKind kind = MachineLoopKind::While;
    std::optional<MachineInductionVariable> induction_var;
    std::uint32_t depth = 1;

    [[nodiscard]] bool contains(BlockId bb) const noexcept {
        return std::find(blocks.begin(), blocks.end(), bb) != blocks.end();
    }

    [[nodiscard]] bool contains(const MachineLoop& subloop) const noexcept {
        for (BlockId b : subloop.blocks) {
            if (!contains(b)) return false;
        }
        return true;
    }

    [[nodiscard]] bool is_loop_header(BlockId bb) const noexcept {
        return header == bb;
    }

    [[nodiscard]] bool is_loop_latch(BlockId bb) const noexcept {
        return latch == bb || std::find(latches.begin(), latches.end(), bb) != latches.end();
    }

    [[nodiscard]] bool is_loop_exiting(BlockId bb) const noexcept {
        return std::find(exiting_blocks.begin(), exiting_blocks.end(), bb) != exiting_blocks.end();
    }

    [[nodiscard]] bool is_loop_exit(BlockId bb) const noexcept {
        return std::find(exit_blocks.begin(), exit_blocks.end(), bb) != exit_blocks.end();
    }

    [[nodiscard]] bool is_for_loop() const noexcept { return kind == MachineLoopKind::For; }
    [[nodiscard]] bool is_while_loop() const noexcept { return kind == MachineLoopKind::While; }
    [[nodiscard]] bool is_do_while_loop() const noexcept { return kind == MachineLoopKind::DoWhile; }

    [[nodiscard]] bool has_constant_trip_count() const noexcept {
        return induction_var && induction_var->is_constant_trip_count;
    }

    [[nodiscard]] std::uint64_t trip_count() const noexcept {
        return induction_var ? induction_var->trip_count : 0;
    }
};

struct MachineDomTreeNode {
    BlockId block = INVALID_BLOCK_ID;
    BlockId idom = INVALID_BLOCK_ID;
    std::vector<BlockId> children;
    std::uint32_t dfs_in = 0;
    std::uint32_t dfs_out = 0;
    std::uint32_t level = 0;
};

class MachineDominatorTree {
    BlockId root_ = INVALID_BLOCK_ID;
    std::vector<MachineDomTreeNode> nodes_;

public:
    void clear() noexcept {
        root_ = INVALID_BLOCK_ID;
        nodes_.clear();
    }

    [[nodiscard]] bool empty() const noexcept {
        return root_ == INVALID_BLOCK_ID || nodes_.empty();
    }

    [[nodiscard]] BlockId root() const noexcept {
        return root_;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return nodes_.size();
    }

    [[nodiscard]] const std::vector<MachineDomTreeNode>& nodes() const noexcept {
        return nodes_;
    }

    [[nodiscard]] const MachineDomTreeNode* node(BlockId b) const noexcept {
        if (b >= nodes_.size()) return nullptr;
        return &nodes_[b];
    }

    [[nodiscard]] BlockId idom(BlockId b) const noexcept {
        if (b >= nodes_.size()) return INVALID_BLOCK_ID;
        return nodes_[b].idom;
    }

    [[nodiscard]] bool is_reachable(BlockId b) const noexcept {
        if (b >= nodes_.size()) return false;
        return nodes_[b].dfs_in > 0;
    }

    [[nodiscard]] std::span<const BlockId> children(BlockId b) const noexcept {
        if (b >= nodes_.size()) return {};
        return nodes_[b].children;
    }

    [[nodiscard]] std::uint32_t level(BlockId b) const noexcept {
        if (b >= nodes_.size()) return 0;
        return nodes_[b].level;
    }

    [[nodiscard]] bool dominates(BlockId a, BlockId b) const noexcept {
        if (a == b) return true;
        if (a >= nodes_.size() || b >= nodes_.size()) return false;
        const auto& na = nodes_[a];
        const auto& nb = nodes_[b];
        if (na.dfs_in == 0 || nb.dfs_in == 0) return false;
        return (na.dfs_in <= nb.dfs_in) && (na.dfs_out >= nb.dfs_out);
    }

    [[nodiscard]] bool strictly_dominates(BlockId a, BlockId b) const noexcept {
        return (a != b) && dominates(a, b);
    }

    [[nodiscard]] BlockId nearest_common_dominator(BlockId a, BlockId b) const noexcept {
        if (a == b) return a;
        if (a >= nodes_.size() || b >= nodes_.size()) return INVALID_BLOCK_ID;
        if (dominates(a, b)) return a;
        if (dominates(b, a)) return b;

        BlockId curr_a = a;
        BlockId curr_b = b;
        while (curr_a != INVALID_BLOCK_ID && curr_b != INVALID_BLOCK_ID && curr_a != curr_b) {
            if (nodes_[curr_a].level > nodes_[curr_b].level) {
                curr_a = nodes_[curr_a].idom;
            } else if (nodes_[curr_b].level > nodes_[curr_a].level) {
                curr_b = nodes_[curr_b].idom;
            } else {
                curr_a = nodes_[curr_a].idom;
                curr_b = nodes_[curr_b].idom;
            }
        }
        return curr_a;
    }

    void compute(const MachineFunction& mf);
};

class MachineDominanceFrontier {
    std::vector<std::vector<BlockId>> frontiers_;

public:
    void clear() noexcept {
        frontiers_.clear();
    }

    [[nodiscard]] bool empty() const noexcept {
        return frontiers_.empty();
    }

    [[nodiscard]] std::span<const BlockId> get_frontier(BlockId b) const noexcept {
        if (b >= frontiers_.size()) return {};
        return frontiers_[b];
    }

    [[nodiscard]] bool in_frontier(BlockId a, BlockId b) const noexcept {
        if (a >= frontiers_.size()) return false;
        const auto& f = frontiers_[a];
        return std::find(f.begin(), f.end(), b) != f.end();
    }

    [[nodiscard]] std::vector<BlockId> compute_iterated_dominance_frontier(
        std::span<const BlockId> def_blocks) const;

    void compute(const MachineFunction& mf, const MachineDominatorTree& dom_tree);
};

class MachineLoopInfo {
    std::vector<MachineLoop> loops_;
    std::unordered_map<BlockId, std::size_t> block_to_loop_;

public:
    void clear() noexcept {
        loops_.clear();
        block_to_loop_.clear();
    }

    [[nodiscard]] const std::vector<MachineLoop>& loops() const noexcept {
        return loops_;
    }

    [[nodiscard]] const MachineLoop* get_loop_for(BlockId bb) const noexcept {
        auto it = block_to_loop_.find(bb);
        if (it != block_to_loop_.end() && it->second < loops_.size()) {
            return &loops_[it->second];
        }
        return nullptr;
    }

    [[nodiscard]] std::uint32_t get_loop_depth(BlockId bb) const noexcept {
        const auto* l = get_loop_for(bb);
        return l ? l->depth : 0;
    }

    [[nodiscard]] bool is_loop_header(BlockId bb) const noexcept {
        const auto* l = get_loop_for(bb);
        return l && l->header == bb;
    }

    void add_loop(MachineLoop loop) {
        const std::size_t idx = loops_.size();
        for (BlockId bb : loop.blocks) {
            block_to_loop_[bb] = idx;
        }
        loops_.push_back(std::move(loop));
    }

    [[nodiscard]] bool empty() const noexcept { return loops_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return loops_.size(); }

    void compute(const MachineFunction& mf, const MachineDominatorTree& dom_tree);
};

/* ============================================================================
 * Algorithm Implementations
 * ========================================================================== */

inline void MachineDominatorTree::compute(const MachineFunction& mf) {
    clear();
    const auto block_span = mf.blocks();
    const std::size_t n = block_span.size();
    if (n == 0) return;

    root_ = mf.entry_block();
    if (root_ == INVALID_BLOCK_ID || root_ >= n) return;

    nodes_.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        nodes_[i].block = static_cast<BlockId>(i);
        nodes_[i].idom = INVALID_BLOCK_ID;
        nodes_[i].children.clear();
        nodes_[i].dfs_in = 0;
        nodes_[i].dfs_out = 0;
        nodes_[i].level = 0;
    }

    std::vector<bool> visited(n, false);
    std::vector<BlockId> post_order;
    post_order.reserve(n);
    std::vector<std::int32_t> post_order_num(n, -1);

    std::vector<std::pair<BlockId, std::size_t>> dfs_stack;
    visited[root_] = true;
    dfs_stack.push_back({root_, 0});

    while (!dfs_stack.empty()) {
        auto& [u, succ_idx] = dfs_stack.back();
        const auto* bb = mf.block(u);
        if (bb && succ_idx < bb->successors.size()) {
            BlockId succ = bb->successors[succ_idx++];
            if (succ < n && !visited[succ]) {
                visited[succ] = true;
                dfs_stack.push_back({succ, 0});
            }
        } else {
            post_order_num[u] = static_cast<std::int32_t>(post_order.size());
            post_order.push_back(u);
            dfs_stack.pop_back();
        }
    }

    std::vector<BlockId> idoms(n, INVALID_BLOCK_ID);
    idoms[root_] = root_;

    auto intersect = [&](BlockId b1, BlockId b2) -> BlockId {
        BlockId finger1 = b1;
        BlockId finger2 = b2;
        while (finger1 != finger2) {
            while (finger1 != INVALID_BLOCK_ID && finger2 != INVALID_BLOCK_ID &&
                   post_order_num[finger1] < post_order_num[finger2]) {
                finger1 = idoms[finger1];
            }
            while (finger1 != INVALID_BLOCK_ID && finger2 != INVALID_BLOCK_ID &&
                   post_order_num[finger2] < post_order_num[finger1]) {
                finger2 = idoms[finger2];
            }
            if (finger1 == INVALID_BLOCK_ID || finger2 == INVALID_BLOCK_ID) {
                break;
            }
        }
        return (finger1 != INVALID_BLOCK_ID) ? finger1 : finger2;
    };

    bool changed = true;
    while (changed) {
        changed = false;
        for (auto it = post_order.rbegin(); it != post_order.rend(); ++it) {
            const BlockId b = *it;
            if (b == root_) continue;
            const auto* bb = mf.block(b);
            if (!bb) continue;

            BlockId new_idom = INVALID_BLOCK_ID;
            for (BlockId p : bb->predecessors) {
                if (p < n && idoms[p] != INVALID_BLOCK_ID) {
                    new_idom = p;
                    break;
                }
            }
            if (new_idom == INVALID_BLOCK_ID) continue;

            for (BlockId p : bb->predecessors) {
                if (p < n && p != new_idom && idoms[p] != INVALID_BLOCK_ID) {
                    new_idom = intersect(p, new_idom);
                }
            }

            if (idoms[b] != new_idom) {
                idoms[b] = new_idom;
                changed = true;
            }
        }
    }
    idoms[root_] = INVALID_BLOCK_ID;

    for (std::size_t i = 0; i < n; ++i) {
        nodes_[i].idom = idoms[i];
        if (idoms[i] != INVALID_BLOCK_ID && idoms[i] < n) {
            nodes_[idoms[i]].children.push_back(static_cast<BlockId>(i));
        }
    }

    std::uint32_t timestamp = 0;
    nodes_[root_].level = 0;
    nodes_[root_].dfs_in = ++timestamp;

    std::vector<std::pair<BlockId, std::size_t>> dom_stack;
    dom_stack.push_back({root_, 0});

    while (!dom_stack.empty()) {
        auto& [u, child_idx] = dom_stack.back();
        if (child_idx < nodes_[u].children.size()) {
            BlockId v = nodes_[u].children[child_idx++];
            nodes_[v].level = nodes_[u].level + 1;
            nodes_[v].dfs_in = ++timestamp;
            dom_stack.push_back({v, 0});
        } else {
            nodes_[u].dfs_out = ++timestamp;
            dom_stack.pop_back();
        }
    }
}

inline void MachineDominanceFrontier::compute(const MachineFunction& mf, const MachineDominatorTree& dom_tree) {
    clear();
    const auto n = mf.blocks().size();
    frontiers_.assign(n, {});
    if (n == 0 || dom_tree.empty()) return;

    for (const auto& bb : mf.blocks()) {
        if (bb.predecessors.size() < 2) continue;
        const BlockId b = bb.id;
        const BlockId idom_b = dom_tree.idom(b);

        for (BlockId p : bb.predecessors) {
            BlockId runner = p;
            while (runner != INVALID_BLOCK_ID && runner != idom_b && runner < n) {
                auto& df = frontiers_[runner];
                if (std::find(df.begin(), df.end(), b) == df.end()) {
                    df.push_back(b);
                }
                runner = dom_tree.idom(runner);
            }
        }
    }
}

inline std::vector<BlockId> MachineDominanceFrontier::compute_iterated_dominance_frontier(
    std::span<const BlockId> def_blocks) const {
    std::vector<BlockId> idf;
    if (def_blocks.empty() || frontiers_.empty()) return idf;

    std::vector<bool> in_idf(frontiers_.size(), false);
    std::vector<bool> in_worklist(frontiers_.size(), false);
    std::vector<BlockId> worklist;
    worklist.reserve(def_blocks.size());

    for (BlockId b : def_blocks) {
        if (b < frontiers_.size()) {
            worklist.push_back(b);
            in_worklist[b] = true;
        }
    }

    std::size_t idx = 0;
    while (idx < worklist.size()) {
        BlockId node = worklist[idx++];
        if (node >= frontiers_.size()) continue;

        for (BlockId frontier_node : frontiers_[node]) {
            if (frontier_node < frontiers_.size() && !in_idf[frontier_node]) {
                in_idf[frontier_node] = true;
                idf.push_back(frontier_node);
                if (!in_worklist[frontier_node]) {
                    in_worklist[frontier_node] = true;
                    worklist.push_back(frontier_node);
                }
            }
        }
    }
    return idf;
}

inline void MachineLoopInfo::compute(const MachineFunction& mf, const MachineDominatorTree& dom_tree) {
    clear();
    const auto block_count = mf.blocks().size();
    if (block_count == 0 || dom_tree.empty()) return;

    std::unordered_map<BlockId, std::vector<BlockId>> header_to_latches;
    for (const auto& bb : mf.blocks()) {
        const BlockId u = bb.id;
        for (BlockId v : bb.successors) {
            if (dom_tree.dominates(v, u)) {
                header_to_latches[v].push_back(u);
            }
        }
    }

    for (auto& [header, latches] : header_to_latches) {
        MachineLoop loop;
        loop.header = header;
        loop.latch = latches.front();
        loop.latches = latches;

        std::vector<bool> in_loop(block_count, false);
        loop.blocks.push_back(header);
        in_loop[header] = true;

        std::vector<BlockId> worklist;
        for (BlockId latch : latches) {
            if (!in_loop[latch]) {
                in_loop[latch] = true;
                loop.blocks.push_back(latch);
            }
            worklist.push_back(latch);
        }

        while (!worklist.empty()) {
            BlockId curr = worklist.back();
            worklist.pop_back();
            const auto* curr_bb = mf.block(curr);
            if (!curr_bb) continue;

            for (BlockId pred : curr_bb->predecessors) {
                if (pred < block_count && !in_loop[pred]) {
                    in_loop[pred] = true;
                    loop.blocks.push_back(pred);
                    worklist.push_back(pred);
                }
            }
        }

        const auto* header_bb = mf.block(header);
        if (header_bb) {
            BlockId candidate_preheader = INVALID_BLOCK_ID;
            std::size_t outside_preds = 0;
            for (BlockId pred : header_bb->predecessors) {
                if (pred < block_count && !in_loop[pred]) {
                    outside_preds++;
                    candidate_preheader = pred;
                }
            }
            if (outside_preds == 1) {
                loop.preheader = candidate_preheader;
            }
        }

        for (BlockId b : loop.blocks) {
            const auto* b_bb = mf.block(b);
            if (!b_bb) continue;
            bool is_exiting = false;
            for (BlockId succ : b_bb->successors) {
                if (succ < block_count && !in_loop[succ]) {
                    is_exiting = true;
                    if (std::find(loop.exit_blocks.begin(), loop.exit_blocks.end(), succ) == loop.exit_blocks.end()) {
                        loop.exit_blocks.push_back(succ);
                    }
                }
            }
            if (is_exiting) {
                loop.exiting_blocks.push_back(b);
            }
        }

        loop.kind = MachineLoopKind::While;
        const auto* latch_bb = mf.block(loop.latch);
        if (latch_bb && !latch_bb->instructions.empty()) {
            const auto* last_inst = mf.instruction(latch_bb->instructions.back());
            if (last_inst && (last_inst->form == MachineForm::BranchCond || last_inst->form == MachineForm::BranchCompare)) {
                loop.kind = MachineLoopKind::DoWhile;
            }
        }

        add_loop(std::move(loop));
    }

    for (std::size_t i = 0; i < loops_.size(); ++i) {
        std::uint32_t depth = 1;
        for (std::size_t j = 0; j < loops_.size(); ++j) {
            if (i == j) continue;
            if (loops_[j].blocks.size() > loops_[i].blocks.size() &&
                loops_[j].contains(loops_[i].header)) {
                depth++;
            }
        }
        loops_[i].depth = depth;
    }

    block_to_loop_.clear();
    for (std::size_t i = 0; i < loops_.size(); ++i) {
        for (BlockId bb : loops_[i].blocks) {
            auto it = block_to_loop_.find(bb);
            if (it == block_to_loop_.end()) {
                block_to_loop_[bb] = i;
            } else {
                if (loops_[i].depth > loops_[it->second].depth) {
                    block_to_loop_[bb] = i;
                }
            }
        }
    }
}

// Concrete bridge method implementations for MachineFunction
inline void MachineFunction::invalidate_analysis_caches() noexcept {
    dom_tree_valid_ = false;
    dom_frontier_valid_ = false;
    loop_info_valid_ = false;
}

inline void MachineFunction::insert_instruction_before(BlockId block_id, InstrId cursor_id, InstrId new_instr_id) {
    MachineBasicBlock* bb = block(block_id);
    MachineInstruction* new_inst = instruction(new_instr_id);
    if (!bb || !new_inst) return;
    if (new_inst->parent_block != INVALID_BLOCK_ID) return;

    if (cursor_id != INVALID_INSTR_ID) {
        MachineInstruction* cursor = instruction(cursor_id);
        if (!cursor || cursor->parent_block != block_id) return;
    }

    invalidate_analysis_caches();
    new_inst->parent_block = block_id;
    if (cursor_id == INVALID_INSTR_ID || bb->head_instr == INVALID_INSTR_ID) {
        if (bb->tail_instr != INVALID_INSTR_ID) {
            instruction(bb->tail_instr)->next_instr = new_instr_id;
            new_inst->prev_instr = bb->tail_instr;
            new_inst->next_instr = INVALID_INSTR_ID;
            bb->tail_instr = new_instr_id;
        } else {
            bb->head_instr = bb->tail_instr = new_instr_id;
            new_inst->prev_instr = new_inst->next_instr = INVALID_INSTR_ID;
        }
        bb->instructions.push_back(new_instr_id);
    } else {
        MachineInstruction* cursor = instruction(cursor_id);
        new_inst->next_instr = cursor_id;
        new_inst->prev_instr = cursor->prev_instr;
        if (cursor->prev_instr != INVALID_INSTR_ID) {
            instruction(cursor->prev_instr)->next_instr = new_instr_id;
        } else {
            bb->head_instr = new_instr_id;
        }
        cursor->prev_instr = new_instr_id;

        const auto it = std::find(bb->instructions.begin(), bb->instructions.end(), cursor_id);
        if (it == bb->instructions.end()) return;
        bb->instructions.insert(it, new_instr_id);
    }
    bb->instr_count = bb->instructions.size();
}

inline void MachineFunction::remove_instruction(InstrId instr_id) {
    MachineInstruction* inst = instruction(instr_id);
    if (!inst || inst->parent_block == INVALID_BLOCK_ID) return;
    MachineBasicBlock* bb = block(inst->parent_block);
    if (!bb) return;

    invalidate_analysis_caches();

    if (inst->prev_instr != INVALID_INSTR_ID) {
        MachineInstruction* prev = instruction(inst->prev_instr);
        if (prev) prev->next_instr = inst->next_instr;
    } else {
        bb->head_instr = inst->next_instr;
    }

    if (inst->next_instr != INVALID_INSTR_ID) {
        MachineInstruction* next = instruction(inst->next_instr);
        if (next) next->prev_instr = inst->prev_instr;
    } else {
        bb->tail_instr = inst->prev_instr;
    }

    auto it = std::find(bb->instructions.begin(), bb->instructions.end(), instr_id);
    if (it != bb->instructions.end()) {
        bb->instructions.erase(it);
    }
    bb->instr_count = bb->instructions.size();

    inst->parent_block = INVALID_BLOCK_ID;
    inst->prev_instr = INVALID_INSTR_ID;
    inst->next_instr = INVALID_INSTR_ID;
}

inline void MachineFunction::splice_instructions(BlockId dest_block_id, InstrId cursor_id,
                                                BlockId src_block_id, InstrId first_instr_id, InstrId last_instr_id) {
    if (src_block_id == dest_block_id && first_instr_id == cursor_id) return;
    MachineBasicBlock* src_bb = block(src_block_id);
    MachineBasicBlock* dest_bb = block(dest_block_id);
    if (!src_bb || !dest_bb) return;

    MachineInstruction* first_inst = instruction(first_instr_id);
    MachineInstruction* last_inst = instruction(last_instr_id);
    if (!first_inst || !last_inst) return;
    if (first_inst->parent_block != src_block_id || last_inst->parent_block != src_block_id) return;

    invalidate_analysis_caches();

    std::vector<InstrId> range;
    InstrId cur = first_instr_id;
    while (cur != INVALID_INSTR_ID) {
        range.push_back(cur);
        MachineInstruction* ci = instruction(cur);
        if (!ci) break;
        ci->parent_block = dest_block_id;
        if (cur == last_instr_id) break;
        cur = ci->next_instr;
    }

    if (first_inst->prev_instr != INVALID_INSTR_ID) {
        MachineInstruction* prev = instruction(first_inst->prev_instr);
        if (prev) prev->next_instr = last_inst->next_instr;
    } else {
        src_bb->head_instr = last_inst->next_instr;
    }

    if (last_inst->next_instr != INVALID_INSTR_ID) {
        MachineInstruction* next = instruction(last_inst->next_instr);
        if (next) next->prev_instr = first_inst->prev_instr;
    } else {
        src_bb->tail_instr = first_inst->prev_instr;
    }

    for (InstrId id : range) {
        auto it = std::find(src_bb->instructions.begin(), src_bb->instructions.end(), id);
        if (it != src_bb->instructions.end()) {
            src_bb->instructions.erase(it);
        }
    }
    src_bb->instr_count = src_bb->instructions.size();

    if (cursor_id == INVALID_INSTR_ID || dest_bb->head_instr == INVALID_INSTR_ID) {
        if (dest_bb->tail_instr != INVALID_INSTR_ID) {
            instruction(dest_bb->tail_instr)->next_instr = first_instr_id;
            first_inst->prev_instr = dest_bb->tail_instr;
            last_inst->next_instr = INVALID_INSTR_ID;
            dest_bb->tail_instr = last_instr_id;
        } else {
            dest_bb->head_instr = first_instr_id;
            dest_bb->tail_instr = last_instr_id;
            first_inst->prev_instr = INVALID_INSTR_ID;
            last_inst->next_instr = INVALID_INSTR_ID;
        }
        dest_bb->instructions.insert(dest_bb->instructions.end(), range.begin(), range.end());
    } else {
        MachineInstruction* cursor = instruction(cursor_id);
        first_inst->prev_instr = cursor->prev_instr;
        first_inst->next_instr = (first_instr_id == last_instr_id) ? cursor_id : first_inst->next_instr;
        last_inst->next_instr = cursor_id;

        if (cursor->prev_instr != INVALID_INSTR_ID) {
            instruction(cursor->prev_instr)->next_instr = first_instr_id;
        } else {
            dest_bb->head_instr = first_instr_id;
        }
        cursor->prev_instr = last_instr_id;

        auto it = std::find(dest_bb->instructions.begin(), dest_bb->instructions.end(), cursor_id);
        if (it != dest_bb->instructions.end()) {
            dest_bb->instructions.insert(it, range.begin(), range.end());
        } else {
            dest_bb->instructions.insert(dest_bb->instructions.end(), range.begin(), range.end());
        }
    }
    dest_bb->instr_count = dest_bb->instructions.size();
}

inline void MachineFunction::recompute_dominators() const {
    if (!dom_tree_cache_ptr_) {
        dom_tree_cache_ptr_ = new MachineDominatorTree();
    }
    static_cast<MachineDominatorTree*>(dom_tree_cache_ptr_)->compute(*this);
    dom_tree_valid_ = true;
}

inline const MachineDominatorTree& MachineFunction::dominators() const {
    if (!dom_tree_valid_) {
        recompute_dominators();
    }
    return *static_cast<const MachineDominatorTree*>(dom_tree_cache_ptr_);
}

inline bool MachineFunction::dominates(BlockId a, BlockId b) const {
    return dominators().dominates(a, b);
}

inline bool MachineFunction::strictly_dominates(BlockId a, BlockId b) const {
    return dominators().strictly_dominates(a, b);
}

inline bool MachineFunction::instruction_dominates(InstrId a, InstrId b) const {
    if (a == b) return true;
    const auto* ia = instruction(a);
    const auto* ib = instruction(b);
    if (!ia || !ib) return false;
    if (ia->parent_block == INVALID_BLOCK_ID || ib->parent_block == INVALID_BLOCK_ID) return false;

    if (ia->parent_block == ib->parent_block) {
        InstrId curr = ia->next_instr;
        while (curr != INVALID_INSTR_ID) {
            if (curr == b) return true;
            const auto* cinst = instruction(curr);
            if (!cinst) break;
            curr = cinst->next_instr;
        }
        return false;
    }

    return dominates(ia->parent_block, ib->parent_block);
}

inline bool MachineFunction::dominates(const MachineInstruction& a, const MachineInstruction& b) const {
    return instruction_dominates(a.id, b.id);
}

inline BlockId MachineFunction::nearest_common_dominator(BlockId a, BlockId b) const {
    return dominators().nearest_common_dominator(a, b);
}

inline void MachineFunction::recompute_dominance_frontier() const {
    if (!dom_frontier_cache_ptr_) {
        dom_frontier_cache_ptr_ = new MachineDominanceFrontier();
    }
    static_cast<MachineDominanceFrontier*>(dom_frontier_cache_ptr_)->compute(*this, dominators());
    dom_frontier_valid_ = true;
}

inline const MachineDominanceFrontier& MachineFunction::dominance_frontier() const {
    if (!dom_frontier_valid_) {
        recompute_dominance_frontier();
    }
    return *static_cast<const MachineDominanceFrontier*>(dom_frontier_cache_ptr_);
}

inline void MachineFunction::recompute_loop_info() const {
    if (!loop_info_cache_ptr_) {
        loop_info_cache_ptr_ = new MachineLoopInfo();
    }
    static_cast<MachineLoopInfo*>(loop_info_cache_ptr_)->compute(*this, dominators());
    loop_info_valid_ = true;
}

inline MachineLoopInfo& MachineFunction::loop_info() noexcept {
    if (!loop_info_valid_) {
        recompute_loop_info();
    }
    return *static_cast<MachineLoopInfo*>(loop_info_cache_ptr_);
}

inline const MachineLoopInfo& MachineFunction::loop_info() const noexcept {
    if (!loop_info_valid_) {
        recompute_loop_info();
    }
    return *static_cast<const MachineLoopInfo*>(loop_info_cache_ptr_);
}

inline std::vector<BlockId> MachineFunction::traversal_preorder() const {
    std::vector<BlockId> order;
    const BlockId root = entry_block();
    if (root == INVALID_BLOCK_ID || root >= blocks().size()) return order;
    std::vector<bool> visited(blocks().size(), false);
    std::vector<BlockId> stack;
    stack.push_back(root);
    visited[root] = true;

    while (!stack.empty()) {
        BlockId curr = stack.back();
        stack.pop_back();
        order.push_back(curr);
        const auto* bb = block(curr);
        if (!bb) continue;
        for (auto it = bb->successors.rbegin(); it != bb->successors.rend(); ++it) {
            BlockId succ = *it;
            if (succ < blocks().size() && !visited[succ]) {
                visited[succ] = true;
                stack.push_back(succ);
            }
        }
    }
    return order;
}

inline std::vector<BlockId> MachineFunction::traversal_postorder() const {
    std::vector<BlockId> order;
    const BlockId root = entry_block();
    if (root == INVALID_BLOCK_ID || root >= blocks().size()) return order;
    std::vector<bool> visited(blocks().size(), false);
    std::vector<std::pair<BlockId, std::size_t>> stack;
    visited[root] = true;
    stack.push_back({root, 0});

    while (!stack.empty()) {
        auto& [u, succ_idx] = stack.back();
        const auto* bb = block(u);
        if (bb && succ_idx < bb->successors.size()) {
            BlockId succ = bb->successors[succ_idx++];
            if (succ < blocks().size() && !visited[succ]) {
                visited[succ] = true;
                stack.push_back({succ, 0});
            }
        } else {
            order.push_back(u);
            stack.pop_back();
        }
    }
    return order;
}

inline std::vector<BlockId> MachineFunction::traversal_reverse_postorder() const {
    auto order = traversal_postorder();
    std::reverse(order.begin(), order.end());
    return order;
}

inline std::vector<BlockId> MachineFunction::traversal_breadth_first() const {
    std::vector<BlockId> order;
    const BlockId root = entry_block();
    if (root == INVALID_BLOCK_ID || root >= blocks().size()) return order;
    std::vector<bool> visited(blocks().size(), false);
    std::queue<BlockId> q;
    visited[root] = true;
    q.push(root);

    while (!q.empty()) {
        BlockId curr = q.front();
        q.pop();
        order.push_back(curr);
        const auto* bb = block(curr);
        if (!bb) continue;
        for (BlockId succ : bb->successors) {
            if (succ < blocks().size() && !visited[succ]) {
                visited[succ] = true;
                q.push(succ);
            }
        }
    }
    return order;
}

} // namespace dgtl::machine