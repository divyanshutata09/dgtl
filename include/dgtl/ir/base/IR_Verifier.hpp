/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Version:    1.0.0 — FLAT-STREAM IR VERIFIER  [PRODUCTION-GRADE v2.0]
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * PILLAR 3 COMPLEMENT: LIGHTWEIGHT IR CORRECTNESS ORACLE
 *
 * PURPOSE
 * -------
 * IR_Verifier.hpp is the flat-stream verifier for DGTL IR.  It operates
 * directly on a const IRStream without constructing a CFG or DomTree —
 * making it suitable for quick pre-pass correctness gates, post-transform
 * sanity checks, and test harness integration.
 *
 * RELATIONSHIP TO OTHER VERIFIERS
 * --------------------------------
 *  IR_Verifier         (this file)  — flat-stream, no CFG required, O(N)
 *  SSAVerifier         (IR_SSA.hpp) — full SSA property, needs CFG + DomTree
 *  validate_shadow_integrity()      — IRStream-internal transactional guard
 *  IR_Analysis_Bridge.hpp           — entry-point readiness/bridge guard helpers
 *
 * CHECKS PERFORMED
 * ----------------
 *  1. Structural integrity  — producing opcodes must write a valid result_id
 *  2. Hash integrity        — intent_hash must match IRNode::calculate_hash()
 *  3. Flat single-def       — no result_id written more than once in the stream
 *  4. Use-before-def        — operands must be defined before first use (flat order)
 *  5. Ownership lifecycle   — ACQUIRE/RELEASE pairing; no double-RELEASE; no leak
 *  6. Lock lifecycle        — LOCK_ACQUIRE/LOCK_RELEASE pairing; no double-unlock
 *  7. Opcode constraints    — per-opcode operand rules (BRANCH target, CALL callee,
 *                             STORE address+value, FMA three-operand, etc.)
 *
 * BUGS FIXED FROM v1.0
 * --------------------
 *  BUG-1  Missing #include "IR_PrintSerialDumper.hpp" — opcode_name() was
 *         undefined, causing a compilation failure in any TU that included
 *         this header.
 *  BUG-2  NOP hash exemption was wrong: the old guard
 *         `if (NOP && hash==0) continue` still checked NOPs whose hash had
 *         not been zeroed after tombstone replacement — causing false hash-
 *         corruption reports.  Fix: skip ALL NOP nodes unconditionally.
 *  BUG-3  LOAD_IMM / IMMEDIATE operand false positives: op_a and op_b of
 *         these opcodes carry the HIGH/LOW 32-bit halves of a 64-bit
 *         immediate constant, NOT virtual register IDs.  The old
 *         check_use_before_def_flat treated them as vregs, producing a
 *         use-before-def false positive for every constant in the stream.
 *  BUG-4  NOP tombstones in use-before-def: replaced nodes that became NOPs
 *         but whose op fields were not zeroed would be scanned for operand IDs
 *         and generate spurious use-before-def diagnostics.
 *  BUG-5  NOP tombstones in single-def: a NOP node with a non-zero result_id
 *         (stale from the instruction it replaced) would poison the def_map
 *         and cause false DuplicateDef reports for the still-live defining node.
 *  BUG-6  is_value_producing was incomplete: missing REG_MOVE, SIMD_BSL_PREDICATE,
 *         all 10 tensor ops, 7 DSA value-producing ops, CHAN_RECV, LOCK_ACQUIRE.
 *  BUG-7  check_opcode_constraints was missing: GOTO target, STORE/STORE_V512/
 *         VEC_STORE address+value, OWNERSHIP_TRANSFER handle,
 *         FMA three-operand, SYSCALL number, BRANCH_COND false-target,
 *         LOCK_RELEASE handle.
 *  BUG-8  No LOCK_ACQUIRE/LOCK_RELEASE lifecycle tracking — a lock taken in
 *         one block and double-released in another would go undetected.
 *  BUG-9  No max_diagnostics cap — a 1M-node corrupted stream could exhaust
 *         memory by generating millions of identical diagnostics.
 *  BUG-10 No Severity field — callers could not distinguish hard errors from
 *         advisory warnings without parsing diagnostic messages.
 *
 * STANDARDS: C++26 / UCRT64 — Zero-Overhead Primitive Mapping
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

#pragma once

#include "IR_Node.hpp"
#include "IR_Stream.hpp"
#include "IR_PrintSerialDumper.hpp" // opcode_name() — required for human-readable messages

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "dgtl/utils/container/Flat_Map.hpp"

namespace dgtl::ir {

// ============================================================================
// SECTION: DIAGNOSTIC TYPES
// ============================================================================

/**
 * @struct IRVerifyDiagnostic
 * @brief A single verifier finding, collected into a vector by IRVerifier::verify().
 *
 * Fields are ordered so that aggregate initialization `{kind, node_index, extra_id,
 * message}` works without specifying severity (it defaults to Error).
 */
struct IRVerifyDiagnostic {

    enum class Kind : uint8_t {
        StructuralError,    ///< Value-producing opcode wrote INVALID_SLOT result_id
        HashCorruption,     ///< intent_hash mismatch — mutation without refresh_intent_hash()
        DuplicateDef,       ///< Same result_id defined at two nodes (SSA violation)
        UseBeforeDef,       ///< Operand (vreg) appears before its defining node in flat order
        OwnershipViolation, ///< Double-RELEASE, orphan RELEASE, or unreleased ACQUIRE (leak)
        LockViolation,      ///< LOCK double-unlock, unlock-without-lock, or lock never released
        InternalInvariantViolation, ///< Verifier internal map/set capacity or bookkeeping invariant failure
        OpcodeConstraint,   ///< Opcode-specific operand rule violated
    };

    enum class Severity : uint8_t {
        Error,   ///< Stream is definitively malformed; undefined behaviour possible
        Warning, ///< Advisory — correctness may be preserved but property is unexpected
    };

    Kind        kind;
    uint32_t    node_index;              ///< 0-based index in the stream
    uint32_t    extra_id  = INVALID_SLOT;///< Secondary ID (first-def idx, handle, etc.)
    std::string message;
    Severity    severity  = Severity::Error; ///< Default: every finding is an Error
};

// ============================================================================
// SECTION: VERIFIER OPTIONS
// ============================================================================

/**
 * @struct IRVerifyOptions
 * @brief Selects which passes to run and sets safety caps on diagnostic volume.
 *
 * All fields default to the most comprehensive, production-safe setting.
 * Disable individual passes in test harnesses that want to isolate a single check.
 */
struct IRVerifyOptions {
    /// Maximum diagnostics to collect before halting further emission.
    /// Prevents allocator exhaustion on structurally corrupted streams.
    /// Set to 0 for unlimited (strongly discouraged in production).
    uint32_t max_diagnostics    = 512;

    bool check_structural       = true; ///< Pass 1: value-producing ops write valid result_id
    bool check_hash             = true; ///< Pass 2: intent_hash consistency
    bool check_single_def       = true; ///< Pass 3: flat SSA single-definition
    bool check_use_before_def   = true; ///< Pass 4: operands precede their definitions
    bool check_ownership        = true; ///< Pass 5: OWNERSHIP_ACQUIRE/RELEASE lifecycle
    bool check_locks            = true; ///< Pass 6: LOCK_ACQUIRE/LOCK_RELEASE lifecycle
    bool check_opcode           = true; ///< Pass 7: per-opcode semantic constraints

    /// Demote HashCorruption findings to Severity::Warning instead of Error.
    /// Use when verification runs immediately before a scheduled hash-refresh pass,
    /// where mismatches are expected and non-critical.
    bool warn_hash_corruption   = false;
};

// ============================================================================
// SECTION: VERIFIER
// ============================================================================

/**
 * @class IRVerifier
 * @brief Lightweight, stateless flat-stream IR correctness oracle.
 *
 * All verify methods are `const` — one instance can be reused across many streams.
 * No CFG or DomTree is required; all checks are O(N).
 *
 * Usage:
 * @code
 *   dgtl::ir::IRVerifier verifier;
 *
 *   // Full verification with default options:
 *   auto diags = verifier.verify(stream);
 *   if (!diags.empty()) { ... }
 *
 *   // Quick pass/fail gate:
 *   if (!verifier.is_valid(stream)) { ... }
 *
 *   // Custom options — hash warnings, cap at 64:
 *   dgtl::ir::IRVerifyOptions opts;
 *   opts.max_diagnostics    = 64;
 *   opts.warn_hash_corruption = true;
 *   auto diags = verifier.verify(stream, opts);
 * @endcode
 *
 * For full SSA dominance-aware verification, use SSAVerifier from IR_SSA.hpp
 * after building a CFG.
 */
class IRVerifier {
public:
    IRVerifier()                             = default;
    ~IRVerifier()                            = default;
    IRVerifier(IRVerifier&&)                 = default;
    IRVerifier& operator=(IRVerifier&&)      = default;
    IRVerifier(const IRVerifier&)            = delete;
    IRVerifier& operator=(const IRVerifier&) = delete;

    // ----------------------------------------------------------------
    // PRIMARY API
    // ----------------------------------------------------------------

    /**
     * @brief Run all enabled verification passes.
     * @param stream  The stream to verify.
     * @param opts    Pass selection and cap (defaults: all passes, cap 512).
     * @return Vector of diagnostics.  Empty = stream is clean.
     */
    [[nodiscard]] std::vector<IRVerifyDiagnostic>
    verify(const IRStream& stream, const IRVerifyOptions& opts = {}) const {
        std::vector<IRVerifyDiagnostic> diags;
        if (stream.is_null() || stream.size() == 0) return diags;
        diags.reserve(std::min<size_t>(64, opts.max_diagnostics > 0 ? opts.max_diagnostics : 64));

        if (opts.check_structural     && !capped(diags, opts)) check_structural(stream, diags, opts);
        if (opts.check_hash           && !capped(diags, opts)) check_hash_integrity(stream, diags, opts);
        if (opts.check_single_def     && !capped(diags, opts)) check_single_def_flat(stream, diags, opts);
        if (opts.check_use_before_def && !capped(diags, opts)) check_use_before_def_flat(stream, diags, opts);
        if (opts.check_ownership      && !capped(diags, opts)) check_ownership_lifecycle(stream, diags, opts);
        if (opts.check_locks          && !capped(diags, opts)) check_lock_lifecycle(stream, diags, opts);
        if (opts.check_opcode         && !capped(diags, opts)) check_opcode_constraints(stream, diags, opts);

        if (opts.max_diagnostics > 0 && diags.size() > opts.max_diagnostics) {
            diags.resize(opts.max_diagnostics);
        }
        return diags;
    }

    /**
     * @brief Quick validity gate — returns false on the first error-level violation.
     *
     * Warnings do NOT cause this to return false; use verify() + ir_count_errors()
     * if you want to treat warnings as failures.
     */
    [[nodiscard]] bool is_valid(const IRStream& stream,
                                const IRVerifyOptions& opts = {}) const noexcept {
        if (stream.is_null() || stream.size() == 0) return true;
        // Run with cap=1: any single Error diagnostic means invalid.
        IRVerifyOptions fast = opts;
        fast.max_diagnostics = 1;
        auto diags = verify(stream, fast);
        for (const auto& d : diags) {
            if (d.severity == IRVerifyDiagnostic::Severity::Error) return false;
        }
        return true;
    }

    // ----------------------------------------------------------------
    // INDIVIDUAL PASSES  (public for targeted use in test harnesses)
    // ----------------------------------------------------------------

    /**
     * @brief Pass 1 — Structural integrity.
     *
     * Every value-producing opcode must write a valid result_id != INVALID_SLOT.
     * NOP tombstones are unconditionally exempt.
     */
    void check_structural(const IRStream& stream,
                          std::vector<IRVerifyDiagnostic>& diags,
                          const IRVerifyOptions& opts = {}) const {
        const size_t n = stream.size();
        for (size_t i = 0; i < n && !capped(diags, opts); ++i) {
            const IRNode nd = stream.get_node(i);
            if (nd.opcode == OpCode::NOP)            continue; // tombstone
            if (!is_value_producing(nd.opcode))      continue;
            if (nd.result_id == INVALID_SLOT) {
                push(diags, {
                    IRVerifyDiagnostic::Kind::StructuralError,
                    u32(i), INVALID_SLOT,
                    "Node[" + s(i) + "]: opcode " +
                    std::string(opcode_name(nd.opcode)) +
                    " is value-producing but result_id == INVALID_SLOT"
                });
            }
        }
    }

    /**
     * @brief Pass 2 — Intent-hash integrity.
     *
     * Every non-NOP node's intent_hash must equal
     * `IRNode::calculate_hash(opcode, result_id, op_a, op_b, op_c)`.
     *
     * NOP nodes are unconditionally exempt: they are tombstones that may retain
     * stale hashes from the instructions they replaced (zeroing the hash on
     * tombstoning is not guaranteed by the IRStream contract).
     *
     * Set `opts.warn_hash_corruption = true` to demote mismatches to Warning
     * when running before a scheduled hash-refresh pass.
     */
    void check_hash_integrity(const IRStream& stream,
                              std::vector<IRVerifyDiagnostic>& diags,
                              const IRVerifyOptions& opts = {}) const {
        const size_t n = stream.size();
        const auto sev = opts.warn_hash_corruption
                         ? IRVerifyDiagnostic::Severity::Warning
                         : IRVerifyDiagnostic::Severity::Error;

        for (size_t i = 0; i < n && !capped(diags, opts); ++i) {
            const IRNode nd = stream.get_node(i);
            if (nd.opcode == OpCode::NOP) continue; // tombstones carry stale hashes

            const uint64_t expected =
                IRNode::calculate_hash(nd.opcode, nd.result_id, nd.op_a, nd.op_b, nd.op_c);
            if (nd.intent_hash != expected) {
                IRVerifyDiagnostic d {
                    IRVerifyDiagnostic::Kind::HashCorruption,
                    u32(i), INVALID_SLOT,
                    "Node[" + s(i) + "]: intent_hash mismatch on " +
                    std::string(opcode_name(nd.opcode)) +
                    " (stored=0x"   + hex64(nd.intent_hash) +
                    " expected=0x"  + hex64(expected) + ")"
                };
                d.severity = sev;
                push(diags, std::move(d));
            }
        }
    }

    /**
     * @brief Pass 3 — Flat single-definition check.
     *
     * Every result_id must be defined at most once across the flat stream.
     * This is a fast O(N) pre-check; the dominance-aware version is SSAVerifier.
     *
     * Exemptions:
     *   - NOP tombstones (stale result_id from replaced instruction)
     *   - result_id == 0   (NULL_REGISTER sentinel — not a real def)
     *   - result_id == INVALID_SLOT (node has no result)
     */
    void check_single_def_flat(const IRStream& stream,
                               std::vector<IRVerifyDiagnostic>& diags,
                               const IRVerifyOptions& opts = {}) const {
        const size_t n = stream.size();
        constexpr size_t kFastCapacity = 16384;
        constexpr size_t kFastMaxLoad = (kFastCapacity * 3u) / 4u;
        constexpr size_t kOverflowCapacity = 131072;
        constexpr size_t kOverflowMaxLoad = (kOverflowCapacity * 3u) / 4u;

        if (n <= kFastMaxLoad) {
            dgtl::utils::FastFlatMap<uint32_t, uint32_t, kFastCapacity> def_map;
            for (size_t i = 0; i < n && !capped(diags, opts); ++i) {
                const IRNode nd = stream.get_node(i);
                if (nd.opcode     == OpCode::NOP) continue;
                if (nd.result_id  == INVALID_SLOT)continue;
                if (nd.result_id  == 0)           continue;

                if (const uint32_t* first = def_map.find(nd.result_id); first != nullptr) {
                    push(diags, {
                        IRVerifyDiagnostic::Kind::DuplicateDef,
                        u32(i),
                        *first,
                        "Node[" + s(i) + "]: result_id " +
                        std::to_string(nd.result_id) +
                        " already defined at node[" + std::to_string(*first) + "]"
                        " (opcode=" + std::string(opcode_name(nd.opcode)) + ")"
                    });
                    continue;
                }

                const bool ok = def_map.insert(nd.result_id, u32(i));
                if (!ok) {
                    push(diags, {
                        IRVerifyDiagnostic::Kind::InternalInvariantViolation,
                        u32(i), nd.result_id,
                        "Verifier fast def-map insertion failed unexpectedly"
                    });
                }
            }
            return;
        }

        if (n > kOverflowMaxLoad) {
            push(diags, {
                IRVerifyDiagnostic::Kind::InternalInvariantViolation,
                0u, INVALID_SLOT,
                "Verifier def-map capacity exceeded"
            });
            return;
        }

        dgtl::utils::FastFlatMap<uint32_t, uint32_t, kOverflowCapacity> def_map;

        for (size_t i = 0; i < n && !capped(diags, opts); ++i) {
            const IRNode nd = stream.get_node(i);
            if (nd.opcode     == OpCode::NOP) continue;
            if (nd.result_id  == INVALID_SLOT)continue;
            if (nd.result_id  == 0)           continue;

            if (const uint32_t* first = def_map.find(nd.result_id); first != nullptr) {
                push(diags, {
                    IRVerifyDiagnostic::Kind::DuplicateDef,
                    u32(i),
                    *first,
                    "Node[" + s(i) + "]: result_id " +
                    std::to_string(nd.result_id) +
                    " already defined at node[" + std::to_string(*first) + "]"
                    " (opcode=" + std::string(opcode_name(nd.opcode)) + ")"
                });
            } else {
                const bool ok = def_map.insert(nd.result_id, u32(i));
                if (!ok) {
                    push(diags, {
                        IRVerifyDiagnostic::Kind::InternalInvariantViolation,
                        u32(i), nd.result_id,
                        "Verifier overflow def-map insertion failed"
                    });
                    return;
                }
            }
        }
    }

    /**
     * @brief Pass 4 — Use-before-def on the flat stream.
     *
     * In a topologically-ordered flat IR, every vreg operand must be produced
     * by an earlier node.  Operand IDs that are 0 or INVALID_SLOT are unused
     * slots and are not checked.
     *
     * Exemptions:
     *   - NOP tombstones: their op fields are stale and must not be interpreted
     *     as virtual register references.
     *   - PHI_NODE: operands may be loop-carried (forward) definitions — exempted
     *     in full by definition; the full dominance check is in SSAVerifier.
     *   - LOAD_IMM / IMMEDIATE: op_a = high 32-bits of immediate,
     *     op_b = low 32-bits of immediate — raw data, NOT vreg IDs.
     *     Checking them as vregs produces a false positive for every constant.
     */
    void check_use_before_def_flat(const IRStream& stream,
                                   std::vector<IRVerifyDiagnostic>& diags,
                                   const IRVerifyOptions& opts = {}) const {
        const size_t n = stream.size();
        constexpr size_t kFastCapacity = 16384;
        constexpr size_t kFastMaxLoad = (kFastCapacity * 3u) / 4u;
        constexpr size_t kOverflowCapacity = 131072;
        constexpr size_t kOverflowMaxLoad = (kOverflowCapacity * 3u) / 4u;

        if (n <= kFastMaxLoad) {
            dgtl::utils::FastFlatMap<uint32_t, uint8_t, kFastCapacity> defined;

            for (size_t i = 0; i < n && !capped(diags, opts); ++i) {
                const IRNode nd = stream.get_node(i);

                if (nd.opcode == OpCode::NOP) {
                    if (nd.result_id != INVALID_SLOT && nd.result_id != 0)
                        defined.insert_or_assign(nd.result_id, static_cast<uint8_t>(1));
                    continue;
                }

                const bool skip_operands =
                    nd.opcode == OpCode::PHI_NODE ||
                    uses_immediate_in_operands(nd.opcode);

                if (!skip_operands) {
                    auto chk = [&](uint32_t id, const char* slot) {
                        if (id == INVALID_SLOT || id == 0) return;
                        if (!defined.contains(id)) {
                            push(diags, {
                                IRVerifyDiagnostic::Kind::UseBeforeDef,
                                u32(i), id,
                                "Node[" + s(i) + "] opcode=" +
                                std::string(opcode_name(nd.opcode)) +
                                ": operand " + slot + " (id=" + std::to_string(id) +
                                ") used before any definition in flat stream"
                            });
                        }
                    };
                    chk(nd.op_a, "op_a");
                    chk(nd.op_b, "op_b");
                    chk(nd.op_c, "op_c");
                }

                if (nd.result_id != INVALID_SLOT && nd.result_id != 0)
                    defined.insert_or_assign(nd.result_id, static_cast<uint8_t>(1));
            }
            return;
        }

        if (n > kOverflowMaxLoad) {
            push(diags, {
                IRVerifyDiagnostic::Kind::InternalInvariantViolation,
                0u, INVALID_SLOT,
                "Verifier use-before-def set capacity exceeded"
            });
            return;
        }

        dgtl::utils::FastFlatMap<uint32_t, uint8_t, kOverflowCapacity> defined;

        for (size_t i = 0; i < n && !capped(diags, opts); ++i) {
            const IRNode nd = stream.get_node(i);

            if (nd.opcode == OpCode::NOP) {
                // Tombstone: do not interpret stale op fields as vreg uses,
                // but DO register a non-zero result_id if present.
                if (nd.result_id != INVALID_SLOT && nd.result_id != 0)
                    (void)defined.insert_or_assign(nd.result_id, static_cast<uint8_t>(1));
                continue;
            }

            const bool skip_operands =
                nd.opcode == OpCode::PHI_NODE ||           // loop-carried defs allowed
                uses_immediate_in_operands(nd.opcode);     // op_a/op_b are raw immediates

            if (!skip_operands) {
                auto chk = [&](uint32_t id, const char* slot) {
                    if (id == INVALID_SLOT || id == 0) return;
                    if (!defined.contains(id)) {
                        push(diags, {
                            IRVerifyDiagnostic::Kind::UseBeforeDef,
                            u32(i), id,
                            "Node[" + s(i) + "] opcode=" +
                            std::string(opcode_name(nd.opcode)) +
                            ": operand " + slot + " (id=" + std::to_string(id) +
                            ") used before any definition in flat stream"
                        });
                    }
                };
                chk(nd.op_a, "op_a");
                chk(nd.op_b, "op_b");
                chk(nd.op_c, "op_c");
            }

            if (nd.result_id != INVALID_SLOT && nd.result_id != 0)
                (void)defined.insert_or_assign(nd.result_id, static_cast<uint8_t>(1));
        }
    }

    /**
     * @brief Pass 5 — Ownership lifecycle verification.
     *
     * Tracks OWNERSHIP_ACQUIRE / OWNERSHIP_RELEASE / OWNERSHIP_TRANSFER:
     *
     *   R1. ACQUIRE with no matching RELEASE at end-of-stream → resource leak.
     *   R2. RELEASE of a handle never ACQUIRE'd → orphan release.
     *   R3. RELEASE of an already-RELEASE'd handle → double-free.
     *   R4. TRANSFER relinquishes ownership; receiver manages the handle thereafter.
     *
     * Resource identity = result_id of the ACQUIRE node (the virtual handle register).
     */
    void check_ownership_lifecycle(const IRStream& stream,
                                   std::vector<IRVerifyDiagnostic>& diags,
                                   const IRVerifyOptions& opts = {}) const {
        const size_t n = stream.size();
        constexpr size_t kFastCapacity = 16384;
        constexpr size_t kFastMaxLoad = (kFastCapacity * 3u) / 4u;
        constexpr size_t kOverflowCapacity = 131072;
        constexpr size_t kOverflowMaxLoad = (kOverflowCapacity * 3u) / 4u;

        if (n <= kFastMaxLoad) {
            dgtl::utils::FastFlatMap<uint32_t, uint32_t, kFastCapacity> live;
            dgtl::utils::FastFlatMap<uint32_t, uint32_t, kFastCapacity> released;

            for (size_t i = 0; i < n; ++i) {
                const IRNode nd = stream.get_node(i);
                const uint32_t idx = u32(i);

                switch (nd.opcode) {
                case OpCode::OWNERSHIP_ACQUIRE:
                    if (nd.result_id != INVALID_SLOT && nd.result_id != 0)
                        live.insert_or_assign(nd.result_id, idx);
                    break;

                case OpCode::OWNERSHIP_RELEASE: {
                    const uint32_t h = nd.op_a;
                    if (h == INVALID_SLOT || h == 0) break;
                    if (live.contains(h)) {
                        live.erase(h);
                        released.insert_or_assign(h, idx);
                    } else if (const uint32_t* first_release = released.find(h); first_release != nullptr) {
                        if (capped(diags, opts)) break;
                        push(diags, {
                            IRVerifyDiagnostic::Kind::OwnershipViolation, idx, h,
                            "Node[" + s(i) + "]: OWNERSHIP_RELEASE of handle " +
                            std::to_string(h) + " is a double-release"
                            " (first release at node[" + std::to_string(*first_release) + "])"
                        });
                    } else {
                        if (capped(diags, opts)) break;
                        push(diags, {
                            IRVerifyDiagnostic::Kind::OwnershipViolation, idx, h,
                            "Node[" + s(i) + "]: OWNERSHIP_RELEASE of handle " +
                            std::to_string(h) +
                            " has no prior OWNERSHIP_ACQUIRE in this stream"
                        });
                    }
                    break;
                }

                case OpCode::OWNERSHIP_TRANSFER: {
                    const uint32_t h = nd.op_a;
                    if (h != INVALID_SLOT && h != 0)
                        live.erase(h);
                    break;
                }

                default: break;
                }
            }

            for (const auto& [h, acq_idx] : live) {
                if (capped(diags, opts)) break;
                push(diags, {
                    IRVerifyDiagnostic::Kind::OwnershipViolation,
                    acq_idx, h,
                    "Node[" + std::to_string(acq_idx) + "]: OWNERSHIP_ACQUIRE of handle " +
                    std::to_string(h) +
                    " has no matching OWNERSHIP_RELEASE (resource leak)"
                });
            }
            return;
        }

        if (n > kOverflowMaxLoad) {
            push(diags, {
                IRVerifyDiagnostic::Kind::InternalInvariantViolation,
                0u, INVALID_SLOT,
                "Verifier ownership maps capacity exceeded"
            });
            return;
        }

        dgtl::utils::FastFlatMap<uint32_t, uint32_t, kOverflowCapacity> live;
        dgtl::utils::FastFlatMap<uint32_t, uint32_t, kOverflowCapacity> released;

        for (size_t i = 0; i < n; ++i) {
            const IRNode nd = stream.get_node(i);
            const uint32_t idx = u32(i);

            switch (nd.opcode) {
            case OpCode::OWNERSHIP_ACQUIRE:
                if (nd.result_id != INVALID_SLOT && nd.result_id != 0)
                    (void)live.insert_or_assign(nd.result_id, idx);
                break;

            case OpCode::OWNERSHIP_RELEASE: {
                const uint32_t h = nd.op_a;
                if (h == INVALID_SLOT || h == 0) break; // caught by opcode constraints
                if (live.contains(h)) {
                    live.erase(h);
                    (void)released.insert_or_assign(h, idx);
                } else if (const uint32_t* rel_idx = released.find(h); rel_idx != nullptr) {
                    if (capped(diags, opts)) break;
                    push(diags, {
                        IRVerifyDiagnostic::Kind::OwnershipViolation, idx, h,
                        "Node[" + s(i) + "]: OWNERSHIP_RELEASE of handle " +
                        std::to_string(h) + " is a double-release"
                        " (first release at node[" + std::to_string(*rel_idx) + "])"
                    });
                } else {
                    if (capped(diags, opts)) break;
                    push(diags, {
                        IRVerifyDiagnostic::Kind::OwnershipViolation, idx, h,
                        "Node[" + s(i) + "]: OWNERSHIP_RELEASE of handle " +
                        std::to_string(h) +
                        " has no prior OWNERSHIP_ACQUIRE in this stream"
                    });
                }
                break;
            }

            case OpCode::OWNERSHIP_TRANSFER: {
                const uint32_t h = nd.op_a;
                if (h != INVALID_SLOT && h != 0)
                    live.erase(h); // receiver now owns it — remove from this stream's live set
                break;
            }

            default: break;
            }
        }

        // R1: handles still live at end-of-stream were never released.
        for (const auto& [h, acq_idx] : live) {
            if (capped(diags, opts)) break;
            push(diags, {
                IRVerifyDiagnostic::Kind::OwnershipViolation,
                acq_idx, h,
                "Node[" + std::to_string(acq_idx) + "]: OWNERSHIP_ACQUIRE of handle " +
                std::to_string(h) +
                " has no matching OWNERSHIP_RELEASE (resource leak)"
            });
        }
    }

    /**
     * @brief Pass 6 — Lock lifecycle verification.
     *
     * Tracks LOCK_ACQUIRE / LOCK_RELEASE pairs:
     *
     *   L1. LOCK_ACQUIRE with no LOCK_RELEASE at end-of-stream → lock never released
     *       (potential deadlock on re-entry or cross-thread access).
     *   L2. LOCK_RELEASE of a handle never acquired → unlock-without-lock.
     *   L3. LOCK_RELEASE of an already-unlocked handle → double-unlock.
     *
     * Lock identity = result_id of the LOCK_ACQUIRE node.
     */
    void check_lock_lifecycle(const IRStream& stream,
                              std::vector<IRVerifyDiagnostic>& diags,
                              const IRVerifyOptions& opts = {}) const {
        const size_t n = stream.size();
        constexpr size_t kFastCapacity = 16384;
        constexpr size_t kFastMaxLoad = (kFastCapacity * 3u) / 4u;
        constexpr size_t kOverflowCapacity = 131072;
        constexpr size_t kOverflowMaxLoad = (kOverflowCapacity * 3u) / 4u;

        if (n <= kFastMaxLoad) {
            dgtl::utils::FastFlatMap<uint32_t, uint32_t, kFastCapacity> held;
            dgtl::utils::FastFlatMap<uint32_t, uint32_t, kFastCapacity> unlocked;

            for (size_t i = 0; i < n; ++i) {
                const IRNode nd = stream.get_node(i);
                const uint32_t idx = u32(i);

                switch (nd.opcode) {
                case OpCode::LOCK_ACQUIRE:
                    if (nd.result_id != INVALID_SLOT && nd.result_id != 0)
                        held.insert_or_assign(nd.result_id, idx);
                    break;

                case OpCode::LOCK_RELEASE: {
                    const uint32_t h = nd.op_a;
                    if (h == INVALID_SLOT || h == 0) break;
                    if (held.contains(h)) {
                        held.erase(h);
                        unlocked.insert_or_assign(h, idx);
                    } else if (const uint32_t* first_unlock = unlocked.find(h); first_unlock != nullptr) {
                        if (capped(diags, opts)) break;
                        push(diags, {
                            IRVerifyDiagnostic::Kind::LockViolation, idx, h,
                            "Node[" + s(i) + "]: LOCK_RELEASE of handle " +
                            std::to_string(h) + " is a double-unlock"
                            " (first unlock at node[" + std::to_string(*first_unlock) + "])"
                        });
                    } else {
                        if (capped(diags, opts)) break;
                        push(diags, {
                            IRVerifyDiagnostic::Kind::LockViolation, idx, h,
                            "Node[" + s(i) + "]: LOCK_RELEASE of handle " +
                            std::to_string(h) +
                            " has no prior LOCK_ACQUIRE (unlock-without-lock)"
                        });
                    }
                    break;
                }

                default: break;
                }
            }

            for (const auto& [h, acq_idx] : held) {
                if (capped(diags, opts)) break;
                push(diags, {
                    IRVerifyDiagnostic::Kind::LockViolation,
                    acq_idx, h,
                    "Node[" + std::to_string(acq_idx) + "]: LOCK_ACQUIRE of handle " +
                    std::to_string(h) +
                    " has no matching LOCK_RELEASE (potential deadlock)"
                });
            }
            return;
        }

        if (n > kOverflowMaxLoad) {
            push(diags, {
                IRVerifyDiagnostic::Kind::InternalInvariantViolation,
                0u, INVALID_SLOT,
                "Verifier lock maps capacity exceeded"
            });
            return;
        }

        dgtl::utils::FastFlatMap<uint32_t, uint32_t, kOverflowCapacity> held;
        dgtl::utils::FastFlatMap<uint32_t, uint32_t, kOverflowCapacity> unlocked;

        for (size_t i = 0; i < n; ++i) {
            const IRNode nd = stream.get_node(i);
            const uint32_t idx = u32(i);

            switch (nd.opcode) {
            case OpCode::LOCK_ACQUIRE:
                if (nd.result_id != INVALID_SLOT && nd.result_id != 0)
                    (void)held.insert_or_assign(nd.result_id, idx);
                break;

            case OpCode::LOCK_RELEASE: {
                const uint32_t h = nd.op_a;
                if (h == INVALID_SLOT || h == 0) break;
                if (held.contains(h)) {
                    held.erase(h);
                    (void)unlocked.insert_or_assign(h, idx);
                } else if (const uint32_t* unlock_idx = unlocked.find(h); unlock_idx != nullptr) {
                    if (capped(diags, opts)) break;
                    push(diags, {
                        IRVerifyDiagnostic::Kind::LockViolation, idx, h,
                        "Node[" + s(i) + "]: LOCK_RELEASE of handle " +
                        std::to_string(h) + " is a double-unlock"
                        " (first unlock at node[" + std::to_string(*unlock_idx) + "])"
                    });
                } else {
                    if (capped(diags, opts)) break;
                    push(diags, {
                        IRVerifyDiagnostic::Kind::LockViolation, idx, h,
                        "Node[" + s(i) + "]: LOCK_RELEASE of handle " +
                        std::to_string(h) +
                        " has no prior LOCK_ACQUIRE (unlock-without-lock)"
                    });
                }
                break;
            }

            default: break;
            }
        }

        // L1: locks still held at end-of-stream.
        for (const auto& [h, acq_idx] : held) {
            if (capped(diags, opts)) break;
            push(diags, {
                IRVerifyDiagnostic::Kind::LockViolation,
                acq_idx, h,
                "Node[" + std::to_string(acq_idx) + "]: LOCK_ACQUIRE of handle " +
                std::to_string(h) +
                " has no matching LOCK_RELEASE (potential deadlock)"
            });
        }
    }

    /**
     * @brief Pass 7 — Opcode-specific semantic constraint checks.
     *
     * Per-opcode invariants enforced:
     *
     *   GOTO / BRANCH / BRANCH_COMPARE_FUSED
     *       op_a must be valid (jump/branch target label required).
     *
     *   BRANCH_COND
     *       op_a (true-target) AND op_b (false-target) must both be valid.
     *
     *   CALL
     *       op_a (callee slot) must be valid.
     *
     *   SYSCALL
     *       op_a (syscall number) must be valid.
     *
     *   STORE / STORE_V512 / VEC_STORE
     *       op_a (address/base) AND op_b (value being stored) must both be valid.
     *       A STORE with an invalid address silently writes to an undefined location.
     *
     *   FMA_F32 / FMA_WIDEN_F16_F32
     *       op_a, op_b, AND op_c must all be valid (result = op_a * op_b + op_c).
     *
     *   PHI_NODE
     *       At least one of op_a / op_b must be a valid incoming value.
     *       (> 2 operands are stored in the extended phi table, not in IRNode fields.)
     *
     *   OWNERSHIP_RELEASE
     *       op_a (resource handle) must be valid.
     *
     *   OWNERSHIP_TRANSFER
     *       op_a (handle to transfer) must be valid.
     *
     *   LOCK_RELEASE
     *       op_a (lock handle) must be valid.
     *
     *   GUARD_VALUE / ASSUME
     *       op_a (guarded value) must be valid.
     *
     *   DEOPT_TRIGGER
     *       op_a (deopt reason code) must be valid.
     */
    void check_opcode_constraints(const IRStream& stream,
                                  std::vector<IRVerifyDiagnostic>& diags,
                                  const IRVerifyOptions& opts = {}) const {
        const size_t n = stream.size();
        for (size_t i = 0; i < n && !capped(diags, opts); ++i) {
            const IRNode nd = stream.get_node(i);
            if (nd.opcode == OpCode::NOP) continue;
            const uint32_t idx = u32(i);

            switch (nd.opcode) {

            // ---- Control flow ----
            case OpCode::GOTO:
            case OpCode::BRANCH:
            case OpCode::BRANCH_COMPARE_FUSED:
                if (nd.op_a == INVALID_SLOT) {
                    push(diags, {
                        IRVerifyDiagnostic::Kind::OpcodeConstraint, idx, INVALID_SLOT,
                        "Node[" + s(i) + "] opcode=" +
                        std::string(opcode_name(nd.opcode)) +
                        ": op_a (branch/jump target label) must not be INVALID_SLOT"
                    });
                }
                break;

            case OpCode::BRANCH_COND:
                if (nd.op_a == INVALID_SLOT) {
                    push(diags, {
                        IRVerifyDiagnostic::Kind::OpcodeConstraint, idx, INVALID_SLOT,
                        "Node[" + s(i) + "] opcode=BRANCH_COND"
                        ": op_a (true-target label) must not be INVALID_SLOT"
                    });
                }
                if (nd.op_b == INVALID_SLOT) {
                    push(diags, {
                        IRVerifyDiagnostic::Kind::OpcodeConstraint, idx, INVALID_SLOT,
                        "Node[" + s(i) + "] opcode=BRANCH_COND"
                        ": op_b (false-target label) must not be INVALID_SLOT"
                    });
                }
                break;

            // ---- Calls ----
            case OpCode::CALL:
                if (nd.op_a == INVALID_SLOT) {
                    push(diags, {
                        IRVerifyDiagnostic::Kind::OpcodeConstraint, idx, INVALID_SLOT,
                        "Node[" + s(i) + "] opcode=CALL"
                        ": op_a (callee slot) must not be INVALID_SLOT"
                    });
                }
                break;

            case OpCode::SYSCALL:
                if (nd.op_a == INVALID_SLOT) {
                    push(diags, {
                        IRVerifyDiagnostic::Kind::OpcodeConstraint, idx, INVALID_SLOT,
                        "Node[" + s(i) + "] opcode=SYSCALL"
                        ": op_a (syscall number) must not be INVALID_SLOT"
                    });
                }
                break;

            // ---- Memory stores: both address and value required ----
            case OpCode::STORE:
            case OpCode::STORE_V512:
            case OpCode::VEC_STORE:
                if (nd.op_a == INVALID_SLOT) {
                    push(diags, {
                        IRVerifyDiagnostic::Kind::OpcodeConstraint, idx, INVALID_SLOT,
                        "Node[" + s(i) + "] opcode=" +
                        std::string(opcode_name(nd.opcode)) +
                        ": op_a (store address/base) must not be INVALID_SLOT"
                    });
                }
                if (nd.op_b == INVALID_SLOT) {
                    push(diags, {
                        IRVerifyDiagnostic::Kind::OpcodeConstraint, idx, INVALID_SLOT,
                        "Node[" + s(i) + "] opcode=" +
                        std::string(opcode_name(nd.opcode)) +
                        ": op_b (value to store) must not be INVALID_SLOT"
                    });
                }
                break;

            // ---- FMA: all three operands required (result = a*b + c) ----
            case OpCode::FMA_F32:
            case OpCode::FMA_WIDEN_F16_F32: {
                std::string_view nm = opcode_name(nd.opcode);
                if (nd.op_a == INVALID_SLOT) {
                    push(diags, {
                        IRVerifyDiagnostic::Kind::OpcodeConstraint, idx, INVALID_SLOT,
                        "Node[" + s(i) + "] opcode=" + std::string(nm) +
                        ": op_a (multiplicand a) must not be INVALID_SLOT"
                    });
                }
                if (nd.op_b == INVALID_SLOT) {
                    push(diags, {
                        IRVerifyDiagnostic::Kind::OpcodeConstraint, idx, INVALID_SLOT,
                        "Node[" + s(i) + "] opcode=" + std::string(nm) +
                        ": op_b (multiplicand b) must not be INVALID_SLOT"
                    });
                }
                if (nd.op_c == INVALID_SLOT) {
                    push(diags, {
                        IRVerifyDiagnostic::Kind::OpcodeConstraint, idx, INVALID_SLOT,
                        "Node[" + s(i) + "] opcode=" + std::string(nm) +
                        ": op_c (addend c) must not be INVALID_SLOT"
                        " — FMA requires all three operands (result = a*b + c)"
                    });
                }
                break;
            }

            // ---- SSA ----
            case OpCode::PHI_NODE:
                if (nd.op_a == INVALID_SLOT && nd.op_b == INVALID_SLOT) {
                    push(diags, {
                        IRVerifyDiagnostic::Kind::OpcodeConstraint, idx, INVALID_SLOT,
                        "Node[" + s(i) + "] opcode=PHI_NODE"
                        ": at least one of op_a/op_b must be a valid incoming value"
                    });
                }
                break;

            // ---- Ownership ----
            case OpCode::OWNERSHIP_RELEASE:
                if (nd.op_a == INVALID_SLOT) {
                    push(diags, {
                        IRVerifyDiagnostic::Kind::OpcodeConstraint, idx, INVALID_SLOT,
                        "Node[" + s(i) + "] opcode=OWNERSHIP_RELEASE"
                        ": op_a (resource handle) must not be INVALID_SLOT"
                    });
                }
                break;

            case OpCode::OWNERSHIP_TRANSFER:
                if (nd.op_a == INVALID_SLOT) {
                    push(diags, {
                        IRVerifyDiagnostic::Kind::OpcodeConstraint, idx, INVALID_SLOT,
                        "Node[" + s(i) + "] opcode=OWNERSHIP_TRANSFER"
                        ": op_a (handle to transfer) must not be INVALID_SLOT"
                    });
                }
                break;

            // ---- Locks ----
            case OpCode::LOCK_RELEASE:
                if (nd.op_a == INVALID_SLOT) {
                    push(diags, {
                        IRVerifyDiagnostic::Kind::OpcodeConstraint, idx, INVALID_SLOT,
                        "Node[" + s(i) + "] opcode=LOCK_RELEASE"
                        ": op_a (lock handle) must not be INVALID_SLOT"
                    });
                }
                break;

            // ---- Speculation / guards ----
            case OpCode::GUARD_VALUE:
            case OpCode::ASSUME:
                if (nd.op_a == INVALID_SLOT) {
                    push(diags, {
                        IRVerifyDiagnostic::Kind::OpcodeConstraint, idx, INVALID_SLOT,
                        "Node[" + s(i) + "] opcode=" +
                        std::string(opcode_name(nd.opcode)) +
                        ": op_a (guarded value) must not be INVALID_SLOT"
                    });
                }
                break;

            case OpCode::DEOPT_TRIGGER:
                if (nd.op_a == INVALID_SLOT) {
                    push(diags, {
                        IRVerifyDiagnostic::Kind::OpcodeConstraint, idx, INVALID_SLOT,
                        "Node[" + s(i) + "] opcode=DEOPT_TRIGGER"
                        ": op_a (deopt reason code) must not be INVALID_SLOT"
                    });
                }
                break;

            default:
                break;
            }
        }
    }

private:
    // ----------------------------------------------------------------
    // INTERNAL HELPERS
    // ----------------------------------------------------------------

    /**
     * @brief Returns true for opcodes that write a meaningful value into result_id.
     *
     * Conservative: unrecognised opcodes (custom dialect >= 0x8000, future
     * extensions) return false to avoid false-positive structural errors.
     */
    [[nodiscard]] static constexpr bool is_value_producing(OpCode op) noexcept {
        switch (op) {
        // ---- Memory loads ----
        case OpCode::LOAD:
        case OpCode::REG_MOVE:            // alias MOV — moves a value into result_id
        case OpCode::LOAD_IMM:            // alias CONST
        case OpCode::LEA:
        case OpCode::IMMEDIATE:           // folded constant

        // ---- Integer i32 ----
        case OpCode::ADD_I32:
        case OpCode::SUB_I32:
        case OpCode::MUL_I32:
        case OpCode::DIV_I32:
        case OpCode::SHL_I32:
        case OpCode::AND_I32:
        case OpCode::OR_I32:
        case OpCode::XOR_I32:
        case OpCode::MOD_I32:
        case OpCode::SHR_I32:

        // ---- Integer i64 ----
        case OpCode::ADD_I64:
        case OpCode::SUB_I64:
        case OpCode::MUL_I64:
        case OpCode::DIV_I64:

        // ---- Float f32 ----
        case OpCode::ADD_F32:
        case OpCode::SUB_F32:
        case OpCode::MUL_F32:
        case OpCode::DIV_F32:
        case OpCode::FMA_F32:
        case OpCode::FMA_WIDEN_F16_F32:

        // ---- Float f64 ----
        case OpCode::ADD_F64:
        case OpCode::SUB_F64:
        case OpCode::MUL_F64:
        case OpCode::DIV_F64:
        case OpCode::MOD_I64:

        // ---- Compare (boolean result) ----
        case OpCode::CMP_EQ_I32:
        case OpCode::CMP_NE_I32:
        case OpCode::CMP_LT_I32:
        case OpCode::CMP_LE_I32:
        case OpCode::CMP_GT_I32:
        case OpCode::CMP_GE_I32:

        // ---- Bitfield ----
        case OpCode::BIT_FIELD_INSERT:
        case OpCode::BIT_FIELD_EXTRACT:

        // ---- SIMD (value-producing) ----
        case OpCode::GATHER_F32_512:
        case OpCode::GATHER_16:
        case OpCode::VEC_LOAD:
        case OpCode::VEC_ADD_F32_512:
        case OpCode::VEC_MUL_F32_512:
        case OpCode::VEC_SHUFFLE:
        case OpCode::VEC_REDUCE_ADD:
        case OpCode::FCVTL_F16_TO_F32:
        case OpCode::FCVTN_F32_TO_F16:
        case OpCode::SIMD_BSL_PREDICATE:  // produces predicated SIMD result

        // ---- SSA ----
        case OpCode::PHI_NODE:

        // ---- Speculation ----
        case OpCode::GUARD_VALUE:
        case OpCode::ASSUME:

        // ---- Ownership ----
        case OpCode::OWNERSHIP_ACQUIRE:
        case OpCode::ATOMIC_SNAPSHOT:

        // ---- Locks (produce a lock-handle register) ----
        case OpCode::LOCK_ACQUIRE:

        // ---- Concurrency (channel receive produces a received value) ----
        case OpCode::CHAN_RECV:

        // ---- Tensor — all produce a transformed/reduced tensor ----
        case OpCode::TENSOR_RESHAPE:
        case OpCode::TENSOR_REDUCE:
        case OpCode::TENSOR_TRANSPOSE:
        case OpCode::TENSOR_SLICE:
        case OpCode::TENSOR_CONCAT:
        case OpCode::TENSOR_BROADCAST:
        case OpCode::TENSOR_GEMM:
        case OpCode::TENSOR_CONV:
        case OpCode::TENSOR_POOL:
        case OpCode::TENSOR_SOFTMAX:

        // ---- DSA (build/traverse/lookup all produce handles or results) ----
        case OpCode::GRAPH_BUILD:
        case OpCode::GRAPH_TRAVERSE:
        case OpCode::TREE_BUILD:
        case OpCode::TREE_TRAVERSE:
        case OpCode::TRIE_BUILD:
        case OpCode::TRIE_LOOKUP:
        case OpCode::TRIE_INSERT:
        case OpCode::DSA_ALLOCATE:

        // ---- Call ----
        case OpCode::CALL:
            return true;

        // ---- Void opcodes (not value-producing) ----
        // NOP, STORE, STORE_V512, VEC_STORE, PREFETCH,
        // GOTO, LABEL, RET/RETURN, BRANCH, BRANCH_COND, BRANCH_COMPARE_FUSED,
        // SYSCALL, GPU_LAUNCH, ASYNC_LAUNCH, SYNC_BARRIER,
        // OWNERSHIP_RELEASE, OWNERSHIP_TRANSFER,
        // ASYNC_SPAWN, ASYNC_AWAIT, FIBER_YIELD, BARRIER_SYNC,
        // CHAN_SEND, LOCK_RELEASE, MEMORY_FENCE, THREAD_SPAWN,
        // DSA_DEALLOCATE, SCOP_ANALYZE, REGION_BEGIN, REGION_END, FUSION_GROUP,
        // DEOPT_TRIGGER, GUARD_ALIAS.
        // Custom dialect ops (>= CUSTOM_DIALECT_START = 0x8000) are conservatively
        // treated as void to avoid false positives on unknown extensions.
        default:
            return false;
        }
    }

    /**
     * @brief Returns true for opcodes that encode raw immediate data in op_a/op_b,
     *        NOT virtual register IDs.
     *
     * LOAD_IMM  (13): op_a = high 32-bits of immediate, op_b = low 32-bits
     * IMMEDIATE (80): same encoding — folded constant form
     *
     * The use-before-def pass must skip operand checking for these opcodes to
     * avoid false positives on every constant in the stream.
     */
    [[nodiscard]] static constexpr bool uses_immediate_in_operands(OpCode op) noexcept {
        return op == OpCode::LOAD_IMM || op == OpCode::IMMEDIATE;
    }

    // ---- Tiny helpers to reduce boilerplate ----

    [[nodiscard]] static bool capped(const std::vector<IRVerifyDiagnostic>& diags,
                                     const IRVerifyOptions& opts) noexcept {
        return opts.max_diagnostics > 0 &&
               diags.size() >= static_cast<size_t>(opts.max_diagnostics);
    }

    static void push(std::vector<IRVerifyDiagnostic>& diags, IRVerifyDiagnostic d) {
        diags.push_back(std::move(d));
    }

    [[nodiscard]] static std::string s(size_t i) {
        return std::to_string(i);
    }

    [[nodiscard]] static uint32_t u32(size_t i) noexcept {
        return static_cast<uint32_t>(i);
    }

    /// Format a uint64_t as a fixed 16-char lowercase hex string (no "0x" prefix).
    [[nodiscard]] static std::string hex64(uint64_t v) {
        constexpr const char* kD = "0123456789abcdef";
        char buf[17];
        buf[16] = '\0';
        for (int k = 15; k >= 0; --k, v >>= 4) buf[k] = kD[v & 0xFU];
        return std::string(buf);
    }
};

// ============================================================================
// SECTION: DIAGNOSTIC UTILITY FUNCTIONS
// ============================================================================

/// Return all diagnostics matching the given kind.
[[nodiscard]] inline std::vector<IRVerifyDiagnostic>
ir_filter_by_kind(const std::vector<IRVerifyDiagnostic>& diags,
                  IRVerifyDiagnostic::Kind kind) {
    std::vector<IRVerifyDiagnostic> out;
    for (const auto& d : diags)
        if (d.kind == kind) out.push_back(d);
    return out;
}

/// Return all diagnostics at the given severity level.
[[nodiscard]] inline std::vector<IRVerifyDiagnostic>
ir_filter_by_severity(const std::vector<IRVerifyDiagnostic>& diags,
                      IRVerifyDiagnostic::Severity sev) {
    std::vector<IRVerifyDiagnostic> out;
    for (const auto& d : diags)
        if (d.severity == sev) out.push_back(d);
    return out;
}

/// Count diagnostics with Severity::Error.
[[nodiscard]] inline uint32_t
ir_count_errors(const std::vector<IRVerifyDiagnostic>& diags) noexcept {
    uint32_t n = 0;
    for (const auto& d : diags)
        if (d.severity == IRVerifyDiagnostic::Severity::Error) ++n;
    return n;
}

/// Count diagnostics with Severity::Warning.
[[nodiscard]] inline uint32_t
ir_count_warnings(const std::vector<IRVerifyDiagnostic>& diags) noexcept {
    uint32_t n = 0;
    for (const auto& d : diags)
        if (d.severity == IRVerifyDiagnostic::Severity::Warning) ++n;
    return n;
}

// ============================================================================
// SECTION: FREE-FUNCTION CONVENIENCE WRAPPERS
// ============================================================================

/**
 * @brief One-shot stream verification.  Returns all diagnostics.
 *
 * Equivalent to `IRVerifier{}.verify(stream, opts)`.
 */
[[nodiscard]] inline std::vector<IRVerifyDiagnostic>
ir_verify(const IRStream& stream, const IRVerifyOptions& opts = {}) {
    return IRVerifier{}.verify(stream, opts);
}

/**
 * @brief One-shot validity gate.  Returns false if any Error-level violation is found.
 *
 * Warnings do NOT cause this to return false.
 */
[[nodiscard]] inline bool
ir_is_valid(const IRStream& stream, const IRVerifyOptions& opts = {}) noexcept {
    return IRVerifier{}.is_valid(stream, opts);
}

} // namespace dgtl::ir
