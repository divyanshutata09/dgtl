/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Machine IR Types, Opcodes, Registers, Operands & Instructions
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

#include <string_view>
#include <vector>

export module dgtl.mir.machine_ir:types;

import dgtl.mir.isel_gen;

export namespace dgtl::machine {

/* ============================================================================
 * Core identifiers
 * ========================================================================== */

using MachineId   = std::uint32_t;
using BlockId     = std::uint32_t;
using InstrId     = std::uint32_t;
using RegId       = std::uint32_t;
using StackSlotId = std::uint32_t;
using SymbolId    = std::uint32_t;

inline constexpr MachineId INVALID_MACHINE_ID = 0xFFFFFFFFu;
inline constexpr BlockId INVALID_BLOCK_ID = 0xFFFFFFFFu;
inline constexpr InstrId INVALID_INSTR_ID = 0xFFFFFFFFu;
inline constexpr RegId INVALID_REG = 0xFFFFFFFFu;
inline constexpr StackSlotId INVALID_STACK_SLOT = 0xFFFFFFFFu;
inline constexpr SymbolId INVALID_SYMBOL = 0xFFFFFFFFu;

/* ============================================================================
 * Machine IR state
 * ========================================================================== */

enum class MachinePhase : std::uint8_t {
    Selected,
    Legalized,
    PreRegAlloc,
    PostRegAlloc,
    FrameLowered,
    ABILowered,
    EmissionReady
};

enum class RegisterKind : std::uint8_t {
    Virtual,
    Physical
};

using MachineRegisterKind = RegisterKind;

enum class RegisterClassKind : std::uint8_t {
    Invalid,
    General,
    Integer,
    FloatingPoint,
    Vector,
    Predicate,
    Mask,
    Address,
    Special,
    Custom
};

/* ============================================================================
 * Register model
 * ========================================================================== */

struct MachineRegister {
    RegId id = INVALID_REG;
    RegisterKind kind = RegisterKind::Virtual;
    RegisterClassKind reg_class = RegisterClassKind::Invalid;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return id != INVALID_REG;
    }

    [[nodiscard]] constexpr bool is_virtual() const noexcept {
        return valid() && kind == RegisterKind::Virtual;
    }

    [[nodiscard]] constexpr bool is_physical() const noexcept {
        return valid() && kind == RegisterKind::Physical;
    }

    [[nodiscard]] static constexpr MachineRegister virtual_reg(
        RegId reg_id,
        RegisterClassKind cls = RegisterClassKind::General) noexcept {
        MachineRegister r;
        r.id = reg_id;
        r.kind = RegisterKind::Virtual;
        r.reg_class = cls;
        return r;
    }

    [[nodiscard]] static constexpr MachineRegister physical_reg(
        RegId reg_id,
        RegisterClassKind cls = RegisterClassKind::General) noexcept {
        MachineRegister r;
        r.id = reg_id;
        r.kind = RegisterKind::Physical;
        r.reg_class = cls;
        return r;
    }

    friend constexpr bool operator==(const MachineRegister&,
                                     const MachineRegister&) noexcept = default;
};

struct RegisterMask {
    std::vector<std::uint64_t> words;

    void resize(std::size_t bit_count) {
        words.resize((bit_count + 63u) / 64u);
    }

    void clear() noexcept {
        std::fill(words.begin(), words.end(), 0u);
    }

    void set(std::size_t bit) {
        const std::size_t word = bit / 64u;
        if (word >= words.size()) {
            words.resize(word + 1u);
        }
        words[word] |= (std::uint64_t{1} << (bit % 64u));
    }

    void reset(std::size_t bit) noexcept {
        const std::size_t word = bit / 64u;
        if (word < words.size()) {
            words[word] &= ~(std::uint64_t{1} << (bit % 64u));
        }
    }

    [[nodiscard]] bool test(std::size_t bit) const noexcept {
        const std::size_t word = bit / 64u;
        return word < words.size() &&
               (words[word] & (std::uint64_t{1} << (bit % 64u))) != 0;
    }
};

/* ============================================================================
 * Machine operands
 * ========================================================================== */

enum class MachineOperandKind : std::uint8_t {
    Invalid,
    Register,
    Immediate,
    FloatingImmediate,
    Memory,
    BasicBlock,
    Symbol,
    StackSlot,
    ConstantPoolIndex,
    JumpTableIndex,
    RegisterMask,
    Metadata
};

enum class OperandRole : std::uint8_t {
    Use,
    Def,
    UseDef,
    Kill,
    Dead,
    ImplicitUse,
    ImplicitDef,
    Debug
};

enum class RelocationKind : std::uint8_t {
    None,
    Absolute,
    PCRelative,
    GOT,
    PLT,
    TLS,
    TargetSpecific
};

struct MachineMemoryOperand {
    MachineRegister base = {};
    MachineRegister index = {};
    std::int32_t scale = 1;
    std::int64_t displacement = 0;
    std::uint32_t address_space = 0;
    std::uint32_t size_bytes = 0;
    std::uint32_t alignment = 1;

    bool load = false;
    bool store = false;
    bool volatile_access = false;
    bool atomic = false;
    bool invariant = false;
};

enum class SubRegIndex : std::uint8_t {
    None = 0,
    SubLow32 = 1,
    SubHigh32 = 2,
    SubLow16 = 3,
    SubHigh16 = 4,
    SubLow8 = 5,
    SubHigh8 = 6,
    SubLow64 = 7,
    SubHigh64 = 8,
    SubLow128 = 9,
    SubHigh128 = 10,
    SubLow256 = 11,
    SubHigh256 = 12
};

using LaneBitmask = std::uint64_t;
inline constexpr LaneBitmask kAllLanes = ~0ULL;

struct alignas(16) MachineOperand {
    MachineOperandKind kind = MachineOperandKind::Invalid;
    OperandRole role = OperandRole::Use;

    MachineRegister reg = {};
    std::int64_t immediate = 0;
    double floating_immediate = 0.0;

    MachineMemoryOperand memory = {};
    BlockId block = INVALID_BLOCK_ID;
    SymbolId symbol = INVALID_SYMBOL;
    StackSlotId stack_slot = INVALID_STACK_SLOT;
    std::uint32_t constant_pool_index = 0xFFFFFFFFu;
    std::uint32_t jump_table_index = 0xFFFFFFFFu;

    RelocationKind relocation = RelocationKind::None;

    std::uint32_t subregister = 0;
    SubRegIndex sub_reg_index = SubRegIndex::None;
    LaneBitmask lane_mask = kAllLanes;
    std::uint32_t flags = 0;

    [[nodiscard]] constexpr bool has_subregister() const noexcept {
        return sub_reg_index != SubRegIndex::None || subregister != 0;
    }

    [[nodiscard]] static MachineOperand use(MachineRegister r) noexcept {
        MachineOperand op;
        op.kind = MachineOperandKind::Register;
        op.role = OperandRole::Use;
        op.reg = r;
        return op;
    }

    [[nodiscard]] static MachineOperand def(MachineRegister r) noexcept {
        MachineOperand op;
        op.kind = MachineOperandKind::Register;
        op.role = OperandRole::Def;
        op.reg = r;
        return op;
    }

    [[nodiscard]] static MachineOperand imm(std::int64_t value) noexcept {
        MachineOperand op;
        op.kind = MachineOperandKind::Immediate;
        op.immediate = value;
        return op;
    }

    [[nodiscard]] static MachineOperand block_ref(BlockId id) noexcept {
        MachineOperand op;
        op.kind = MachineOperandKind::BasicBlock;
        op.block = id;
        return op;
    }

    [[nodiscard]] static MachineOperand stack_ref(StackSlotId id) noexcept {
        MachineOperand op;
        op.kind = MachineOperandKind::StackSlot;
        op.stack_slot = id;
        return op;
    }

    [[nodiscard]] static MachineOperand cpi(std::uint32_t idx) noexcept {
        MachineOperand op;
        op.kind = MachineOperandKind::ConstantPoolIndex;
        op.constant_pool_index = idx;
        return op;
    }

    [[nodiscard]] static MachineOperand jti(std::uint32_t idx) noexcept {
        MachineOperand op;
        op.kind = MachineOperandKind::JumpTableIndex;
        op.jump_table_index = idx;
        return op;
    }

    [[nodiscard]] static MachineOperand memory_ref(
        const MachineMemoryOperand& mem_op,
        OperandRole r = OperandRole::Use) noexcept {
        MachineOperand op;
        op.kind = MachineOperandKind::Memory;
        op.role = r;
        op.memory = mem_op;
        return op;
    }

    [[nodiscard]] constexpr bool is_register() const noexcept {
        return kind == MachineOperandKind::Register;
    }

    [[nodiscard]] constexpr bool is_memory() const noexcept {
        return kind == MachineOperandKind::Memory;
    }

    [[nodiscard]] constexpr bool is_immediate() const noexcept {
        return kind == MachineOperandKind::Immediate;
    }

    [[nodiscard]] constexpr bool is_def() const noexcept {
        return role == OperandRole::Def || role == OperandRole::UseDef ||
               role == OperandRole::ImplicitDef;
    }

    [[nodiscard]] constexpr bool is_use() const noexcept {
        return role == OperandRole::Use || role == OperandRole::UseDef ||
               role == OperandRole::ImplicitUse;
    }
};

/* ============================================================================
 * Industry-Grade Machine Opcode Architecture
 * ========================================================================== */

using MachineOpcode = std::uint16_t;

inline constexpr MachineOpcode INVALID_MACHINE_OPCODE = 0xFFFFu;
inline constexpr MachineOpcode GENERIC_OPCODE_START   = 0x0001u;
inline constexpr MachineOpcode GENERIC_OPCODE_END     = 0x7FFFu;
inline constexpr MachineOpcode TARGET_OPCODE_START    = 0x8000u;

enum GenericMachineOpcode : MachineOpcode {
    GM_INVALID = 0,
    GM_NOOP = 1,
    GM_COPY,
    GM_IMMEDIATE,
    GM_LOAD_STACK,
    GM_STORE_STACK,

    // Integer Binary 32-bit
    GM_IADD32, GM_ISUB32, GM_IMUL32, GM_IDIV32, GM_IREM32,
    GM_IAND32, GM_IOR32,  GM_IXOR32, GM_ISHL32, GM_ISHR32,

    // Integer Binary 64-bit
    GM_IADD64, GM_ISUB64, GM_IMUL64, GM_IDIV64, GM_IREM64,
    GM_IAND64, GM_IOR64,  GM_IXOR64, GM_ISHL64, GM_ISHR64,

    // Floating-Point
    GM_FADD32, GM_FSUB32, GM_FMUL32, GM_FDIV32,
    GM_FADD64, GM_FSUB64, GM_FMUL64, GM_FDIV64,
    GM_FFMA32, GM_FFMA64, GM_FAST_INV_SQRT,

    // Comparison & Control Flow
    GM_ICMP32, GM_ICMP64, GM_FCMP32, GM_FCMP64,
    GM_ICMP32_EQ, GM_ICMP32_NE, GM_ICMP32_LT, GM_ICMP32_LE, GM_ICMP32_GT, GM_ICMP32_GE,
    GM_ICMP64_EQ, GM_ICMP64_NE, GM_ICMP64_LT, GM_ICMP64_LE, GM_ICMP64_GT, GM_ICMP64_GE,
    GM_FCMP32_EQ, GM_FCMP32_NE, GM_FCMP32_LT, GM_FCMP32_LE, GM_FCMP32_GT, GM_FCMP32_GE,
    GM_FCMP64_EQ, GM_FCMP64_NE, GM_FCMP64_LT, GM_FCMP64_LE, GM_FCMP64_GT, GM_FCMP64_GE,
    GM_BR_COMPARE_EQ, GM_BR_COMPARE_NE, GM_BR_COMPARE_LT, GM_BR_COMPARE_LE, GM_BR_COMPARE_GT, GM_BR_COMPARE_GE,
    GM_GOTO, GM_BR_COND, GM_BR_COMPARE, GM_BR_TABLE,
    GM_CALL, GM_CALL_VARIADIC, GM_RETURN, GM_SYSCALL,

    // Memory
    GM_LOAD, GM_STORE, GM_VEC_LOAD, GM_VEC_STORE,
    GM_VEC_GATHER, GM_VEC_SCATTER,

    // Concurrency / Atomics
    GM_FENCE, GM_ACQUIRE_FENCE, GM_RELEASE_FENCE, GM_ATOMIC_FENCE,
    GM_ATOMIC_LOAD, GM_ATOMIC_STORE, GM_ATOMIC_SNAPSHOT,
    GM_LOCK_ACQUIRE, GM_LOCK_RELEASE, GM_SPIN_ACQUIRE,
    GM_BARRIER_SYNC, GM_SYNC_BARRIER,

    // Async / Coroutines / Fibers
    GM_ASYNC_AWAIT, GM_ASYNC_LAUNCH, GM_ASYNC_SPAWN,
    GM_FIBER_YIELD, GM_FIBER_YIELD_LOOP, GM_SUSPEND_CTX, GM_RESUME_CTX,
    GM_THREAD_SPAWN,

    // Memory & Address Math
    GM_LEA, GM_GEP, GM_PTR_DIFF, GM_PTR_TO_INT, GM_INT_TO_PTR,
    GM_PREFETCH, GM_PREFETCH_T0, GM_CLFLUSH, GM_READ_TSC,

    // String Primitives
    GM_STRING_LENGTH, GM_STRING_COMPARE, GM_STRING_CONCAT,
    GM_STRING_SUBSTRING, GM_STRING_COPY, GM_STRING_EQUALS,
    GM_STRING_FIND, GM_STRING_HASH,

    // Dynamic Allocations / Data Structures
    GM_DSA_ALLOCATE, GM_DSA_DEALLOCATE,
    GM_RING_PUSH_FREE, GM_DEOPT_TRIGGER,
    GM_TREE_BUILD, GM_TREE_TRAVERSE,
    GM_TRIE_BUILD, GM_TRIE_INSERT, GM_TRIE_LOOKUP,
    GM_GRAPH_BUILD, GM_GRAPH_TRAVERSE,
    GM_TUPLE_PACK, GM_TUPLE_UNPACK,

    // Tensor & Accelerator Kernels
    GM_TENSOR_OP, GM_TENSOR_BROADCAST, GM_TENSOR_CONCAT, GM_TENSOR_CONV,
    GM_TENSOR_GEMM, GM_TENSOR_GATHER, GM_TENSOR_POOL, GM_TENSOR_REDUCE,
    GM_TENSOR_RESHAPE, GM_TENSOR_SCATTER_D, GM_TENSOR_SLICE,
    GM_TENSOR_SOFTMAX, GM_TENSOR_TRANSPOSE,
    GM_MATMUL_BF16, GM_MATMUL_FP8, GM_SDPA_F16,
    GM_SOFTMAX_FAST, GM_LAYER_NORM, GM_ROPE_EMBED,
    GM_MAT4_MUL, GM_QUAT_SLERP, GM_VEC3_CROSS, GM_VEC4_DOT, GM_RAY_AABB_ISECT,
    GM_GPU_LAUNCH, GM_WASM_INVOKE,

    GM_MAX_GENERIC = GENERIC_OPCODE_END
};

[[nodiscard]] constexpr bool is_generic_opcode(MachineOpcode op) noexcept {
    return op >= GENERIC_OPCODE_START && op <= GENERIC_OPCODE_END;
}

[[nodiscard]] constexpr bool is_target_opcode(MachineOpcode op) noexcept {
    return op >= TARGET_OPCODE_START && op != INVALID_MACHINE_OPCODE;
}
 
/* ============================================================================
 * Comparison Predicates
 * ========================================================================== */

enum class ComparisonPredicate : std::uint8_t {
    Equal = 0,
    NotEqual = 1,
    SignedLessThan = 2,
    SignedLessEqual = 3,
    SignedGreaterThan = 4,
    SignedGreaterEqual = 5,
    UnsignedLessThan = 6,
    UnsignedLessEqual = 7,
    UnsignedGreaterThan = 8,
    UnsignedGreaterEqual = 9,
    Ordered = 10,
    Unordered = 11
};

inline constexpr std::string_view to_predicate_string(ComparisonPredicate pred) noexcept {
    switch (pred) {
        case ComparisonPredicate::Equal: return "eq";
        case ComparisonPredicate::NotEqual: return "ne";
        case ComparisonPredicate::SignedLessThan: return "lt";
        case ComparisonPredicate::SignedLessEqual: return "le";
        case ComparisonPredicate::SignedGreaterThan: return "gt";
        case ComparisonPredicate::SignedGreaterEqual: return "ge";
        case ComparisonPredicate::UnsignedLessThan: return "ult";
        case ComparisonPredicate::UnsignedLessEqual: return "ule";
        case ComparisonPredicate::UnsignedGreaterThan: return "ugt";
        case ComparisonPredicate::UnsignedGreaterEqual: return "uge";
        case ComparisonPredicate::Ordered: return "ord";
        case ComparisonPredicate::Unordered: return "uno";
        default: return "eq";
    }
}

/* ============================================================================
 * Canonical MachineForm and MachineSemanticKind
 * ========================================================================== */

using MachineForm = dgtl::mir::MachineForm;
using MachineSemanticKind = dgtl::mir::MachineSemanticKind;

[[nodiscard]] constexpr bool
semantic_kind_matches_form(MachineForm form, MachineSemanticKind kind) noexcept {
    return dgtl::mir::generated::semantic_kind_matches_form(form, kind);
}

[[nodiscard]] constexpr bool
requires_semantic_kind(MachineForm form) noexcept {
    return form != MachineForm::Unhandled;
}

[[nodiscard]] inline constexpr std::string_view machine_form_name(MachineForm form) noexcept {
    switch (form) {
        case MachineForm::NoOp: return "NoOp";
        case MachineForm::Move: return "Move";
        case MachineForm::LoadImmediate: return "LoadImmediate";
        case MachineForm::LoadStack: return "LoadStack";
        case MachineForm::StoreStack: return "StoreStack";
        case MachineForm::IntegerBinary32: return "IntegerBinary32";
        case MachineForm::IntegerBinary64: return "IntegerBinary64";
        case MachineForm::FloatBinary32: return "FloatBinary32";
        case MachineForm::FloatBinary64: return "FloatBinary64";
        case MachineForm::FloatTernaryFma32: return "FloatTernaryFma32";
        case MachineForm::Compare32: return "Compare32";
        case MachineForm::VectorBinary512: return "VectorBinary512";
        case MachineForm::VectorGather512: return "VectorGather512";
        case MachineForm::VectorStore512: return "VectorStore512";
        case MachineForm::Call: return "Call";
        case MachineForm::Return: return "Return";
        case MachineForm::Goto: return "Goto";
        case MachineForm::BranchCond: return "BranchCond";
        case MachineForm::BranchCompare: return "BranchCompare";
        case MachineForm::Syscall: return "Syscall";
        case MachineForm::TensorOp: return "TensorOp";
        case MachineForm::HardwareFence: return "HardwareFence";
        case MachineForm::AtomicOp: return "AtomicOp";
        case MachineForm::ConcurrentSync: return "ConcurrentSync";
        case MachineForm::Unhandled: return "Unhandled";
        default: return "Unknown";
    }
}

enum class MachineInstrFlag : std::uint32_t {
    None            = 0,
    Terminator      = 1u << 0,
    Branch          = 1u << 1,
    Call            = 1u << 2,
    Return          = 1u << 3,
    Load            = 1u << 4,
    Store           = 1u << 5,
    Atomic          = 1u << 6,
    Barrier         = 1u << 7,
    SideEffect      = 1u << 8,
    Commutable      = 1u << 9,
    MayLoad         = 1u << 10,
    MayStore        = 1u << 11,
    HasUnmodeledSideEffect = 1u << 12,
    Pseudo          = 1u << 13,
    Debug           = 1u << 14,
    FrameSetup      = 1u << 15,
    FrameDestroy    = 1u << 16,
    CFI             = 1u << 17
};

constexpr MachineInstrFlag operator|(MachineInstrFlag a, MachineInstrFlag b) noexcept {
    return static_cast<MachineInstrFlag>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

constexpr MachineInstrFlag operator&(MachineInstrFlag a, MachineInstrFlag b) noexcept {
    return static_cast<MachineInstrFlag>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}

enum class MachineTrait : std::uint64_t {
    None                = 0ULL,
    Commutative         = 1ULL << 0,
    HasNoSideEffects    = 1ULL << 1,
    IsTerminator        = 1ULL << 2,
    IsMemoryRead        = 1ULL << 3,
    IsMemoryWrite       = 1ULL << 4,
    IsMemoryOp          = 1ULL << 5,
    IsVectorizable      = 1ULL << 6,
    IsFusible           = 1ULL << 7,
    IsSpeculative       = 1ULL << 8,
    IsGuard             = 1ULL << 9,
    IsGPU               = 1ULL << 10,
    IsTensor            = 1ULL << 11,
    IsGraph             = 1ULL << 12,
    IsAsyncSpawnable    = 1ULL << 13,
    IsLatencyCritical   = 1ULL << 14,
    IsHardwareIntrinsic = 1ULL << 15,
    Associative         = 1ULL << 16,
    ConstantFoldable    = 1ULL << 17,
    MemoryBarrier       = 1ULL << 18,
    SingleDefUse        = 1ULL << 19,
    RequiresSpill       = 1ULL << 20,
    ClobbersFlags       = 1ULL << 21,
    TwoAddress          = 1ULL << 22,
    BranchDelaySlot     = 1ULL << 23
};

[[nodiscard]] constexpr MachineTrait operator|(MachineTrait a, MachineTrait b) noexcept {
    return static_cast<MachineTrait>(static_cast<std::uint64_t>(a) | static_cast<std::uint64_t>(b));
}
[[nodiscard]] constexpr MachineTrait operator&(MachineTrait a, MachineTrait b) noexcept {
    return static_cast<MachineTrait>(static_cast<std::uint64_t>(a) & static_cast<std::uint64_t>(b));
}
[[nodiscard]] constexpr MachineTrait operator~(MachineTrait a) noexcept {
    return static_cast<MachineTrait>(~static_cast<std::uint64_t>(a));
}

enum class MachineDomain : std::uint64_t {
    None        = 0ULL,
    Arithmetic  = 1ULL << 0,
    Memory      = 1ULL << 1,
    ControlFlow = 1ULL << 2,
    Terminator  = 1ULL << 3,
    Comparison  = 1ULL << 4,
    BitwiseOp   = 1ULL << 5,
    SIMD        = 1ULL << 6,
    GPU         = 1ULL << 7,
    Tensor      = 1ULL << 8,
    Concurrency = 1ULL << 9,
    DSA         = 1ULL << 10,
    Ownership   = 1ULL << 11,
    PointerMath = 1ULL << 12,
    NeuralNet   = 1ULL << 13,
    SpatialMath = 1ULL << 14
};

[[nodiscard]] constexpr MachineDomain operator|(MachineDomain a, MachineDomain b) noexcept {
    return static_cast<MachineDomain>(static_cast<std::uint64_t>(a) | static_cast<std::uint64_t>(b));
}
[[nodiscard]] constexpr MachineDomain operator&(MachineDomain a, MachineDomain b) noexcept {
    return static_cast<MachineDomain>(static_cast<std::uint64_t>(a) & static_cast<std::uint64_t>(b));
}

enum class MachineAttribute : std::uint64_t {
    None             = 0ULL,
    NoWrap           = 1ULL << 0,
    Exact            = 1ULL << 1,
    FastMath         = 1ULL << 2,
    Aligned          = 1ULL << 3,
    NoAlias          = 1ULL << 4,
    AddrSpaceGeneric = 1ULL << 5,
    Predicated       = 1ULL << 6,
    Relaxed          = 1ULL << 7,
    Acquire          = 1ULL << 8,
    Release          = 1ULL << 9,
    SeqCst           = 1ULL << 10,
    HotPath          = 1ULL << 11,
    ColdPath         = 1ULL << 12,
    LockFree         = 1ULL << 13,
    Variadic         = 1ULL << 14,
    MultiResult      = 1ULL << 15,
    Volatile         = 1ULL << 16
};

[[nodiscard]] constexpr MachineAttribute operator|(MachineAttribute a, MachineAttribute b) noexcept {
    return static_cast<MachineAttribute>(static_cast<std::uint64_t>(a) | static_cast<std::uint64_t>(b));
}
[[nodiscard]] constexpr MachineAttribute operator&(MachineAttribute a, MachineAttribute b) noexcept {
    return static_cast<MachineAttribute>(static_cast<std::uint64_t>(a) & static_cast<std::uint64_t>(b));
}

enum class MachineCFIKind : std::uint8_t {
    None = 0,
    DefCFA = 1,
    DefCFAOffset = 2,
    DefCFARegister = 3,
    Offset = 4,
    Restore = 5,
    RememberState = 6,
    RestoreState = 7
};

struct MachineCFIData {
    MachineCFIKind kind = MachineCFIKind::None;
    MachineRegister reg = {};
    std::int32_t offset = 0;
};

struct alignas(64) MachineInstruction {
    InstrId id = INVALID_INSTR_ID;
    MachineForm form = MachineForm::Unhandled;
    MachineSemanticKind semantic_kind = MachineSemanticKind::None;
    // Target-independent opcode used by MIR transforms, analyses and selectors.
    MachineOpcode opcode = INVALID_MACHINE_OPCODE;
    // Concrete target opcode assigned exactly once during target lowering.
    // Keeping this separate prevents target lowering from destroying MIR semantics.
    MachineOpcode target_opcode_value = INVALID_MACHINE_OPCODE;
    MachineInstrFlag flags = MachineInstrFlag::None;
    MachineCFIData cfi_data{};

    BlockId parent_block = INVALID_BLOCK_ID;
    InstrId prev_instr   = INVALID_INSTR_ID;
    InstrId next_instr   = INVALID_INSTR_ID;

    std::vector<MachineOperand> operands;

    [[nodiscard]] constexpr bool is_cfi_instruction() const noexcept {
        return cfi_data.kind != MachineCFIKind::None || (flags & MachineInstrFlag::CFI) != MachineInstrFlag::None;
    }

    std::uint32_t semantic_provenance = 0;
    std::uint32_t debug_id = 0;

    MachineTrait traits = MachineTrait::None;
    MachineDomain domains = MachineDomain::None;
    MachineAttribute attributes = MachineAttribute::None;
    std::uint64_t intent_hash = 0ULL;
    std::uint32_t epoch = 0;
    std::uint16_t latency_cycles = 1;
    std::uint16_t throughput_reciprocal = 1;

    [[nodiscard]] constexpr bool has_trait(MachineTrait t) const noexcept {
        return (traits & t) != MachineTrait::None;
    }

    void add_trait(MachineTrait t) noexcept {
        traits = traits | t;
    }

    [[nodiscard]] constexpr bool in_domain(MachineDomain d) const noexcept {
        return (domains & d) != MachineDomain::None;
    }

    void add_domain(MachineDomain d) noexcept {
        domains = domains | d;
    }

    [[nodiscard]] constexpr bool has_attribute(MachineAttribute a) const noexcept {
        return (attributes & a) != MachineAttribute::None;
    }

    void add_attribute(MachineAttribute a) noexcept {
        attributes = attributes | a;
    }

    [[nodiscard]] bool has_target_opcode() const noexcept {
        return is_target_opcode(target_opcode_value);
    }

    [[nodiscard]] MachineOpcode target_opcode() const noexcept {
        return target_opcode_value;
    }

    [[nodiscard]] bool has_opcode() const noexcept {
        return is_generic_opcode(opcode) || has_target_opcode();
    }

    [[nodiscard]] bool assign_target_opcode_once(MachineOpcode target_opcode) noexcept {
        if (!is_target_opcode(target_opcode) || has_target_opcode()) {
            return false;
        }
        target_opcode_value = target_opcode;
        return true;
    }

    [[nodiscard]] bool has_semantic_kind() const noexcept {
        return semantic_kind != MachineSemanticKind::None;
    }

    [[nodiscard]] constexpr bool has_flag(MachineInstrFlag flag) const noexcept {
        return (flags & flag) != MachineInstrFlag::None;
    }

    void add_flag(MachineInstrFlag flag) noexcept {
        flags = flags | flag;
    }

    void add_operand(MachineOperand operand) {
        operands.push_back(std::move(operand));
    }

    [[nodiscard]] constexpr bool is_terminator() const noexcept {
        return has_flag(MachineInstrFlag::Terminator) ||
               form == MachineForm::Return ||
               form == MachineForm::Goto ||
               form == MachineForm::BranchCond ||
               form == MachineForm::BranchCompare ||
               opcode == GM_RETURN ||
               opcode == GM_GOTO ||
               opcode == GM_BR_COND ||
               opcode == GM_BR_COMPARE ||
               (opcode >= GM_BR_COMPARE_EQ && opcode <= GM_BR_COMPARE_GE);
    }

    [[nodiscard]] constexpr bool is_branch() const noexcept {
        return has_flag(MachineInstrFlag::Branch) ||
               form == MachineForm::Goto ||
               form == MachineForm::BranchCond ||
               form == MachineForm::BranchCompare ||
               opcode == GM_GOTO ||
               opcode == GM_BR_COND ||
               opcode == GM_BR_COMPARE ||
               (opcode >= GM_BR_COMPARE_EQ && opcode <= GM_BR_COMPARE_GE);
    }

    [[nodiscard]] constexpr bool is_call() const noexcept {
        return has_flag(MachineInstrFlag::Call) ||
               form == MachineForm::Call ||
               opcode == GM_CALL ||
               opcode == GM_CALL_VARIADIC;
    }

    [[nodiscard]] constexpr bool is_return() const noexcept {
        return has_flag(MachineInstrFlag::Return) ||
               form == MachineForm::Return ||
               opcode == GM_RETURN;
    }

    [[nodiscard]] constexpr bool is_load() const noexcept {
        return has_flag(MachineInstrFlag::Load);
    }

    [[nodiscard]] constexpr bool is_store() const noexcept {
        return has_flag(MachineInstrFlag::Store);
    }

    [[nodiscard]] constexpr bool is_barrier() const noexcept {
        return has_flag(MachineInstrFlag::Barrier) ||
               form == MachineForm::Return ||
               form == MachineForm::Goto ||
               opcode == GM_RETURN ||
               opcode == GM_GOTO;
    }

    [[nodiscard]] constexpr bool is_atomic() const noexcept {
        return has_flag(MachineInstrFlag::Atomic);
    }

    [[nodiscard]] constexpr bool may_load() const noexcept {
        return has_flag(MachineInstrFlag::MayLoad) || has_flag(MachineInstrFlag::Load);
    }

    [[nodiscard]] constexpr bool may_store() const noexcept {
        return has_flag(MachineInstrFlag::MayStore) || has_flag(MachineInstrFlag::Store);
    }

    [[nodiscard]] constexpr bool has_side_effects() const noexcept {
        return has_flag(MachineInstrFlag::SideEffect) || has_flag(MachineInstrFlag::HasUnmodeledSideEffect);
    }

    [[nodiscard]] constexpr bool is_commutable() const noexcept {
        return has_flag(MachineInstrFlag::Commutable);
    }

    [[nodiscard]] constexpr bool is_pseudo() const noexcept {
        return has_flag(MachineInstrFlag::Pseudo);
    }

    [[nodiscard]] constexpr bool is_frame_setup() const noexcept {
        return has_flag(MachineInstrFlag::FrameSetup);
    }

    [[nodiscard]] constexpr bool is_frame_destroy() const noexcept {
        return has_flag(MachineInstrFlag::FrameDestroy);
    }

    [[nodiscard]] bool is_phi() const noexcept {
        if (operands.size() >= 3 && (operands.size() % 2 == 1)) {
            if (operands[0].is_def()) {
                for (std::size_t i = 1; i < operands.size(); i += 2) {
                    if (operands[i].kind != MachineOperandKind::BasicBlock) {
                        return false;
                    }
                }
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] ComparisonPredicate comparison_predicate() const noexcept {
        for (const auto& op : operands) {
            if (op.kind == MachineOperandKind::Immediate && (op.flags & 0x80000000u)) {
                return static_cast<ComparisonPredicate>(op.immediate & 0xFF);
            }
        }
        switch (opcode) {
            case GM_ICMP32_EQ: case GM_ICMP64_EQ: case GM_FCMP32_EQ: case GM_FCMP64_EQ: case GM_BR_COMPARE_EQ:
                return ComparisonPredicate::Equal;
            case GM_ICMP32_NE: case GM_ICMP64_NE: case GM_FCMP32_NE: case GM_FCMP64_NE: case GM_BR_COMPARE_NE:
                return ComparisonPredicate::NotEqual;
            case GM_ICMP32_LT: case GM_ICMP64_LT: case GM_FCMP32_LT: case GM_FCMP64_LT: case GM_BR_COMPARE_LT:
                return ComparisonPredicate::SignedLessThan;
            case GM_ICMP32_LE: case GM_ICMP64_LE: case GM_FCMP32_LE: case GM_FCMP64_LE: case GM_BR_COMPARE_LE:
                return ComparisonPredicate::SignedLessEqual;
            case GM_ICMP32_GT: case GM_ICMP64_GT: case GM_FCMP32_GT: case GM_FCMP64_GT: case GM_BR_COMPARE_GT:
                return ComparisonPredicate::SignedGreaterThan;
            case GM_ICMP32_GE: case GM_ICMP64_GE: case GM_FCMP32_GE: case GM_FCMP64_GE: case GM_BR_COMPARE_GE:
                return ComparisonPredicate::SignedGreaterEqual;
            default: break;
        }
        if (semantic_provenance <= 11u) {
            return static_cast<ComparisonPredicate>(semantic_provenance);
        }
        switch (semantic_provenance) {
            case 40u: case 140u: case 0x0380u: return ComparisonPredicate::Equal;
            case 41u: case 141u: case 0x0381u: return ComparisonPredicate::NotEqual;
            case 42u: case 142u: case 0x0382u: return ComparisonPredicate::SignedLessThan;
            case 43u: case 143u: case 0x0383u: return ComparisonPredicate::SignedLessEqual;
            case 44u: case 144u: case 0x0384u: return ComparisonPredicate::SignedGreaterThan;
            case 45u: case 145u: case 0x0385u: return ComparisonPredicate::SignedGreaterEqual;
            default: return ComparisonPredicate::Equal;
        }
    }

    void set_comparison_predicate(ComparisonPredicate pred) noexcept {
        MachineOperand pred_op = MachineOperand::imm(static_cast<std::int64_t>(pred));
        pred_op.flags |= 0x80000000u;
        operands.push_back(pred_op);
        semantic_provenance = static_cast<std::uint32_t>(pred);
    }

    [[nodiscard]] const MachineOperand* get_def() const noexcept {
        for (const auto& op : operands) {
            if (op.is_def()) return &op;
        }
        return nullptr;
    }

    [[nodiscard]] std::vector<const MachineOperand*> get_uses() const {
        std::vector<const MachineOperand*> result;
        for (const auto& op : operands) {
            if (op.is_use() || op.kind == MachineOperandKind::Immediate ||
                op.kind == MachineOperandKind::BasicBlock ||
                op.kind == MachineOperandKind::Memory) {
                result.push_back(&op);
            }
        }
        return result;
    }
};

[[nodiscard]] inline const MachineOperand* mir_def(const MachineInstruction& inst) noexcept {
    return inst.get_def();
}

[[nodiscard]] inline std::vector<const MachineOperand*> mir_uses(const MachineInstruction& inst) {
    return inst.get_uses();
}

using MachineInstr = MachineInstruction;

} // namespace dgtl::machine

export namespace dgtl::mir {
    using dgtl::machine::mir_def;
    using dgtl::machine::mir_uses;
    using dgtl::machine::MachineInstr;
}
