/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 2: BIT-PACKED INTERMEDIATE REPRESENTATION (BIR) - VERSION 2.1
 * PILLAR 4: HARDWARE AFFINITY & BACKEND EMISSION CONSTRAINTS
 * TARGET ENGINE: ISA Descriptors, Register Sets, & Micro-Arch Invariants
 * STANDARDS: C++26 / UCRT64 (Compile-Time Hardware Specialization)
 */

#pragma once
#include <cstdint>
#include <string_view>
#include <vector>
#include "../ir/Global_IR.hpp"

namespace dgtl::ir {

/**
 * @enum ArchType
 * @brief Supported execution environments for the DGTL-Zenith Compiler.
 */
enum class ArchType : uint8_t {
    X86_64_AVX512,  // Tier-1: HFT / AI Infrastructure (Primary)
    AARCH64_SVE,    // Tier-2: Mobile/Edge Performance
    FPGA_VERILOG,   // Low-Latency Custom Silicon
    WASM_SIMD       // High-Performance Web
};

/**
 * @struct RegisterDesc
 * @brief Metadata for physical hardware registers.
 */
struct RegisterDesc {
    uint32_t id;
    std::string_view name;
    uint32_t bit_width;
    bool is_volatile;
    bool is_vector;
};

/**
 * @class TargetMachine
 * @brief Encapsulates the hardware philosophy: "Optimization through Physical Awareness."
 * This class dictates how BIR nodes are lowered based on the specific CPU features.
 */
class alignas(64) TargetMachine {
public:
    struct Capabilities {
        bool has_avx512 = false;
        bool has_amx    = false;
        bool has_fma    = false;
        bool has_bmi2   = false;
        uint32_t cache_line_size = 64;
    };

    explicit TargetMachine(ArchType arch) : m_arch(arch) {
        initialize_target();
    }

    // --- ARCHITECTURE QUERIES ---

    [[nodiscard]] ArchType get_arch() const noexcept { return m_arch; }
    
    /**
     * @brief Logic: Determines if the OpCode is natively supported or requires lowering/emulation.
     */
    [[nodiscard]] bool supports_native(OpCode op) const noexcept {
        if (m_arch == ArchType::X86_64_AVX512) {
            // Native support for 512-bit vector operations
            if (op == OpCode::VEC_ADD_F32_512 || op == OpCode::GATHER_F32_512) 
                return m_caps.has_avx512;
            return true;
        }
        return false;
    }

    /**
     * @brief Returns the physical register count to guide the Register Allocator (Step 3).
     */
    [[nodiscard]] uint32_t get_gp_register_count() const noexcept {
        return (m_arch == ArchType::X86_64_AVX512) ? 16 : 32;
    }

    [[nodiscard]] const Capabilities& get_caps() const noexcept { return m_caps; }

    // --- EMISSION CONSTRAINTS ---

    /**
     * @brief Alignment requirement for memory operations (LOAD/STORE).
     */
    [[nodiscard]] size_t get_preferred_alignment(OpCode op) const noexcept {
        if (op == OpCode::STORE_V512 || op == OpCode::LOAD) return 64;
        return 8;
    }

    /**
     * @brief Philosophy: Deterministic branching heuristic.
     * Guides the compiler on whether to use CMOV (Conditional Move) or JMP.
     */
    [[nodiscard]] bool prefer_branchless() const noexcept {
        return m_arch == ArchType::X86_64_AVX512;
    }

private:
    ArchType m_arch;
    Capabilities m_caps;

    void initialize_target() {
        if (m_arch == ArchType::X86_64_AVX512) {
            m_caps.has_avx512 = true;
            m_caps.has_fma    = true;
            m_caps.has_bmi2   = true;
            m_caps.cache_line_size = 64;
        }
        // Logic: Additional arch initialization as needed
    }
};

/**
 * @struct TargetPipeline
 * @brief Defines the stages of translation from BIR to Machine Code.
 */
struct TargetPipeline {
    bool enable_gvn       = true;
    bool enable_dce       = true;
    bool enable_loop_unroll = true;
    uint32_t optimization_level = 3; // 0 to 3 (DGTL Extreme)
};

} // namespace dgtl::ir

/**
 * @brief Backward Compatibility Alias for Legacy DGTL/Zenith Logic.
 */
namespace dgtl::ir {
    using ArchType      = dgtl::ir::ArchType;
    using TargetMachine = dgtl::ir::TargetMachine;
    using TargetPipeline = dgtl::ir::TargetPipeline;
}
