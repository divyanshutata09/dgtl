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

/* * DGTL (Deep General-purpose Transformation Layer) version 1.0.0 : THE COMPILER SPEC
 * STEP 2: BIT-PACKED INTERMEDIATE REPRESENTATION (BIR) - VERSION 2.1
 * PILLAR 4: HARDWARE AFFINITY & BACKEND EMISSION CONSTRAINTS
 * TARGET ENGINE: ISA Descriptors, Register Sets, & Micro-Arch Invariants
 * STANDARDS: C++26 / UCRT64 (Compile-Time Hardware Specialization)
 */

module;

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <numeric>

export module dgtl.target;

import dgtl.ir_enums;

export namespace dgtl::core {

using OpCode = dgtl::ir::OpCode;

// ============================================================
// Architecture & SIMD Enums (Merged from both files)
// ============================================================

enum class ArchType : uint8_t {
    X86_64_AVX512,
    X86_64_AVX2,
    AARCH64_SVE,
    ARM32_NEON,
    RISCV64_RVV,
    WASM_SIMD,
    CUDA_PTX,
    ROCM_AMDGCN,
    FPGA_VERILOG
};

enum class TargetFamily : uint8_t {
    X64,
    ARM64,
    ARM32,
    WASM,
    RISCV64,
    CUDA,
    ROCm,
    FPGA
};

[[nodiscard]] constexpr std::string_view target_family_name(TargetFamily family) noexcept {
    switch (family) {
        case TargetFamily::X64:    return "x64";
        case TargetFamily::ARM64:  return "arm64";
        case TargetFamily::ARM32:  return "arm32";
        case TargetFamily::WASM:   return "wasm";
        case TargetFamily::RISCV64:return "riscv64";
        case TargetFamily::CUDA:   return "cuda";
        case TargetFamily::ROCm:   return "rocm";
        case TargetFamily::FPGA:   return "fpga";
    }
    return "unknown";
}

/**
 * @brief Semantic target capability contract shared by target consumers.
 *
 * These features describe the compilation target, not the host CPU on which
 * DGTL itself is running. Host detection remains represented by
 * HardwareFeatures below. Keeping the two concepts separate prevents
 * accidental host-feature leakage into cross-compilation.
 */
enum class TargetFeature : uint8_t {
    Scalar32 = 0,
    Scalar64,
    Vector,
    VectorMemory,
    FMA,
    Atomics,
    Concurrency,
    Syscall,
    AVX2,
    AVX512,
    BMI2,
    SVE,
    NEON,
    VFP,
    RVV,
    CUDA,
    ROCm,
    Tensor,
    WasmSIMD,
    ObjectMachO,
    AMX,
    Count
};

class TargetFeatureSet {
public:
    using mask_type = uint64_t;

    constexpr TargetFeatureSet() noexcept = default;
    constexpr explicit TargetFeatureSet(mask_type mask) noexcept : mask_(mask) {}

    [[nodiscard]] constexpr bool has(TargetFeature feature) const noexcept {
        const auto bit = static_cast<unsigned>(feature);
        return bit < static_cast<unsigned>(TargetFeature::Count)
            && ((mask_ >> bit) & 1ull) != 0ull;
    }

    constexpr void add(TargetFeature feature) noexcept {
        const auto bit = static_cast<unsigned>(feature);
        if (bit < static_cast<unsigned>(TargetFeature::Count)) {
            mask_ |= (1ull << bit);
        }
    }

    constexpr void remove(TargetFeature feature) noexcept {
        const auto bit = static_cast<unsigned>(feature);
        if (bit < static_cast<unsigned>(TargetFeature::Count)) {
            mask_ &= ~(1ull << bit);
        }
    }

    [[nodiscard]] constexpr mask_type mask() const noexcept { return mask_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return mask_ == 0; }

    friend constexpr bool operator==(TargetFeatureSet, TargetFeatureSet) noexcept = default;

private:
    mask_type mask_ = 0;
};

/**
 * @brief Distinguishes the target endpoint from its ISA identity.
 *
 * Object format is deliberately separate from architecture. For example,
 * Mach-O is an object format, not a CPU ISA.
 */
enum class TargetKind : uint8_t {
    CPU = 0,
    GPU = 1,
    Accelerator = 2,
    VirtualMachine = 3,
    FPGA = 4
};

enum class ObjectFormat : uint8_t {
    None = 0,
    ELF,
    PE,
    MachO,
    Wasm
};

/**
 * @brief Stable target identity carried by TargetMachine.
 *
 * Strings are non-owning and intended for static target descriptors.
 */
struct TargetIdentity {
    TargetKind kind = TargetKind::CPU;
    ObjectFormat object_format = ObjectFormat::None;
    ArchType arch = ArchType::X86_64_AVX2;
    TargetFamily family = TargetFamily::X64;
    std::string_view canonical_name{};
    std::string_view cpu{};
    std::string_view subtarget{};
    TargetFeatureSet features{};

    [[nodiscard]] constexpr bool has(TargetFeature feature) const noexcept {
        return features.has(feature);
    }
};


enum class SimdTier : uint8_t {
    Scalar = 0,  // No SIMD or non-x86
    AVX2   = 1,  // 256-bit YMM dual-pump
    AVX512 = 2   // 512-bit ZMM (requires avx512f + avx512dq)
};

enum class SimdExecutionProfile : uint8_t {
    Scalar = 0,
    AVX2DualPump = 1,
    AVX512Wide = 2
};

// ============================================================
// Feature Detection Structures
// ============================================================

struct X86SimdCaps {
    bool has_avx2 = false;
    bool has_avx512 = false;
    bool has_fma = false;
    bool has_bmi2 = false;
    bool has_tsc = false;
};

/**
 * @struct HardwareFeatures
 * @brief Consolidated feature set for internal state.
 */
struct HardwareFeatures {
    SimdTier simd      = SimdTier::Scalar;
    bool     has_tsc   = false;
    bool     has_avx2  = false;
    bool     has_avx512 = false;
    bool     has_fma   = false;
    bool     has_bmi2  = false;
    bool     has_amx   = false;
    uint32_t cache_line_size = 64;
    uint64_t preferred_affinity_mask = 0x1ull;
    uint32_t preferred_core_index = 0;
};

struct AffinityPolicy {
    uint64_t mask = 0x1ull;
    uint32_t preferred_core = 0;
    bool realtime_priority = false;
};

[[nodiscard]] AffinityPolicy default_high_performance_affinity() noexcept;
[[nodiscard]] bool apply_high_performance_affinity(const AffinityPolicy& policy = default_high_performance_affinity()) noexcept;

// ============================================================
// Feature Detection (Forward Declarations)
// ============================================================
// GCC 15 ICE workaround (cp/module.cc:9996):
// __builtin_cpu_supports(), __rdtsc(), and AVX intrinsics inside *inline*
// exported functions trigger an ICE because GCC's module serializer cannot
// round-trip the underlying compiler-builtin/ifunc tree nodes through the
// BMI.  All functions containing those calls are declared here but defined
// out-of-line in Target.cpp (the module implementation unit).
[[nodiscard]] std::uint64_t read_tsc() noexcept;
void stabilize() noexcept;
[[nodiscard]] X86SimdCaps detect_x86_simd_caps() noexcept;
[[nodiscard]] HardwareFeatures detect_hardware_impl() noexcept;

[[nodiscard]] inline HardwareFeatures detect_hardware() noexcept {
    return detect_hardware_impl();
}
void prime_preferred_simd_units(std::size_t iterations = 2048) noexcept;

[[nodiscard]] inline SimdExecutionProfile preferred_simd_execution_profile() noexcept {
    const HardwareFeatures hw = detect_hardware();
    // AVX-512 is the wider profile; test it before AVX2.
    if (hw.has_avx512) {
        return SimdExecutionProfile::AVX512Wide;
    }
    if (hw.has_avx2) {
        return SimdExecutionProfile::AVX2DualPump;
    }
    return SimdExecutionProfile::Scalar;
}

[[nodiscard]] inline SimdTier preferred_simd_tier() noexcept {
    switch (preferred_simd_execution_profile()) {
        case SimdExecutionProfile::AVX2DualPump:
            return SimdTier::AVX2;
        case SimdExecutionProfile::AVX512Wide:
            return SimdTier::AVX512;
        case SimdExecutionProfile::Scalar:
        default:
            return SimdTier::Scalar;
    }
}

[[nodiscard]] inline ArchType select_best_x86_arch() noexcept {
    const X86SimdCaps caps = detect_x86_simd_caps();
    return caps.has_avx512 ? ArchType::X86_64_AVX512 : ArchType::X86_64_AVX2;
}

[[nodiscard]] inline ArchType select_preferred_x86_arch() noexcept {
    return preferred_simd_tier() == SimdTier::AVX512 ? ArchType::X86_64_AVX512 : ArchType::X86_64_AVX2;
}
// prime_preferred_simd_units is defined out-of-line in Target.cpp.

// ============================================================
// TargetMachine Class (Merged Architecture Awareness)
// ============================================================

struct RegisterDesc {
    uint32_t id = 0;
    [[maybe_unused]] std::string_view name{};
    uint32_t bit_width = 0;
    bool is_volatile = false;
    bool is_vector = false;
};

enum class RegisterBank : uint8_t {
    GeneralPurpose = 0,
    Vector = 1,
    Predicate = 2,
    Special = 3,
};

enum class CallingConventionKind : uint8_t {
    SystemV_x64 = 0,
    Windows_x64,
    AAPCS64,
    AAPCS32,
    RiscV64,
    WasmCABI,
    CUDADevice,
    ROCmDevice,
    BareMetal,
};

struct TargetRegisterInfo {
    uint16_t id = 0;
    [[maybe_unused]] std::string_view name{};
    RegisterBank bank = RegisterBank::GeneralPurpose;
    uint16_t bit_width = 0;
    bool caller_saved = false;
    bool allocatable = true;
};

struct CallingConventionDesc {
    CallingConventionKind kind = CallingConventionKind::BareMetal;
    std::string_view name{};
    std::array<uint16_t, 8> integer_arg_registers{};
    std::array<uint16_t, 8> vector_arg_registers{};
    uint8_t integer_arg_count = 0;
    uint8_t vector_arg_count = 0;
    uint16_t integer_return_register = 0;
    uint16_t vector_return_register = 0;
    uint16_t stack_alignment = 16;
    uint16_t shadow_space_bytes = 0;

    [[nodiscard]] std::string_view get_name() const noexcept { return name; }
    [[nodiscard]] uint16_t get_integer_arg_reg(size_t index) const noexcept {
        return (index < integer_arg_registers.size()) ? integer_arg_registers[index] : 0;
    }
    [[nodiscard]] uint16_t get_vector_arg_reg(size_t index) const noexcept {
        return (index < vector_arg_registers.size()) ? vector_arg_registers[index] : 0;
    }
};

struct InstructionDesc {
    OpCode opcode = static_cast<OpCode>(0);
    uint8_t latency_cycles = 1;
    uint8_t reciprocal_throughput = 1;
    uint16_t legal_scalar_width_mask = 0;
    bool supports_fusion = false;
};

class TargetDescription {
public:
    static constexpr uint16_t kWidth32 = 1u << 0;
    static constexpr uint16_t kWidth64 = 1u << 1;
    static constexpr uint16_t kWidth128 = 1u << 2;
    static constexpr uint16_t kWidth256 = 1u << 3;
    static constexpr uint16_t kWidth512 = 1u << 4;

    [[nodiscard]] static TargetDescription for_arch(
        ArchType arch,
        CallingConventionKind calling_convention = CallingConventionKind::SystemV_x64);

    [[nodiscard]] ArchType arch() const noexcept { return arch_; }
    [[nodiscard]] CallingConventionKind calling_convention_kind() const noexcept {
        return calling_convention_.kind;
    }
    [[nodiscard]] std::span<const TargetRegisterInfo> general_purpose_registers() const noexcept {
        return std::span<const TargetRegisterInfo>(gp_registers_.data(), gp_register_count_);
    }
    [[nodiscard]] std::span<const TargetRegisterInfo> vector_registers() const noexcept {
        return std::span<const TargetRegisterInfo>(vector_registers_.data(), vector_register_count_);
    }
    [[nodiscard]] const CallingConventionDesc& default_calling_convention() const noexcept {
        return calling_convention_;
    }
    [[nodiscard]] std::optional<InstructionDesc> instruction_desc(OpCode opcode) const noexcept {
        const auto it = std::find_if(instruction_table_.begin(), instruction_table_.begin() + instruction_count_,
                                     [opcode](const auto& desc) { return desc.opcode == opcode; });
        if (it != instruction_table_.begin() + instruction_count_) {
            return *it;
        }
        return std::nullopt;
    }
    [[nodiscard]] uint8_t instruction_latency(OpCode opcode) const noexcept {
        const auto desc = instruction_desc(opcode);
        return desc.has_value() ? desc->latency_cycles : 1;
    }
    [[nodiscard]] uint8_t instruction_reciprocal_throughput(OpCode opcode) const noexcept {
        const auto desc = instruction_desc(opcode);
        return desc.has_value() ? desc->reciprocal_throughput : 1;
    }
    [[nodiscard]] bool instruction_supports_fusion(OpCode opcode) const noexcept {
        const auto desc = instruction_desc(opcode);
        return desc.has_value() && desc->supports_fusion;
    }
    [[nodiscard]] bool supports_scalar_width(uint32_t bits) const noexcept {
        const uint16_t mask = width_to_mask(bits);
        return (supported_scalar_widths_ & mask) != 0;
    }
    [[nodiscard]] bool supports_vector_width(uint32_t bits) const noexcept {
        const uint16_t mask = width_to_mask(bits);
        return (supported_vector_widths_ & mask) != 0;
    }
    [[nodiscard]] bool supports_fma() const noexcept {
        return supports_fma_;
    }

    /**
     * @brief Returns whether the target has a direct instruction-table entry.
     *
     * This is the canonical target-local opcode legality query. Higher-level
     * legalization may still lower an opcode to another form; a false result
     * therefore means "not directly supported by this target description".
     */
    [[nodiscard]] bool supports_opcode(OpCode opcode) const noexcept {
        return instruction_desc(opcode).has_value();
    }

    /** @brief True when the vector ISA uses a scalable vector length. */
    [[nodiscard]] bool has_scalable_vectors() const noexcept {
        return scalable_vectors_;
    }
    [[nodiscard]] float average_instruction_latency() const noexcept {
        if (instruction_count_ == 0) return 1.0f;
        const uint32_t total = std::accumulate(instruction_table_.begin(), instruction_table_.begin() + instruction_count_, 0u,
                                               [](uint32_t sum, const auto& desc) { return sum + desc.latency_cycles; });
        return static_cast<float>(total) / static_cast<float>(instruction_count_);
    }
    [[nodiscard]] float average_instruction_reciprocal_throughput() const noexcept {
        if (instruction_count_ == 0) return 1.0f;
        const uint32_t total = std::accumulate(instruction_table_.begin(), instruction_table_.begin() + instruction_count_, 0u,
                                               [](uint32_t sum, const auto& desc) { return sum + desc.reciprocal_throughput; });
        return static_cast<float>(total) / static_cast<float>(instruction_count_);
    }

private:
    ArchType arch_ = ArchType::X86_64_AVX2;
    std::array<TargetRegisterInfo, 32> gp_registers_{};
    std::array<TargetRegisterInfo, 32> vector_registers_{};
    std::array<InstructionDesc, 32> instruction_table_{};
    CallingConventionDesc calling_convention_{};
    uint8_t gp_register_count_ = 0;
    uint8_t vector_register_count_ = 0;
    uint8_t instruction_count_ = 0;
    uint16_t supported_scalar_widths_ = kWidth64;
    uint16_t supported_vector_widths_ = 0;
    bool supports_fma_ = false;
    bool scalable_vectors_ = false;

    [[nodiscard]] static constexpr uint16_t width_to_mask(uint32_t bits) noexcept {
        switch (bits) {
            case 32: return kWidth32;
            case 64: return kWidth64;
            case 128: return kWidth128;
            case 256: return kWidth256;
            case 512: return kWidth512;
            default: return 0;
        }
    }

    void add_gp(uint16_t id, std::string_view name, bool caller_saved) noexcept {
        if (gp_register_count_ >= gp_registers_.size()) return;
        gp_registers_[gp_register_count_++] = TargetRegisterInfo{
            id, name, RegisterBank::GeneralPurpose, 64, caller_saved, true};
    }

    void add_vec(uint16_t id, std::string_view name, uint16_t bit_width, bool caller_saved) noexcept {
        if (vector_register_count_ >= vector_registers_.size()) return;
        vector_registers_[vector_register_count_++] = TargetRegisterInfo{
            id, name, RegisterBank::Vector, bit_width, caller_saved, true};
    }

    void add_instruction(OpCode opcode, uint8_t latency, uint8_t throughput,
                         uint16_t widths, bool fusion) noexcept {
        if (instruction_count_ >= instruction_table_.size()) return;
        instruction_table_[instruction_count_++] = InstructionDesc{
            opcode, latency, throughput, widths, fusion};
    }

    void setup_registers() noexcept;
    void setup_calling_convention(CallingConventionKind calling_convention) noexcept;
    void setup_instruction_table() noexcept;
};

class alignas(64) TargetMachine {
public:
    /**
     * @brief Target capabilities used by legalization, ISel and scheduling.
     *
     * The generic fields use the same semantic vocabulary as the generated
     * ISel target contract. Architecture-specific capability fields are retained as explicit target capability state.
     */
    struct Capabilities {
        bool scalar32 = false;
        bool scalar64 = false;
        bool vector = false;
        bool vector_memory = false;
        bool atomics = false;
        bool concurrency = false;
        bool syscall = false;
        bool tensor = false;
        bool wasm_simd = false;
        bool object_macho = false;

        bool has_avx2 = false;
        bool has_avx512 = false;
        bool has_amx = false;
        bool has_fma = false;
        bool has_bmi2 = false;
        bool has_sve = false;
        bool has_neon = false;
        bool has_vfp = false;
        bool has_rvv = false;
        bool has_cuda = false;
        bool has_rocm = false;

        uint32_t cache_line_size = 64;
        uint64_t preferred_affinity_mask = 0x1ull;
        uint32_t preferred_core_index = 0;

        [[nodiscard]] constexpr TargetFeatureSet feature_set() const noexcept {
            TargetFeatureSet result;
            if (scalar32)       result.add(TargetFeature::Scalar32);
            if (scalar64)       result.add(TargetFeature::Scalar64);
            if (vector)         result.add(TargetFeature::Vector);
            if (vector_memory)  result.add(TargetFeature::VectorMemory);
            if (has_fma)        result.add(TargetFeature::FMA);
            if (atomics)        result.add(TargetFeature::Atomics);
            if (concurrency)    result.add(TargetFeature::Concurrency);
            if (syscall)        result.add(TargetFeature::Syscall);
            if (has_avx2)       result.add(TargetFeature::AVX2);
            if (has_avx512)     result.add(TargetFeature::AVX512);
            if (has_bmi2)       result.add(TargetFeature::BMI2);
            if (has_sve)        result.add(TargetFeature::SVE);
            if (has_neon)       result.add(TargetFeature::NEON);
            if (has_vfp)        result.add(TargetFeature::VFP);
            if (has_rvv)        result.add(TargetFeature::RVV);
            if (has_cuda)       result.add(TargetFeature::CUDA);
            if (has_rocm)       result.add(TargetFeature::ROCm);
            if (tensor)         result.add(TargetFeature::Tensor);
            if (wasm_simd)      result.add(TargetFeature::WasmSIMD);
            if (object_macho)   result.add(TargetFeature::ObjectMachO);
            if (has_amx)        result.add(TargetFeature::AMX);
            return result;
        }

        [[nodiscard]] constexpr bool has(TargetFeature feature) const noexcept {
            return feature_set().has(feature);
        }
    };

    TargetMachine()
        : TargetMachine(ArchType::X86_64_AVX2, CallingConventionKind::SystemV_x64) {}

    explicit TargetMachine(
        ArchType arch,
        CallingConventionKind calling_convention = CallingConventionKind::SystemV_x64)
        : TargetMachine(arch, ObjectFormat::None, calling_convention) {}

    explicit TargetMachine(
        ArchType arch,
        ObjectFormat object_format,
        CallingConventionKind calling_convention = CallingConventionKind::SystemV_x64)
        : m_arch(arch),
          object_format_(object_format),
          requested_calling_convention_(calling_convention),
          identity_(identity_for_arch(arch, object_format)) {
        initialize_target();
        finalize_capabilities();
        host_features_ = detect_hardware();
        identity_.features = m_caps.feature_set();
    }

    [[nodiscard]] ArchType get_arch() const noexcept { return m_arch; }

    [[nodiscard]] static std::optional<ArchType> arch_from_target_name(
        std::string_view name) noexcept;

    [[nodiscard]] static std::optional<ObjectFormat> object_format_from_target_name(
        std::string_view name) noexcept;
    
    [[nodiscard]] bool supports_native(OpCode op) const noexcept {
        // The target description is the single source of truth for direct
        // opcode legality. Feature-specific checks are represented there too,
        // so Legalizer and ISel do not observe contradictory target contracts.
        return description_.supports_opcode(op);
    }

    [[nodiscard]] bool host_supports_target() const noexcept {
        switch (m_arch) {
            case ArchType::X86_64_AVX512:
                return host_features_.has_avx512;
            case ArchType::X86_64_AVX2:
                return host_features_.has_avx2;
            case ArchType::AARCH64_SVE:
            case ArchType::ARM32_NEON:
            case ArchType::RISCV64_RVV:
                // Host ISA probing for non-x86 targets is platform-specific and
                // must not be inferred from x86 feature detection.
                return false;
            case ArchType::WASM_SIMD:
            case ArchType::CUDA_PTX:
            case ArchType::ROCM_AMDGCN:
            case ArchType::FPGA_VERILOG:
                return false;
        }
        return false;
    }

    [[nodiscard]] uint32_t get_gp_register_count() const noexcept {
        return static_cast<uint32_t>(description_.general_purpose_registers().size());
    }

    [[nodiscard]] const Capabilities& get_caps() const noexcept { return m_caps; }

    // Host capabilities are intentionally separate from the compilation target.
    // This is essential for cross-compilation and for JIT policy decisions that
    // depend on the machine executing DGTL rather than the code being emitted.
    [[nodiscard]] const HardwareFeatures& host_features() const noexcept {
        return host_features_;
    }

    [[nodiscard]] const TargetDescription& description() const noexcept { return description_; }

    [[nodiscard]] const TargetIdentity& identity() const noexcept { return identity_; }
    [[nodiscard]] TargetFamily family() const noexcept { return identity_.family; }
    [[nodiscard]] ObjectFormat object_format() const noexcept { return object_format_; }
    [[nodiscard]] bool emits_macho() const noexcept {
        return object_format_ == ObjectFormat::MachO;
    }
    [[nodiscard]] std::string_view canonical_target_name() const noexcept {
        return identity_.canonical_name;
    }
    [[nodiscard]] std::string_view cpu() const noexcept { return identity_.cpu; }
    [[nodiscard]] std::string_view subtarget() const noexcept { return identity_.subtarget; }

    [[nodiscard]] TargetFeatureSet target_features() const noexcept {
        return m_caps.feature_set();
    }
    [[nodiscard]] bool has_feature(TargetFeature feature) const noexcept {
        return m_caps.has(feature);
    }
    [[nodiscard]] bool supports_scalar_width(uint32_t bits) const noexcept {
        return description_.supports_scalar_width(bits);
    }
    [[nodiscard]] bool supports_vector_width(uint32_t bits) const noexcept {
        return description_.supports_vector_width(bits);
    }
    [[nodiscard]] bool supports_fma() const noexcept {
        return m_caps.has_fma || description_.supports_fma();
    }
    [[nodiscard]] bool supports_vector_memory() const noexcept {
        return m_caps.vector_memory;
    }

    [[nodiscard]] static size_t get_preferred_alignment(OpCode op) noexcept {
        if (op == OpCode::STORE_V512 || op == OpCode::LOAD) return 64;
        return 8;
    }

    [[nodiscard]] bool prefer_branchless() const noexcept {
        return m_caps.has_avx512 || m_caps.has_avx2 || m_arch == ArchType::AARCH64_SVE;
    }

    [[nodiscard]] bool prefers_vector_heavy_schedule() const noexcept {
        return m_caps.has_avx512 || m_caps.has_avx2 || m_arch == ArchType::AARCH64_SVE;
    }

    [[nodiscard]] float memory_latency_bias() const noexcept {
        if (m_arch == ArchType::AARCH64_SVE) return 1.05f;
        if (m_arch == ArchType::WASM_SIMD) return 1.12f;
        return 1.0f;
    }

    [[nodiscard]] uint8_t instruction_latency(OpCode op) const noexcept {
        return description_.instruction_latency(op);
    }
    [[nodiscard]] uint8_t instruction_reciprocal_throughput(OpCode op) const noexcept {
        return description_.instruction_reciprocal_throughput(op);
    }
    [[nodiscard]] float average_instruction_latency() const noexcept {
        return description_.average_instruction_latency();
    }
    [[nodiscard]] float average_instruction_reciprocal_throughput() const noexcept {
        return description_.average_instruction_reciprocal_throughput();
    }

    [[nodiscard]] const CallingConventionDesc& default_calling_convention() const noexcept {
        return description_.default_calling_convention();
    }

    [[nodiscard]] CallingConventionKind requested_calling_convention() const noexcept {
        return requested_calling_convention_;
    }

    [[nodiscard]] CallingConventionKind calling_convention() const noexcept {
        return requested_calling_convention_;
    }



private:
    ArchType m_arch = ArchType::X86_64_AVX2;
    ObjectFormat object_format_ = ObjectFormat::None;
    Capabilities m_caps{};
    HardwareFeatures host_features_{};
    CallingConventionKind requested_calling_convention_ = CallingConventionKind::SystemV_x64;
    TargetDescription description_{};
    TargetIdentity identity_{};

    [[nodiscard]] static constexpr TargetIdentity identity_for_arch(
        ArchType arch,
        ObjectFormat object_format = ObjectFormat::None) noexcept {
        TargetIdentity identity{};
        identity.arch = arch;
        identity.object_format = object_format;

        switch (arch) {
            case ArchType::X86_64_AVX512:
                identity.kind = TargetKind::CPU;
                identity.family = TargetFamily::X64;
                identity.canonical_name = "x64";
                identity.cpu = "x86-64";
                identity.subtarget = "avx512";
                break;
            case ArchType::X86_64_AVX2:
                identity.kind = TargetKind::CPU;
                identity.family = TargetFamily::X64;
                identity.canonical_name = "x64";
                identity.cpu = "x86-64";
                identity.subtarget = "avx2";
                break;
            case ArchType::AARCH64_SVE:
                identity.kind = TargetKind::CPU;
                identity.family = TargetFamily::ARM64;
                identity.canonical_name = "arm64";
                identity.cpu = "aarch64";
                identity.subtarget = "sve";
                break;
            case ArchType::ARM32_NEON:
                identity.kind = TargetKind::CPU;
                identity.family = TargetFamily::ARM32;
                identity.canonical_name = "arm32";
                identity.cpu = "armv7a";
                identity.subtarget = "neon-vfp";
                break;
            case ArchType::RISCV64_RVV:
                identity.kind = TargetKind::CPU;
                identity.family = TargetFamily::RISCV64;
                identity.canonical_name = "riscv64";
                identity.cpu = "riscv64";
                identity.subtarget = "rv64gc";
                break;
            case ArchType::WASM_SIMD:
                identity.kind = TargetKind::VirtualMachine;
                identity.family = TargetFamily::WASM;
                identity.object_format = ObjectFormat::Wasm;
                identity.canonical_name = "wasm";
                identity.cpu = "wasm";
                identity.subtarget = "simd";
                break;
            case ArchType::CUDA_PTX:
                identity.kind = TargetKind::GPU;
                identity.family = TargetFamily::CUDA;
                identity.canonical_name = "cuda";
                identity.cpu = "nvptx";
                identity.subtarget = "ptx";
                break;
            case ArchType::ROCM_AMDGCN:
                identity.kind = TargetKind::GPU;
                identity.family = TargetFamily::ROCm;
                identity.canonical_name = "rocm";
                identity.cpu = "amdgcn";
                identity.subtarget = "amdgcn-gfx";
                break;
            case ArchType::FPGA_VERILOG:
                identity.kind = TargetKind::FPGA;
                identity.family = TargetFamily::FPGA;
                identity.canonical_name = "fpga";
                identity.cpu = "verilog";
                identity.subtarget = "generic";
                break;
        }
        return identity;
    }

    void initialize_target();

    void finalize_capabilities() noexcept {
        // Capabilities describe the requested compilation target. Host probing
        // never upgrades or downgrades this contract.
        m_caps = Capabilities{};
        m_caps.scalar32 = true;
        m_caps.scalar64 = true;

        switch (m_arch) {
            case ArchType::X86_64_AVX512:
                m_caps.vector = true;
                m_caps.vector_memory = true;
                m_caps.atomics = true;
                m_caps.concurrency = true;
                m_caps.syscall = true;
                m_caps.has_avx2 = true;
                m_caps.has_avx512 = true;
                m_caps.has_fma = true;
                m_caps.has_bmi2 = true;
                break;

            case ArchType::X86_64_AVX2:
                m_caps.vector = true;
                m_caps.vector_memory = true;
                m_caps.atomics = true;
                m_caps.concurrency = true;
                m_caps.syscall = true;
                m_caps.has_avx2 = true;
                m_caps.has_fma = true;
                m_caps.has_bmi2 = true;
                break;

            case ArchType::AARCH64_SVE:
                m_caps.vector = true;
                m_caps.vector_memory = true;
                m_caps.atomics = true;
                m_caps.concurrency = true;
                m_caps.syscall = true;
                m_caps.has_fma = true;
                m_caps.has_sve = true;
                break;

            case ArchType::ARM32_NEON:
                m_caps.vector = true;
                m_caps.vector_memory = true;
                m_caps.atomics = true;
                m_caps.concurrency = true;
                m_caps.syscall = true;
                m_caps.has_fma = true;
                m_caps.has_neon = true;
                m_caps.has_vfp = true;
                break;

            case ArchType::RISCV64_RVV:
                m_caps.vector = true;
                m_caps.vector_memory = true;
                m_caps.atomics = true;
                m_caps.concurrency = true;
                m_caps.syscall = true;
                m_caps.has_fma = true;
                m_caps.has_rvv = true;
                break;

            case ArchType::WASM_SIMD:
                m_caps.vector = true;
                m_caps.vector_memory = true;
                m_caps.atomics = true;
                m_caps.concurrency = true;
                m_caps.wasm_simd = true;
                break;

            case ArchType::CUDA_PTX:
                m_caps.vector = true;
                m_caps.vector_memory = true;
                m_caps.atomics = true;
                m_caps.concurrency = true;
                m_caps.has_fma = true;
                m_caps.has_cuda = true;
                m_caps.tensor = true;
                break;

            case ArchType::ROCM_AMDGCN:
                m_caps.vector = true;
                m_caps.vector_memory = true;
                m_caps.atomics = true;
                m_caps.concurrency = true;
                m_caps.has_fma = true;
                m_caps.has_rocm = true;
                m_caps.tensor = true;
                break;

            case ArchType::FPGA_VERILOG:
                break;
        }

        if (object_format_ == ObjectFormat::MachO) {
            m_caps.object_macho = true;
        }

        m_caps.cache_line_size = 64;
        m_caps.preferred_affinity_mask = default_high_performance_affinity().mask;
        m_caps.preferred_core_index = default_high_performance_affinity().preferred_core;
    }
};

// ============================================================
// Zero-Cost Policy Tags & Pipeline (Merged Logic)
// ============================================================

struct ScalarKernelPolicy {};
struct AVX2KernelPolicy   {};
struct AVX512KernelPolicy {};

template <SimdTier Tier> struct SimdPolicy;
template <> struct SimdPolicy<SimdTier::Scalar> { using type = ScalarKernelPolicy; };
template <> struct SimdPolicy<SimdTier::AVX2>   { using type = AVX2KernelPolicy;   };
template <> struct SimdPolicy<SimdTier::AVX512> { using type = AVX512KernelPolicy; };

struct TargetPipeline {
    bool enable_gvn               = true;
    bool enable_dce               = true;
    bool enable_loop_unroll       = true;
    bool run_dce_pre_inlining     = true;
    bool run_dce_post_inlining    = true;
    bool run_dce_post_specialization = true;
    bool run_dce_late_cleanup     = true;
    bool deterministic_mode       = true;
    bool safety_hardening_mode     = true;
    bool minimize_binary_size      = false;
    uint32_t optimization_level   = 3; 
};

} // namespace dgtl::core
