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

/* * DGTL (Deep General-purpose Transformation Layer) version 1.0.0 : THE COMPILER SPEC
 * STEP 2: BIT-PACKED INTERMEDIATE REPRESENTATION (BIR) - VERSION 3.0 (TITAN-STABLE)
 * PILLAR 4: IR STREAM CORE IMPLEMENTATION WITH ARENA-BACKED SOA
 * DESIGN: 64-Byte Aligned Transactions for HFT-Grade Optimization Stability
 * STANDARDS: C++26 / UCRT64 (Zero-Overhead Primitive Mapping)
 */

#pragma once

#include "IR_Node.hpp"
#include "IR_Traits_Static.hpp"
#include "IR_Stream.hpp"
// IR_SSA.hpp is optional; include it only if SSA-aware printing is needed.
#include <cstddef>
#include <cstdint>
#include <cassert>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <iomanip>
#include <cstring>   // std::memcpy in IRSerializer I/O helpers

// ============================================================================
// ============================================================================
// SECTION: IR PRINTER Â· IR SERIALIZER Â· MIR DUMPER
// ============================================================================
// ============================================================================

namespace dgtl::ir {

// ============================================================================
// OPCODE NAME TABLE
// Alias enumerators (MOV=REG_MOVE=12, CONST=LOAD_IMM=13, RETURN=RET=101)
// resolve to the primary name â€” C++ switch forbids duplicate case labels.
// ============================================================================

[[nodiscard]] inline std::string_view opcode_name(OpCode op) noexcept {
    switch (op) {
        // Memory
        case OpCode::NOP:                   return "NOP";
        case OpCode::LOAD:                  return "LOAD";
        case OpCode::STORE:                 return "STORE";
        case OpCode::REG_MOVE:              return "REG_MOVE";     // alias: MOV
        case OpCode::LOAD_IMM:              return "LOAD_IMM";     // alias: CONST
        case OpCode::LEA:                   return "LEA";
        case OpCode::PREFETCH:              return "PREFETCH";
        case OpCode::IMMEDIATE:             return "IMMEDIATE";
        // Integer i32
        case OpCode::ADD_I32:               return "ADD_I32";
        case OpCode::SUB_I32:               return "SUB_I32";
        case OpCode::MUL_I32:               return "MUL_I32";
        case OpCode::DIV_I32:               return "DIV_I32";
        case OpCode::SHL_I32:               return "SHL_I32";
        case OpCode::AND_I32:               return "AND_I32";
        case OpCode::OR_I32:                return "OR_I32";
        case OpCode::XOR_I32:               return "XOR_I32";
        case OpCode::MOD_I32:               return "MOD_I32";
        case OpCode::SHR_I32:               return "SHR_I32";
        // Integer i64
        case OpCode::ADD_I64:               return "ADD_I64";
        case OpCode::SUB_I64:               return "SUB_I64";
        case OpCode::MUL_I64:               return "MUL_I64";
        case OpCode::DIV_I64:               return "DIV_I64";
        // Float f32
        case OpCode::ADD_F32:               return "ADD_F32";
        case OpCode::SUB_F32:               return "SUB_F32";
        case OpCode::MUL_F32:               return "MUL_F32";
        case OpCode::DIV_F32:               return "DIV_F32";
        case OpCode::FMA_F32:               return "FMA_F32";
        case OpCode::FMA_WIDEN_F16_F32:     return "FMA_WIDEN_F16_F32";
        // Float f64
        case OpCode::ADD_F64:               return "ADD_F64";
        case OpCode::SUB_F64:               return "SUB_F64";
        case OpCode::MUL_F64:               return "MUL_F64";
        case OpCode::DIV_F64:               return "DIV_F64";
        // Compare
        case OpCode::CMP_EQ_I32:            return "CMP_EQ_I32";
        case OpCode::CMP_NE_I32:            return "CMP_NE_I32";
        case OpCode::CMP_LT_I32:            return "CMP_LT_I32";
        case OpCode::CMP_LE_I32:            return "CMP_LE_I32";
        case OpCode::CMP_GT_I32:            return "CMP_GT_I32";
        case OpCode::CMP_GE_I32:            return "CMP_GE_I32";
        // Bitfield
        case OpCode::BIT_FIELD_INSERT:      return "BIT_FIELD_INSERT";
        case OpCode::BIT_FIELD_EXTRACT:     return "BIT_FIELD_EXTRACT";
        // SIMD
        case OpCode::GATHER_F32_512:        return "GATHER_F32_512";
        case OpCode::GATHER_16:             return "GATHER_16";
        case OpCode::STORE_V512:            return "STORE_V512";
        case OpCode::VEC_ADD_F32_512:       return "VEC_ADD_F32_512";
        case OpCode::VEC_MUL_F32_512:       return "VEC_MUL_F32_512";
        case OpCode::SIMD_BSL_PREDICATE:    return "SIMD_BSL_PREDICATE";
        case OpCode::FCVTL_F16_TO_F32:      return "FCVTL_F16_TO_F32";
        case OpCode::FCVTN_F32_TO_F16:      return "FCVTN_F32_TO_F16";
        case OpCode::VEC_LOAD:              return "VEC_LOAD";
        case OpCode::VEC_STORE:             return "VEC_STORE";
        case OpCode::VEC_SHUFFLE:           return "VEC_SHUFFLE";
        case OpCode::VEC_REDUCE_ADD:        return "VEC_REDUCE_ADD";
        // Control flow
        case OpCode::LABEL:                 return "LABEL";
        case OpCode::GOTO:                  return "GOTO";
        case OpCode::BRANCH:                return "BRANCH";
        case OpCode::RET:                   return "RET";          // alias: RETURN
        case OpCode::BRANCH_COND:           return "BRANCH_COND";
        case OpCode::BRANCH_COMPARE_FUSED:  return "BRANCH_COMPARE_FUSED";
        case OpCode::CALL:                  return "CALL";
        case OpCode::SYSCALL:               return "SYSCALL";
        case OpCode::GUARD_ALIAS:           return "GUARD_ALIAS";
        // SSA / speculation
        case OpCode::GUARD_VALUE:           return "GUARD_VALUE";
        case OpCode::DEOPT_TRIGGER:         return "DEOPT_TRIGGER";
        case OpCode::PHI_NODE:              return "PHI_NODE";
        case OpCode::ASSUME:                return "ASSUME";
        // Legacy async (deprecated)
        case OpCode::ASYNC_LAUNCH:          return "ASYNC_LAUNCH";
        case OpCode::SYNC_BARRIER:          return "SYNC_BARRIER";
        // GPU
        case OpCode::GPU_LAUNCH:            return "GPU_LAUNCH";
        // Ownership
        case OpCode::OWNERSHIP_ACQUIRE:     return "OWNERSHIP_ACQUIRE";
        case OpCode::OWNERSHIP_RELEASE:     return "OWNERSHIP_RELEASE";
        case OpCode::OWNERSHIP_TRANSFER:    return "OWNERSHIP_TRANSFER";
        case OpCode::ATOMIC_SNAPSHOT:       return "ATOMIC_SNAPSHOT";
        // Concurrency
        case OpCode::ASYNC_SPAWN:           return "ASYNC_SPAWN";
        case OpCode::ASYNC_AWAIT:           return "ASYNC_AWAIT";
        case OpCode::FIBER_YIELD:           return "FIBER_YIELD";
        case OpCode::BARRIER_SYNC:          return "BARRIER_SYNC";
        case OpCode::CHAN_SEND:             return "CHAN_SEND";
        case OpCode::CHAN_RECV:             return "CHAN_RECV";
        case OpCode::LOCK_ACQUIRE:          return "LOCK_ACQUIRE";
        case OpCode::LOCK_RELEASE:          return "LOCK_RELEASE";
        case OpCode::MEMORY_FENCE:          return "MEMORY_FENCE";
        case OpCode::THREAD_SPAWN:          return "THREAD_SPAWN";
        // DSA
        case OpCode::GRAPH_BUILD:           return "GRAPH_BUILD";
        case OpCode::GRAPH_TRAVERSE:        return "GRAPH_TRAVERSE";
        case OpCode::TREE_BUILD:            return "TREE_BUILD";
        case OpCode::TREE_TRAVERSE:         return "TREE_TRAVERSE";
        case OpCode::TRIE_BUILD:            return "TRIE_BUILD";
        case OpCode::TRIE_LOOKUP:           return "TRIE_LOOKUP";
        case OpCode::TRIE_INSERT:           return "TRIE_INSERT";
        case OpCode::DSA_ALLOCATE:          return "DSA_ALLOCATE";
        case OpCode::DSA_DEALLOCATE:        return "DSA_DEALLOCATE";
        case OpCode::SCOP_ANALYZE:          return "SCOP_ANALYZE";
        // Tensor / ML
        case OpCode::TENSOR_RESHAPE:        return "TENSOR_RESHAPE";
        case OpCode::TENSOR_REDUCE:         return "TENSOR_REDUCE";
        case OpCode::TENSOR_TRANSPOSE:      return "TENSOR_TRANSPOSE";
        case OpCode::TENSOR_SLICE:          return "TENSOR_SLICE";
        case OpCode::TENSOR_CONCAT:         return "TENSOR_CONCAT";
        case OpCode::TENSOR_BROADCAST:      return "TENSOR_BROADCAST";
        case OpCode::TENSOR_GEMM:           return "TENSOR_GEMM";
        case OpCode::TENSOR_CONV:           return "TENSOR_CONV";
        case OpCode::TENSOR_POOL:           return "TENSOR_POOL";
        case OpCode::TENSOR_SOFTMAX:        return "TENSOR_SOFTMAX";
        // Pipeline / fusion
        case OpCode::REGION_BEGIN:          return "REGION_BEGIN";
        case OpCode::REGION_END:            return "REGION_END";
        case OpCode::FUSION_GROUP:          return "FUSION_GROUP";
        // Custom dialect ops (non-core ranges)
        case OpCode::DGTL_FAST_ADD:          return "DGTL_FAST_ADD";
        case OpCode::DGTL_FAST_FMA:          return "DGTL_FAST_FMA";
        case OpCode::SIMD_REDUCE_MAX:        return "SIMD_REDUCE_MAX";
        case OpCode::SIMD_REDUCE_MIN:        return "SIMD_REDUCE_MIN";
        case OpCode::SIMD_SCATTER_INDIRECT:  return "SIMD_SCATTER_INDIRECT";
        case OpCode::SIMD_MASKED_STORE:      return "SIMD_MASKED_STORE";
        case OpCode::SIMD_INTERLEAVED_LOAD:  return "SIMD_INTERLEAVED_LOAD";
        case OpCode::SIMD_COMPLEX_FMA:       return "SIMD_COMPLEX_FMA";
        case OpCode::GPU_MEM_TRANSFER:       return "GPU_MEM_TRANSFER";
        case OpCode::GPU_BARRIER:            return "GPU_BARRIER";
        case OpCode::GPU_SYNC_POINT:         return "GPU_SYNC_POINT";
        case OpCode::GPU_KERNEL_FUSE:        return "GPU_KERNEL_FUSE";
        case OpCode::GPU_ANNOTATE_DEVICE:    return "GPU_ANNOTATE_DEVICE";
        case OpCode::TENSOR_RELU:            return "TENSOR_RELU";
        case OpCode::TENSOR_BATCHNORM:       return "TENSOR_BATCHNORM";
        case OpCode::TENSOR_GATHER:          return "TENSOR_GATHER";
        case OpCode::TENSOR_SCATTER_D:       return "TENSOR_SCATTER";
        case OpCode::CPP_TEMPLATE_SPEC:      return "CPP_TEMPLATE_SPEC";
        case OpCode::RUST_BORROW_MUT:        return "RUST_BORROW_MUT";
        default:
            return "CUSTOM_DIALECT";
    }
}

// ============================================================================
// INTERNAL HELPERS (flag decoders â€” used by both printer and MIR dumper)
// ============================================================================

namespace detail {

/**
 * @brief RAII ostream state saver/restorer.
 *
 * Saves the full format-flag set, fill character, and precision on
 * construction; restores them on destruction. Use at the top of every
 * print_node() / dump_node() call to prevent std::hex, std::left,
 * std::setfill, std::fixed, std::setprecision from leaking to the caller.
 */
struct OStreamStateSaver {
    std::ostream&           out;
    std::ios_base::fmtflags flags;
    char                    fill;
    std::streamsize         precision;

    explicit OStreamStateSaver(std::ostream& o) noexcept
        : out(o), flags(o.flags()), fill(o.fill()), precision(o.precision()) {}

    ~OStreamStateSaver() noexcept {
        out.flags(flags);
        out.fill(fill);
        out.precision(precision);
    }

    OStreamStateSaver(const OStreamStateSaver&) = delete;
    OStreamStateSaver& operator=(const OStreamStateSaver&) = delete;
};

/// Decode Flags (uint16_t, IRAtom::flags) into a '+'-separated token string.
inline void append_atom_flags(uint16_t f, std::ostream& out) {
    bool any = false;
    auto emit = [&](const char* name) {
        if (any) { out << '+'; }
        out << name;
        any = true;
    };
    if (f & Flags::IS_CONST)          emit("IS_CONST");
    if (f & Flags::IS_DEAD)           emit("IS_DEAD");
    if (f & Flags::HAS_SIDE_EFFECTS)  emit("HAS_SIDE_EFFECTS");
    if (f & Flags::IS_VOLATILE)       emit("IS_VOLATILE");
    if (f & Flags::IS_SIMD)           emit("IS_SIMD");
    if (f & Flags::IS_VECTOR_LANE)    emit("IS_VECTOR_LANE");
    if (f & Flags::IS_PREDICATED)     emit("IS_PREDICATED");
    if (f & Flags::IS_FUSED)          emit("IS_FUSED");
    if (f & Flags::IS_OPTIMIZED)      emit("IS_OPTIMIZED");
    if (f & Flags::IS_LOOP_INVARIANT) emit("IS_LOOP_INVARIANT");
    if (f & Flags::NON_TEMPORAL)      emit("NON_TEMPORAL");
    if (f & Flags::IS_BRANCH)         emit("IS_BRANCH");
    if (f & Flags::IS_RETURN)         emit("IS_RETURN");
    if (f & Flags::IS_GUARD)          emit("IS_GUARD");
    if (f & Flags::MAY_DEOPT)         emit("MAY_DEOPT");
    if (!any) out << "NONE";
}

/// Decode ExtendedFlags (uint32_t, IRCore::logic_flags) into token string.
inline void append_logic_flags(uint32_t lf, std::ostream& out) {
    bool any = false;
    auto emit = [&](const char* name) {
        if (any) { out << '+'; }
        out << name;
        any = true;
    };
    if (lf & ExtendedFlags::MEM_RELAXED)        emit("MEM_RELAXED");
    if (lf & ExtendedFlags::MEM_ACQUIRE)        emit("MEM_ACQUIRE");
    if (lf & ExtendedFlags::MEM_RELEASE_)       emit("MEM_RELEASE");
    if (lf & ExtendedFlags::MEM_SEQ_CST)        emit("MEM_SEQ_CST");
    if (lf & ExtendedFlags::NO_ALIAS)           emit("NO_ALIAS");
    if (lf & ExtendedFlags::MAY_ALIAS)          emit("MAY_ALIAS");
    if (lf & ExtendedFlags::FAST_MATH)          emit("FAST_MATH");
    if (lf & ExtendedFlags::STRICT_FP)          emit("STRICT_FP");
    if (lf & ExtendedFlags::LATENCY_CRITICAL)   emit("LATENCY_CRITICAL");
    if (lf & ExtendedFlags::THROUGHPUT_MODE)    emit("THROUGHPUT_MODE");
    if (lf & ExtendedFlags::PREFETCH_HINT)      emit("PREFETCH_HINT");
    if (lf & ExtendedFlags::VECTORIZABLE)       emit("VECTORIZABLE");
    if (lf & ExtendedFlags::VEC_WIDTH_128)      emit("VEC_W128");
    if (lf & ExtendedFlags::VEC_WIDTH_256)      emit("VEC_W256");
    if (lf & ExtendedFlags::VEC_WIDTH_512)      emit("VEC_W512");
    if (lf & ExtendedFlags::PARALLEL_SAFE)      emit("PARALLEL_SAFE");
    if (lf & ExtendedFlags::REQUIRES_SYNC)      emit("REQUIRES_SYNC");
    if (lf & ExtendedFlags::HOT_PATH)           emit("HOT_PATH");
    if (lf & ExtendedFlags::COLD_PATH)          emit("COLD_PATH");
    if (lf & ExtendedFlags::JIT_ONLY)           emit("JIT_ONLY");
    if (lf & ExtendedFlags::AOT_ONLY)           emit("AOT_ONLY");
    if (lf & ExtendedFlags::HYBRID_READY)       emit("HYBRID_READY");
    if (lf & ExtendedFlags::DEOPT_SAFEPOINT)    emit("DEOPT_SAFEPOINT");
    if (lf & ExtendedFlags::PROFILE_GUIDED)     emit("PROFILE_GUIDED");
    if (lf & ExtendedFlags::SPECULATIVE_PATH)   emit("SPECULATIVE_PATH");
    if (lf & ExtendedFlags::CACHE_HOT)          emit("CACHE_HOT");
    if (lf & ExtendedFlags::CACHE_COLD)         emit("CACHE_COLD");
    if (lf & ExtendedFlags::INLINE_CANDIDATE)   emit("INLINE_CANDIDATE");
    if (lf & ExtendedFlags::NO_INLINE)          emit("NO_INLINE");
    if (lf & ExtendedFlags::DETERMINISTIC)      emit("DETERMINISTIC");
    if (!any) out << "NONE";
}

/// Emit a slot operand: prints "_%u" or "void" when zero.
inline void print_operand(uint32_t id, std::ostream& out) {
    if (id == 0) out << "void";
    else         out << "%" << id;
}

[[nodiscard]] inline std::string_view cfg_edge_kind_name(CFGEdgeKind kind) noexcept {
    switch (kind) {
        case CFGEdgeKind::Fallthrough:
            return "fallthrough";
        case CFGEdgeKind::UnconditionalBranch:
            return "branch";
        case CFGEdgeKind::ConditionalTrue:
            return "cond-true";
        case CFGEdgeKind::ConditionalFalse:
            return "cond-false";
        default:
            return "unknown";
    }
}

inline void print_cfg_summary(const IRStream& stream, std::ostream& out,
                              std::string_view line_prefix) {
    const auto& blocks = stream.get_basic_blocks();
    const auto& edges = stream.get_cfg_edges();

    out << line_prefix << "=== CFG"
        << " | blocks: " << blocks.size()
        << " | edges: " << edges.size()
        << " ===\n";

    for (const IRBasicBlock& block : blocks) {
        out << line_prefix << "block b" << block.id
            << " [" << block.begin << ", " << block.end << ")";
        if (block.is_entry) {
            out << " entry";
        }
        if (block.primary_label != INVALID_SLOT) {
            out << " label=%" << block.primary_label;
        }
        out << "\n";

        bool emitted_edge = false;
        for (const CFGEdge& edge : edges) {
            if (edge.from != block.id) {
                continue;
            }
            emitted_edge = true;
            out << line_prefix << "  -> b" << edge.to
                << " [" << cfg_edge_kind_name(edge.kind) << "]\n";
        }

        if (!emitted_edge) {
            out << line_prefix << "  -> (none)\n";
        }
    }
}

} // namespace detail

// ============================================================================
// IR PRINTER â€” DGTL-IR human-readable text format
//
// Output example:
//
//   ; === DGTL-IR stream | nodes: 5 | hash: 0x000000000A1B2C3D ===
//   [0000] LABEL                %0       = ()
//   [0001] LOAD_IMM             %1       = (a=void, b=void, c=void)
//   [0002] ADD_I32              %3       = (a=%1, b=%2, c=void)
//          flags: IS_CONST+IS_OPTIMIZED   logic: FAST_MATH+HOT_PATH
//   [0003] RET                  void     = (a=%3)
//          flags: IS_RETURN
//
// ============================================================================

class IRPrinter {
public:
    struct Options {
        bool show_index;
        bool show_hash;
        bool show_flags;
        bool show_logic_flags;
        bool show_trait_hints;
        bool show_cfg;
        bool show_header;

        // Explicit constructor bypasses GCC aggregate initialization bugs completely
        Options() 
            : show_index(true), 
              show_hash(true), 
              show_flags(true), 
              show_logic_flags(true), 
              show_trait_hints(false), 
                            show_cfg(false), 
              show_header(true) {}
    };

    /// Print the stream in DGTL-IR text format to the given ostream.
    static void print(const IRStream& stream, std::ostream& out,
                      Options opts = Options())
    {
        if (opts.show_header) {
            out << "; === DGTL-IR stream"
                << " | nodes: " << stream.size()
                << " | hash: 0x" << std::hex << std::setw(16)
                                 << std::setfill('0') << stream.get_version()
                << std::dec << " ===\n";
        }

        if (opts.show_cfg) {
            detail::print_cfg_summary(stream, out, "; ");
        }

        for (size_t i = 0; i < stream.size(); ++i) {
            const IRNode& n = stream.get_node_readonly(i);
            print_node(i, n, out, opts);
        }
    }

    /// Return the DGTL-IR text as a std::string.
    [[nodiscard]] static std::string to_string(const IRStream& stream,
                                               Options opts = Options())
    {
        std::ostringstream ss;
        print(stream, ss, opts);
        return ss.str();
    }

    /// Print a single IRNode without stream context (useful from pass authors).
    /// The IRCore parameter is not required by IRPrinter (all printed fields
    /// are mirrored in IRNode); it is accepted but ignored so callers can use
    /// a uniform signature with MIRDumper::dump_node.
    static void print_node(size_t idx, const IRNode& n,
                           std::ostream& out, const Options& opts = Options())
    {
        using detail::print_operand;
        // Restore ostream format state on exit so callers are unaffected.
        detail::OStreamStateSaver saver(out);

        // [index]
        if (opts.show_index) {
            out << "[" << std::setw(4) << std::setfill('0') << std::dec << idx << "] ";
        }

        // mnemonic â€” reset fill to ' ' so the width-24 pad uses spaces, not '0'.
        std::string_view mnemonic = opcode_name(n.opcode);
        out << std::left << std::setfill(' ') << std::setw(24) << mnemonic;

        // result = (operands)  â€” back to default (right-aligned, dec) for numbers
        out << std::right << std::dec;
        print_operand(n.result_id, out);
        out << " = (";
        out << "a="; print_operand(n.op_a, out);
        out << ", b="; print_operand(n.op_b, out);
        out << ", c="; print_operand(n.op_c, out);
        out << ")";

        // intent_hash
        if (opts.show_hash && n.intent_hash != 0) {
            out << "  hash=0x" << std::hex << std::setw(16)
                               << std::setfill('0') << n.intent_hash;
        }
        out << "\n";

        // flag decode (indented, only when flags are set)
        if (opts.show_flags && n.flags != 0) {
            out << "       flags: ";
            detail::append_atom_flags(n.flags, out);
            out << "\n";
        }

        if (opts.show_logic_flags && n.logic_flags != 0) {
            out << "       logic: ";
            detail::append_logic_flags(n.logic_flags, out);
            out << "\n";
        }

        // Optional trait hints
        if (opts.show_trait_hints) {
            out << std::dec << std::defaultfloat;
            out << "       [lat=" << IRTraits::estimate_latency_cycles(n.opcode);
            if (IRTraits::is_commutative(n.opcode))            out << " commutative";
            if (IRTraits::is_simd(n.opcode))                    out << " simd";
            if (IRTraits::has_side_effects(n.opcode, n.flags)) out << " side-fx";
            out << "]\n";
        }
    }

};

// ============================================================================
// IR SERIALIZER â€” compact binary round-trip
//
// Binary layout (all fields little-endian):
//
//   Offset  Size  Field
//   0       4     Magic: 0x4C544744 ('DGTL')
//   4       2     BIR version: 0x0201
//   6       2     Reserved: 0x0000
//   8       8     node_count (uint64_t)
//   16      NÃ—44  Node records:
//                   opcode(2) atom_flags(2) result_id(4) op_a(4) op_b(4)
//                   op_c(4) intent_hash(8) logic_flags(4) aux_flags(4)
//                   debug_id(4) epoch(4)
//
// Total per-node: 44 bytes.
// Total stream: 16 + N Ã— 44 bytes.
// ============================================================================

class IRSerializer {
public:
    static constexpr uint32_t kMagic       = 0x4C544744u; // 'DGTL' LE
    static constexpr uint16_t kBIRVersion  = 0x0201u;     // BIR v2.1
    static constexpr size_t   kHeaderBytes = 16u;
    static constexpr size_t   kNodeBytes   = 44u;

    /// One deserialized node (both tiers fully populated).
    struct NodeRecord {
        IRAtom atom;
        IRCore core;
    };

    // ------------------------------------------------------------------
    // SERIALIZE: IRStream â†’ std::vector<uint8_t>
    // Always succeeds (returns true). An empty stream serializes as a
    // valid 16-byte header with node_count=0 â€” round-trip safe.
    // ------------------------------------------------------------------
    [[nodiscard]] static bool serialize(const IRStream& stream,
                                        std::vector<uint8_t>& out)
    {
        const size_t n = stream.size();
        out.clear();
        out.resize(kHeaderBytes + n * kNodeBytes, 0u);

        uint8_t* p = out.data();

        // Header
        write_u32(p,  kMagic);
        write_u16(p + 4, kBIRVersion);
        write_u16(p + 6, 0x0000u);          // reserved
        write_u64(p + 8, static_cast<uint64_t>(n));
        p += kHeaderBytes;

        for (size_t i = 0; i < n; ++i) {
            const IRNode& nd = stream.get_node_readonly(i);
            const IRCore& c  = stream.get_core_readonly(i);

            write_u16(p,      static_cast<uint16_t>(nd.opcode));
            write_u16(p + 2,  nd.flags);
            write_u32(p + 4,  c.result_id);
            write_u32(p + 8,  c.op_a);
            write_u32(p + 12, c.op_b);
            write_u32(p + 16, c.op_c);
            write_u64(p + 20, c.intent_hash);
            write_u32(p + 28, c.logic_flags);
            write_u32(p + 32, c.aux_flags);
            write_u32(p + 36, c.debug_id);
            write_u32(p + 40, c.epoch);

            p += kNodeBytes;
        }
        return true;
    }

    // ------------------------------------------------------------------
    // PROBE: quick magic/version check without full deserialization.
    // ------------------------------------------------------------------
    [[nodiscard]] static bool probe(std::span<const uint8_t> data) noexcept {
        if (data.size() < kHeaderBytes) return false;
        if (read_u32(data.data()) != kMagic) return false;
        if (read_u16(data.data() + 4) != kBIRVersion) return false;
        return true;
    }

    // ------------------------------------------------------------------
    // DESERIALIZE: std::span<const uint8_t> â†’ std::vector<NodeRecord>
    //
    // On success: returns one NodeRecord per node; error is left empty.
    // On failure: returns empty vector; *error is set if error != nullptr.
    //
    // Use push_node(record.atom, record.core) to populate an IRStream.
    // ------------------------------------------------------------------
    [[nodiscard]] static std::vector<NodeRecord>
    deserialize(std::span<const uint8_t> data, std::string* error = nullptr)
    {
        auto fail = [&](const char* msg) -> std::vector<NodeRecord> {
            if (error) *error = msg;
            return {};
        };

        if (data.size() < kHeaderBytes)
            return fail("IRSerializer: buffer too small for header");
        if (read_u32(data.data()) != kMagic)
            return fail("IRSerializer: invalid magic (expected DGTL)");
        if (read_u16(data.data() + 4) != kBIRVersion)
            return fail("IRSerializer: unsupported BIR version");

        const uint64_t n64 = read_u64(data.data() + 8);
        if (n64 > 0x00FF'FFFFu)
            return fail("IRSerializer: node_count suspiciously large (> 16M)");

        const size_t n = static_cast<size_t>(n64);
        const size_t expected = kHeaderBytes + n * kNodeBytes;
        if (data.size() < expected)
            return fail("IRSerializer: buffer truncated");

        std::vector<NodeRecord> result;
        result.reserve(n);

        const uint8_t* p = data.data() + kHeaderBytes;
        for (size_t i = 0; i < n; ++i) {
            NodeRecord rec{};
            rec.atom.opcode    = static_cast<OpCode>(read_u16(p));
            rec.atom.flags     = read_u16(p + 2);
            rec.core.result_id = read_u32(p + 4);
            rec.core.op_a      = read_u32(p + 8);
            rec.core.op_b      = read_u32(p + 12);
            rec.core.op_c      = read_u32(p + 16);
            rec.core.intent_hash = read_u64(p + 20);
            rec.core.logic_flags = read_u32(p + 28);
            rec.core.aux_flags   = read_u32(p + 32);
            rec.core.debug_id    = read_u32(p + 36);
            rec.core.epoch       = read_u32(p + 40);
            result.push_back(rec);
            p += kNodeBytes;
        }

        return result;
    }

private:
    // ------------------------------------------------------------------
    // Little-endian byte I/O helpers â€” memcpy-based to avoid UB from
    // type-punning / strict-aliasing violations.
    // ------------------------------------------------------------------
    static void write_u16(uint8_t* dst, uint16_t v) noexcept {
        const uint8_t bytes[2] = {
            static_cast<uint8_t>(v),
            static_cast<uint8_t>(v >> 8)
        };
        std::memcpy(dst, bytes, 2);
    }
    static void write_u32(uint8_t* dst, uint32_t v) noexcept {
        const uint8_t bytes[4] = {
            static_cast<uint8_t>(v),
            static_cast<uint8_t>(v >> 8),
            static_cast<uint8_t>(v >> 16),
            static_cast<uint8_t>(v >> 24)
        };
        std::memcpy(dst, bytes, 4);
    }
    static void write_u64(uint8_t* dst, uint64_t v) noexcept {
        uint8_t bytes[8];
        for (int i = 0; i < 8; ++i) bytes[i] = static_cast<uint8_t>(v >> (8*i));
        std::memcpy(dst, bytes, 8);
    }
    static uint16_t read_u16(const uint8_t* src) noexcept {
        uint8_t bytes[2]; std::memcpy(bytes, src, 2);
        return static_cast<uint16_t>(bytes[0]) |
               (static_cast<uint16_t>(bytes[1]) << 8);
    }
    static uint32_t read_u32(const uint8_t* src) noexcept {
        uint8_t bytes[4]; std::memcpy(bytes, src, 4);
        return static_cast<uint32_t>(bytes[0])         |
               (static_cast<uint32_t>(bytes[1]) << 8)  |
               (static_cast<uint32_t>(bytes[2]) << 16) |
               (static_cast<uint32_t>(bytes[3]) << 24);
    }
    static uint64_t read_u64(const uint8_t* src) noexcept {
        uint64_t v = 0;
        uint8_t bytes[8]; std::memcpy(bytes, src, 8);
        for (int i = 0; i < 8; ++i) v |= (static_cast<uint64_t>(bytes[i]) << (8*i));
        return v;
    }
};

// ============================================================================
// MIR DUMPER â€” machine-IR diagnostic dump (all fields, traits, perf hints)
//
// Intended for pass authors and backend integrators debugging post-lowering IR.
// Shows every field of both IRAtom and IRCore, fully decoded, alongside
// IRTraits analysis and micro-architectural performance hints.
//
// Output example:
//
//   === MIR DUMP: 5 nodes | version 0x0000000000001234 ===
//   â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//   [0002] opcode=0x0014  ADD_I32
//          result=%3  a=%1  b=%2  c=void
//          atom_flags=0x0101   [IS_CONST+IS_OPTIMIZED]
//          logic_flags=0x00020040  [FAST_MATH+HOT_PATH]
//          intent_hash=0xDEADBEEFCAFEBABE
//          aux_flags=0x00000000  debug_id=0x0000001A  epoch=4
//          trait: integer | commutative | can_fold | lat=1.0cy  tput=1slot
//
// ============================================================================

class MIRDumper {
public:
    struct Options {
        bool full_fields;
        bool trait_analysis;
        bool perf_hints;
        bool flag_decode;
        bool show_cfg;
        bool show_header;

        // Explicit constructor bypasses GCC aggregate initialization bugs completely
        Options() 
            : full_fields(true), 
              trait_analysis(true), 
              perf_hints(true), 
              flag_decode(true), 
                            show_cfg(false), 
              show_header(true) {}
    };

    static void dump(const IRStream& stream, std::ostream& out,
                     Options opts = Options())
    {
        if (opts.show_header) {
            out << "=== MIR DUMP: " << stream.size() << " nodes"
                << " | version 0x" << std::hex << std::setw(16)
                                   << std::setfill('0') << stream.get_version()
                << std::dec << " ===\n";
            out << std::string(56, '-') << "\n";
        }

        if (opts.show_cfg) {
            detail::print_cfg_summary(stream, out, "");
            out << std::string(56, '-') << "\n";
        }

        for (size_t i = 0; i < stream.size(); ++i) {
            const IRNode& n = stream.get_node_readonly(i);
            const IRCore& c = stream.get_core_readonly(i);
            dump_node(i, n, c, out, opts);
        }
    }

    [[nodiscard]] static std::string to_string(const IRStream& stream,
                                               Options opts = Options())
    {
        std::ostringstream ss;
        dump(stream, ss, opts);
        return ss.str();
    }

    /// Dump a single node outside of a stream context.
    static void dump_node(size_t idx, const IRNode& n, const IRCore& c,
                          std::ostream& out, const Options& opts = Options())
    {
        using detail::print_operand;
        // Restore ostream format state on exit so callers are unaffected.
        detail::OStreamStateSaver saver(out);

        // Header line
        out << "[" << std::right << std::setfill('0') << std::setw(4)
            << std::dec << idx << "]"
            << " opcode=0x" << std::hex << std::setw(4) << std::setfill('0')
            << static_cast<uint32_t>(n.opcode)
            << std::dec << std::setfill(' ')
            << "  " << opcode_name(n.opcode) << "\n";

        // Operands
        out << "       result="; print_operand(n.result_id, out);
        out << "  a=";          print_operand(n.op_a, out);
        out << "  b=";          print_operand(n.op_b, out);
        out << "  c=";          print_operand(n.op_c, out);
        out << "\n";

        // Atom flags
        out << "       atom_flags=0x" << std::hex << std::setw(4)
            << std::setfill('0') << n.flags;
        if (opts.flag_decode && n.flags != 0) {
            out << std::dec << std::setfill(' ') << "   [";
            detail::append_atom_flags(n.flags, out);
            out << "]";
        }
        out << "\n";

        // Logic flags
        out << "       logic_flags=0x" << std::hex << std::setw(8)
            << std::setfill('0') << n.logic_flags;
        if (opts.flag_decode && n.logic_flags != 0) {
            out << std::dec << std::setfill(' ') << "  [";
            detail::append_logic_flags(n.logic_flags, out);
            out << "]";
        }
        out << "\n";

        // Intent hash
        out << "       intent_hash=0x" << std::hex << std::setw(16)
            << std::setfill('0') << n.intent_hash << "\n";

        // Full IRCore fields (aux_flags, debug_id, epoch live only in IRCore)
        if (opts.full_fields) {
            out << "       aux_flags=0x" << std::hex << std::setw(8)
                << std::setfill('0') << c.aux_flags
                << "  debug_id=0x" << std::setw(8) << std::setfill('0') << c.debug_id
                << std::dec << std::setfill(' ')
                << "  epoch=" << c.epoch << "\n";
        }

        // IRTraits analysis
        if (opts.trait_analysis) {
            out << std::dec << std::setfill(' ');
            out << "       trait:";
            if (IRTraits::is_integer_op(n.opcode))           out << " integer";
            else if (IRTraits::is_float_op(n.opcode))        out << " float";
            else if (IRTraits::is_simd(n.opcode))            out << " simd";
            else if (IRTraits::is_compare_op(n.opcode))      out << " compare";
            else if (IRTraits::is_memory_op(n.opcode))       out << " memory";
            else if (IRTraits::is_control_flow(n.opcode))    out << " control-flow";
            else if (IRTraits::is_ownership_op(n.opcode))    out << " ownership";
            else if (IRTraits::is_concurrency_op(n.opcode))  out << " concurrency";
            else if (IRTraits::is_tensor_op(n.opcode))       out << " tensor";
            else if (IRTraits::is_dsa_op(n.opcode))          out << " dsa";
            else                                              out << " other";

            if (IRTraits::is_terminator(n.opcode))                 out << " | terminator";
            if (IRTraits::is_commutative(n.opcode))                out << " | commutative";
            if (IRTraits::can_fold_constants(n.opcode))            out << " | can_fold";
            if (IRTraits::has_side_effects(n.opcode, n.flags))     out << " | side-fx";
            if (IRTraits::must_synchronize(n.opcode))              out << " | must_sync";
            if (IRTraits::may_create_deadlock(n.opcode))           out << " | DEADLOCK_RISK";

            out << "\n";
        }

        // Micro-architectural performance hints
        if (opts.perf_hints) {
            out << "       perf: lat="
                << std::fixed << std::setprecision(1)
                << IRTraits::estimate_latency_cycles(n.opcode)
                << "cy  tput="
                << std::defaultfloat
                << IRTraits::estimate_throughput_slots(n.opcode)
                << "slot"
                << "  weight=" << IRTraits::get_inline_weight(n.opcode)
                << "\n";
        }

        out << "\n"; // blank line between nodes
    }

};

// ============================================================================
// COMPILE-TIME CONTRACTS
// ============================================================================

static_assert(IRSerializer::kNodeBytes == 44u,
    "IRSerializer: node record size must be 44 bytes to match IRAtom+IRCore layout");
static_assert(IRSerializer::kHeaderBytes == 16u,
    "IRSerializer: header must be 16 bytes");

} // namespace dgtl::ir