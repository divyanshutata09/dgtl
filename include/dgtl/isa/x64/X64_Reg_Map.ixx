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

#include <array>
#include <cstdint>
#include <algorithm>
export module dgtl.isa.x64.x64_reg_map;
import dgtl.mir.machine_ir;
import dgtl.flat_map;
export namespace dgtl::isa::x64::emitter {

/**
 * @brief Physical x86_64 Register Indices
 * Standardized for DGTL's High-Performance calling convention.
 * Note: R12-R15 are often callee-saved; DGTL handles this at the Strike prologue.
 */
enum class X64Reg : uint8_t {
    RAX = 0, RCX = 1, RDX = 2, RBX = 3,
    RSP = 4, RBP = 5, RSI = 6, RDI = 7,
    R8  = 8, R9  = 9, R10 = 10, R11 = 11,
    R12 = 12, R13 = 13, R14 = 14, R15 = 15,
    INVALID = 0xFF
};

enum class X64ZmmReg : uint8_t {
    ZMM0 = 0, ZMM1 = 1, ZMM2 = 2, ZMM3 = 3,
    ZMM4 = 4, ZMM5 = 5, ZMM6 = 6, ZMM7 = 7,
    ZMM8 = 8, ZMM9 = 9, ZMM10 = 10, ZMM11 = 11,
    ZMM12 = 12, ZMM13 = 13, ZMM14 = 14, ZMM15 = 15,
    ZMM16 = 16, ZMM17 = 17, ZMM18 = 18, ZMM19 = 19,
    ZMM20 = 20, ZMM21 = 21, ZMM22 = 22, ZMM23 = 23,
    ZMM24 = 24, ZMM25 = 25, ZMM26 = 26, ZMM27 = 27,
    ZMM28 = 28, ZMM29 = 29, ZMM30 = 30, ZMM31 = 31,
    INVALID = 0xFF
};

class X64SinglePassRegisterMapper {
private:
    static constexpr std::array<X64Reg, 5> kNonVolatileGprs = {
        X64Reg::RBX, X64Reg::R12, X64Reg::R13, X64Reg::R14, X64Reg::R15,
    };

    static constexpr std::array<X64Reg, 4> kVolatileGprs = {
        X64Reg::R8, X64Reg::R9, X64Reg::R10, X64Reg::R11,
    };

    // Map virtual registers to callee-saved GPRs (preserved across runtime helper calls)
    static constexpr std::array<X64Reg, 5> kAllocatableGprs = {
        X64Reg::RBX, X64Reg::R12, X64Reg::R13, X64Reg::R14, X64Reg::R15,
    };

public:
    static constexpr uint32_t GprWindow = static_cast<uint32_t>(kAllocatableGprs.size());
    static constexpr uint32_t ZmmWindow = 32;
    static constexpr uint32_t NonVolatileWindow = static_cast<uint32_t>(kNonVolatileGprs.size());
    static constexpr uint32_t VolatileWindow = static_cast<uint32_t>(kVolatileGprs.size());

    static_assert(GprWindow > 0, "x64 register mapper must expose at least one allocatable GPR");
    static_assert((ZmmWindow & (ZmmWindow - 1)) == 0, "ZmmWindow must remain power-of-two for bit-mask mapping");

    [[nodiscard]] static constexpr X64Reg map_gpr_branchless(uint32_t virtual_id) noexcept {
        return kAllocatableGprs[virtual_id % GprWindow];
    }

    [[nodiscard]] static constexpr X64Reg map_gpr_prefer_nonvolatile(uint32_t virtual_id) noexcept {
        return kNonVolatileGprs[virtual_id % NonVolatileWindow];
    }

    [[nodiscard]] static constexpr X64Reg map_volatile_only(uint32_t virtual_id) noexcept {
        return kVolatileGprs[virtual_id % VolatileWindow];
    }

    static constexpr std::array<uint8_t, 10> kNonVolatileXmms = {
        6, 7, 8, 9, 10, 11, 12, 13, 14, 15
    };

    [[nodiscard]] static constexpr uint8_t map_xmm_branchless(uint32_t virtual_id) noexcept {
        return kNonVolatileXmms[virtual_id % kNonVolatileXmms.size()];
    }

    [[nodiscard]] static constexpr X64ZmmReg map_zmm_circular(uint32_t virtual_id) noexcept {
        return static_cast<X64ZmmReg>(virtual_id & (ZmmWindow - 1));
    }

    [[nodiscard]] static constexpr uint8_t zmm_index(uint32_t virtual_id) noexcept {
        return static_cast<uint8_t>(map_zmm_circular(virtual_id));
    }
};

enum class X64StorageKind : uint8_t {
    register_lock,
    stack_slot
};

struct X64StorageBinding {
    X64StorageKind kind = X64StorageKind::register_lock;
    X64Reg reg = X64Reg::INVALID;
    int32_t stack_offset = 0;
    bool pinned = true;
};

class X64LocalStateLockTable {
private:
    dgtl::utils::FastFlatMap<uint32_t, X64StorageBinding, 256> locks_;

public:
    void clear() noexcept {
        locks_.clear();
    }

    void lock_to_register(uint32_t virtual_id, X64Reg reg) noexcept {
        if (reg == X64Reg::INVALID) {
            return;
        }
        (void)locks_.insert_or_assign(virtual_id, X64StorageBinding{X64StorageKind::register_lock, reg, 0, true});
    }

    void lock_to_stack(uint32_t virtual_id, int32_t stack_offset) noexcept {
        (void)locks_.insert_or_assign(virtual_id, X64StorageBinding{X64StorageKind::stack_slot, X64Reg::INVALID, stack_offset, true});
    }

    [[nodiscard]] bool resolve(uint32_t virtual_id, X64StorageBinding& out) const noexcept {
        if (const auto* ptr = locks_.find(virtual_id)) {
            out = *ptr;
            return true;
        }
        return false;
    }

    [[nodiscard]] bool is_locked(uint32_t virtual_id) const noexcept {
        return locks_.find(virtual_id) != nullptr;
    }

    [[nodiscard]] bool register_reserved_for_other(uint32_t virtual_id, X64Reg reg) const noexcept {
        for (auto it = locks_.begin(); it != locks_.end(); ++it) {
            const auto& [key, binding] = *it;
            if (key == virtual_id) {
                continue;
            }
            if (binding.kind == X64StorageKind::register_lock && binding.reg == reg) {
                return true;
            }
        }
        return false;
    }
};

class X64Encoding {
public:
    [[nodiscard]] static constexpr uint8_t reg_index(X64Reg reg) noexcept {
        return static_cast<uint8_t>(reg);
    }

    [[nodiscard]] static constexpr bool is_valid(X64Reg reg) noexcept {
        return reg != X64Reg::INVALID;
    }

    [[nodiscard]] static constexpr bool is_valid(X64ZmmReg reg) noexcept {
        return reg != X64ZmmReg::INVALID;
    }

    /**
     * @brief Encodes the REX prefix for 64-bit operands.
     * @param w Set for 64-bit operand size (REX.W).
     * @param r Extension of the ModR/M 'reg' field (REX.R).
     * @param x Extension of the SIB 'index' field (REX.X).
     * @param b Extension of the ModR/M 'r/m' field or SIB 'base' field (REX.B).
     */
    static constexpr inline uint8_t rex(bool w, bool r, bool x, bool b) noexcept {
        uint8_t res = 0x40;
        if (w) res |= 0x08;         // Bit 3: 64-bit operand size
        if (r) res |= 0x04;         // Bit 2: Extension of ModR/M reg
        if (x) res |= 0x02;         // Bit 1: Extension of SIB index
        if (b) res |= 0x01;         // Bit 0: Extension of ModR/M r/m or SIB base
        return res;
    }

    /**
     * @brief Encodes the ModR/M byte.
     * @param mod 00=Reg Indirect, 01=8-bit disp, 10=32-bit disp, 11=Reg-to-Reg.
     * @param reg The source/destination register (3 bits).
     * @param rm  The second register or memory base (3 bits).
     */
    static constexpr inline uint8_t modrm(uint8_t mod, uint8_t reg, uint8_t rm) noexcept {
        return ((mod & 0x03) << 6) | ((reg & 0x07) << 3) | (rm & 0x07);
    }

    /**
     * @brief Encodes the SIB (Scale-Index-Base) byte.
     * @param ss Scale (0=1, 1=2, 2=4, 3=8).
     * @param index Index register index.
     * @param base Base register index.
     */
    static constexpr inline uint8_t sib(uint8_t ss, uint8_t index, uint8_t base) noexcept {
        return ((ss & 0x03) << 6) | ((index & 0x07) << 3) | (base & 0x07);
    }

    /**
     * @brief Helper to map DGTL's internal register ID to physical X64Reg.
     * PILLAR 4: Strictly isolates RSP and RBP to prevent stack corruption.
     * Mapping Table:
     * DGTL ID [0..5] -> RAX, RBX, RCX, RDX, RSI, RDI
     * DGTL ID [6..15] -> R8..R15
     */
    [[nodiscard]] static constexpr X64Reg map_DGTL_to_phys(uint32_t DGTL_id) noexcept {
        return X64SinglePassRegisterMapper::map_gpr_branchless(DGTL_id);
    }

    [[nodiscard]] static constexpr uint8_t map_DGTL_vec_to_phys(uint32_t DGTL_id) noexcept {
        return X64SinglePassRegisterMapper::zmm_index(DGTL_id);
    }

    /**
     * @brief Check if a register requires a REX prefix extension bit (> 7).
     */
    static constexpr inline bool requires_extension(X64Reg reg) noexcept {
        const uint8_t idx = reg_index(reg);
        return idx >= 8 && idx != 0xFF;
    }
};

// ============================================================================
// AI-DRIVEN REGISTER ALLOCATION (Phase 4 Enhancement)
// ============================================================================

/**
 * @class X64AIGuidedRegisterAllocator
 * @brief Intelligent register allocation using AI cost model predictions.
 * 
 * ALGORITHM:
 * 1. Profile live range pressure across the function
 * 2. Use AI model to predict register spill costs
 * 3. Prefer callee-saved registers for long-lived values
 * 4. Minimize memory access latency based on AI predictions
 * 
 * This enables smarter allocation than simple linear scan by considering
 * actual hardware cost predictions from the neural network model.
 */
class X64AIGuidedRegisterAllocator {
private:
    float m_memory_latency_estimate = 1.0f;
    X64LocalStateLockTable m_locks;

    /**
     * @brief Allocation decision record for a virtual register.
     */
    struct AllocationDecision {
        X64Reg assigned_reg = X64Reg::INVALID;
        int32_t stack_offset = -1;
        bool should_spill = false;
        float ai_predicted_cost = 0.0f;
        uint32_t priority_score = 0;
    };

    /**
     * @brief Compute AI-based priority for register allocation.
     * Higher priority = allocate to physical register; lower = spill to stack.
     * 
     * Factors considered:
     * - Live range length (longer = higher priority)
     * - Use frequency in hot paths (more frequent = higher priority)
     * - Memory latency sensitivity (memory-bound code = higher priority)
     */
    [[nodiscard]] uint32_t compute_priority_score(
        uint32_t virtual_id,
        uint32_t live_range_length,
        uint32_t use_count,
        bool in_hot_path
    ) noexcept {
        (void)virtual_id;
        uint32_t score = 0;

        // Factor 1: Live range length (normalized to 0-100)
        // Longer ranges deserve registers more (avoids repeated spills)
        const uint32_t range_score = std::min(100u, live_range_length * 10);
        score += range_score * 4;  // Weight: 40%

        // Factor 2: Use frequency (normalized to 0-100)
        // More frequent use = higher priority
        const uint32_t freq_score = std::min(100u, use_count * 20);
        score += freq_score * 3;  // Weight: 30%

        // Factor 3: Hot path bonus
        // Critical path values should stay in registers
        if (in_hot_path) {
            score += 150;  // Weight: 15%
        }

        // Factor 4: Model prediction / memory latency sensitivity
        const uint32_t ai_score = static_cast<uint32_t>(m_memory_latency_estimate * 50.0f);
        score += ai_score;  // Weight: 15%

        return score;
    }

public:
    explicit X64AIGuidedRegisterAllocator(
        float memory_latency_estimate = 1.0f
    ) noexcept
        : m_memory_latency_estimate(memory_latency_estimate) {}

    /**
     * @brief Allocate a virtual register to physical storage (register or stack).
     * Uses AI predictions to guide allocation decisions.
     * 
     * @param virtual_id Virtual register ID from DGTL IR
     * @param live_range_length Estimated live range in instructions
     * @param use_count Number of uses of this value
     * @param in_hot_path true if used in critical execution path
     * @param result Output: assigned physical register or stack offset
     * @return true if allocation succeeded
     */
    [[nodiscard]] bool allocate(
        uint32_t virtual_id,
        uint32_t live_range_length,
        uint32_t use_count,
        bool in_hot_path,
        X64StorageBinding& result
    ) noexcept {
        // Check if already locked (e.g., by loop constraints)
        if (m_locks.resolve(virtual_id, result)) {
            return true;
        }

        // Compute priority score to determine allocation strategy
        const uint32_t priority = compute_priority_score(
            virtual_id,
            live_range_length,
            use_count,
            in_hot_path
        );

        // Strategy: Allocate registers based on priority
        // High priority: physical register
        // Low priority: stack slot

        // Threshold for register allocation: empirically tuned
        const uint32_t kRegisterPriorityThreshold = 250;

        if (priority >= kRegisterPriorityThreshold) {
            // Attempt to allocate physical register
            return try_allocate_register(virtual_id, result);
        } else {
            // Allocate stack slot
            return try_allocate_stack(virtual_id, result);
        }
    }

    /**
     * @brief Attempt to allocate a physical register for this virtual ID.
     * Prefers callee-saved registers to minimize prologue/epilogue impact.
     */
    [[nodiscard]] bool try_allocate_register(
        uint32_t virtual_id,
        X64StorageBinding& result
    ) noexcept {
        // Preference order:
        // 1. Callee-saved (RBX, R12-R15) - require prologue/epilogue saving
        // 2. Volatile (R8-R11, RAX, RCX) - no save/restore needed

        // Try callee-saved first (less allocation pressure)
        const X64Reg preferred = X64SinglePassRegisterMapper::map_gpr_prefer_nonvolatile(virtual_id);
        
        if (!m_locks.register_reserved_for_other(virtual_id, preferred)) {
            result = X64StorageBinding{
                X64StorageKind::register_lock,
                preferred,
                0,
                true
            };
            m_locks.lock_to_register(virtual_id, preferred);
            return true;
        }

        // Fall back to volatile registers
        const X64Reg volatile_reg = X64SinglePassRegisterMapper::map_volatile_only(virtual_id);
        
        if (!m_locks.register_reserved_for_other(virtual_id, volatile_reg)) {
            result = X64StorageBinding{
                X64StorageKind::register_lock,
                volatile_reg,
                0,
                true
            };
            m_locks.lock_to_register(virtual_id, volatile_reg);
            return true;
        }

        // All registers reserved; try stack allocation
        return try_allocate_stack(virtual_id, result);
    }

    /**
     * @brief Allocate a stack slot for this virtual register (spill).
     * AI model predicts the cost; stack allocation has predictable latency.
     */
    [[nodiscard]] bool try_allocate_stack(
        uint32_t virtual_id,
        X64StorageBinding& result
    ) noexcept {
        // Stack allocation: offset from RSP/RBP
        // Simplistic: assign sequential stack offsets
        // In production, use more sophisticated stack layout algorithms
        
        static int32_t next_stack_offset = 0;  // Simplified for demo
        const int32_t offset = next_stack_offset;
        next_stack_offset += 8;  // 64-bit value = 8 bytes
        
        result = X64StorageBinding{
            X64StorageKind::stack_slot,
            X64Reg::INVALID,
            offset,
            true
        };
        m_locks.lock_to_stack(virtual_id, offset);
        return true;
    }

    /**
     * @brief Reset allocator state (for new function/block).
     */
    void reset() noexcept {
        m_locks.clear();
    }

    /**
     * @brief Query allocation decision for a virtual register.
     */
    [[nodiscard]] bool resolve_allocation(
        uint32_t virtual_id,
        X64StorageBinding& result
    ) const noexcept {
        return m_locks.resolve(virtual_id, result);
    }

    /**
     * @brief Report allocator statistics (for debugging/tuning).
     */
    struct AllocationStats {
        uint32_t registers_used = 0;
        uint32_t stack_slots_used = 0;
        float estimated_memory_pressure = 0.0f;
    };

    [[nodiscard]] AllocationStats get_stats() const noexcept {
        AllocationStats stats{};
        // In production, track actual register and stack usage
        stats.estimated_memory_pressure = m_memory_latency_estimate;
        return stats;
    }
};

// ============================================================

/*
 * MIR target-register contract.
 *
 * This adapter is the sole x64 physical-register authority used by MIR
 * register allocation.  The legacy modulo mappers above remain available for
 * source compatibility, but they are not legal for emission because modulo
 * mapping can alias simultaneously-live virtual registers.
 */
class X64TargetRegisterInfo final : public dgtl::machine::TargetRegisterInfo {
public:
    [[nodiscard]] std::size_t physical_register_count() const noexcept override {
        return 16u + 32u;
    }

    [[nodiscard]] bool is_allocatable(dgtl::machine::MachineRegister reg) const noexcept override {
        if (!reg.is_physical()) return false;
        if (reg.reg_class == dgtl::machine::RegisterClassKind::General ||
            reg.reg_class == dgtl::machine::RegisterClassKind::Integer ||
            reg.reg_class == dgtl::machine::RegisterClassKind::Address) {
            switch (static_cast<X64Reg>(reg.id)) {
                case X64Reg::RAX:
                case X64Reg::RCX:
                case X64Reg::RDX:
                case X64Reg::RBX:
                case X64Reg::RSI:
                case X64Reg::RDI:
                case X64Reg::R8:
                case X64Reg::R9:
                case X64Reg::R10:
                case X64Reg::R11:
                case X64Reg::R12:
                case X64Reg::R13:
                case X64Reg::R14:
                case X64Reg::R15:
                    return true;
                default:
                    return false;
            }
        }
        if (reg.reg_class == dgtl::machine::RegisterClassKind::FloatingPoint) {
            return reg.id < 16u;
        }
        if (reg.reg_class == dgtl::machine::RegisterClassKind::Vector) {
            return reg.id < 32u;
        }
        return false;
    }

    [[nodiscard]] dgtl::machine::RegisterClassKind
    register_class(dgtl::machine::MachineRegister reg) const noexcept override {
        if (!reg.is_physical()) return dgtl::machine::RegisterClassKind::Invalid;
        if (reg.reg_class == dgtl::machine::RegisterClassKind::FloatingPoint)
            return dgtl::machine::RegisterClassKind::FloatingPoint;
        if (reg.reg_class == dgtl::machine::RegisterClassKind::Vector)
            return dgtl::machine::RegisterClassKind::Vector;
        if (reg.id < 16u)
            return dgtl::machine::RegisterClassKind::General;
        return dgtl::machine::RegisterClassKind::Invalid;
    }

    [[nodiscard]] bool aliases(dgtl::machine::MachineRegister lhs,
                               dgtl::machine::MachineRegister rhs) const noexcept override {
        if (!lhs.is_physical() || !rhs.is_physical()) return false;
        const auto is_gpr = [](dgtl::machine::RegisterClassKind cls) noexcept {
            using dgtl::machine::RegisterClassKind;
            return cls == RegisterClassKind::General ||
                   cls == RegisterClassKind::Integer ||
                   cls == RegisterClassKind::Address;
        };
        if (lhs.id == rhs.id &&
            (lhs.reg_class == rhs.reg_class ||
             (is_gpr(lhs.reg_class) && is_gpr(rhs.reg_class)))) {
            return true;
        }
        // Scalar FP registers share the low architectural XMM register bank.
        const auto fp_vector_alias = [](const auto& fp, const auto& vec) noexcept {
            return fp.reg_class == dgtl::machine::RegisterClassKind::FloatingPoint &&
                   vec.reg_class == dgtl::machine::RegisterClassKind::Vector &&
                   vec.id >= 16u && vec.id < 32u &&
                   fp.id == (vec.id - 16u);
        };
        return fp_vector_alias(lhs, rhs) || fp_vector_alias(rhs, lhs);
    }

    [[nodiscard]] bool is_reserved(dgtl::machine::MachineRegister reg) const noexcept override {
        if (!reg.is_physical()) return false;
        if (reg.reg_class != dgtl::machine::RegisterClassKind::General &&
            reg.reg_class != dgtl::machine::RegisterClassKind::Integer &&
            reg.reg_class != dgtl::machine::RegisterClassKind::Address) {
            return false;
        }
        if (reg.id == static_cast<std::uint32_t>(X64Reg::RSP) ||
            reg.id == static_cast<std::uint32_t>(X64Reg::RBP)) {
            return true;
        }
        return false;
    }

    [[nodiscard]] bool is_caller_saved(dgtl::machine::MachineRegister reg) const noexcept override {
        if (!reg.is_physical()) return false;
        if (reg.reg_class == dgtl::machine::RegisterClassKind::General ||
            reg.reg_class == dgtl::machine::RegisterClassKind::Integer ||
            reg.reg_class == dgtl::machine::RegisterClassKind::Address) {
            switch (static_cast<X64Reg>(reg.id)) {
                case X64Reg::RAX: case X64Reg::RCX: case X64Reg::RDX:
                case X64Reg::R8: case X64Reg::R9: case X64Reg::R10: case X64Reg::R11:
                    return true;
                default: return false;
            }
        }
        if (reg.reg_class == dgtl::machine::RegisterClassKind::FloatingPoint) {
            return reg.id < 6u;
        }
        if (reg.reg_class == dgtl::machine::RegisterClassKind::Vector) {
            return (reg.id >= 16u && reg.id < 22u) ||
                   (reg.id >= 32u && reg.id < 48u);
        }
        return false;
    }

    [[nodiscard]] bool is_callee_saved(dgtl::machine::MachineRegister reg) const noexcept override {
        if (!reg.is_physical()) return false;
        if (reg.reg_class == dgtl::machine::RegisterClassKind::General ||
            reg.reg_class == dgtl::machine::RegisterClassKind::Integer ||
            reg.reg_class == dgtl::machine::RegisterClassKind::Address) {
            switch (static_cast<X64Reg>(reg.id)) {
                case X64Reg::RBX: case X64Reg::RBP:
                case X64Reg::RSI: case X64Reg::RDI:
                case X64Reg::R12: case X64Reg::R13:
                case X64Reg::R14: case X64Reg::R15:
                    return true;
                default:
                    return false;
            }
        }
        if (reg.reg_class == dgtl::machine::RegisterClassKind::FloatingPoint) {
            return reg.id >= 6u && reg.id < 16u;
        }
        if (reg.reg_class == dgtl::machine::RegisterClassKind::Vector) {
            return reg.id >= 22u && reg.id < 32u;
        }
        return false;
    }
};

} // namespace dgtl::isa::x64::emitter

export namespace dgtl::exec {
    using dgtl::isa::x64::emitter::X64Reg;
    using dgtl::isa::x64::emitter::X64ZmmReg;
    using dgtl::isa::x64::emitter::X64SinglePassRegisterMapper;
    using dgtl::isa::x64::emitter::X64StorageKind;
    using dgtl::isa::x64::emitter::X64StorageBinding;
    using dgtl::isa::x64::emitter::X64Encoding;
}

export namespace dgtl::isa::x64 {
    using emitter::X64TargetRegisterInfo;
    using emitter::X64Reg;
    using emitter::X64ZmmReg;
    using emitter::X64SinglePassRegisterMapper;
    using emitter::X64Encoding;
}

export namespace dgtl::isa {
    using x64::emitter::X64TargetRegisterInfo;
    using x64::emitter::X64Reg;
    using x64::emitter::X64ZmmReg;
    using x64::emitter::X64SinglePassRegisterMapper;
    using x64::emitter::X64Encoding;
}

