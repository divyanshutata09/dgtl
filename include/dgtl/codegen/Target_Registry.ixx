/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Target Registry (Driver Layer Target Hardware Abstraction)
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

export module dgtl.codegen.target_registry;

import dgtl.target;
import dgtl.mir.machine_ir;
import dgtl.object.file;
import dgtl.isa.x64.target;
import dgtl.isa.x64.mc_encoder;
import dgtl.isa.arm64.target;
import dgtl.isa.arm64.mc_encoder;
import dgtl.isa.riscv64.target;
import dgtl.isa.riscv64.mc_encoder;
import dgtl.isa.wasm.target;
import dgtl.isa.wasm.encoder;
import dgtl.isa.ptx.target;
import dgtl.isa.ptx.emitter;
import dgtl.isa.amdgpu.target;
import dgtl.isa.amdgpu.emitter;
import dgtl.isa.spirv.target;
import dgtl.isa.spirv.encoder;
import dgtl.object.section;
import dgtl.object.symbol;

export namespace dgtl::codegen {

struct TargetBackend {
    virtual ~TargetBackend() = default;
    [[nodiscard]] virtual const dgtl::machine::TargetRegisterInfo& register_info() const noexcept = 0;
    [[nodiscard]] virtual const dgtl::machine::TargetInstructionInfo& instruction_info() const noexcept = 0;
    [[nodiscard]] virtual const dgtl::machine::TargetFrameInfo& frame_info() const noexcept = 0;
    [[nodiscard]] virtual const dgtl::machine::TargetABI& abi() const noexcept = 0;
    virtual bool encode_function(
        const dgtl::machine::MachineFunction& function,
        dgtl::object::ObjectFile& object,
        std::string_view symbol_name,
        const std::unordered_map<std::uint32_t, std::string>& symbol_table,
        std::string* out_diag = nullptr) const = 0;
};

class X64Backend final : public TargetBackend {
public:
    explicit X64Backend(bool is_windows = true) : is_windows_(is_windows) {}

    [[nodiscard]] const dgtl::machine::TargetRegisterInfo& register_info() const noexcept override {
        return subtarget_.reg_info;
    }
    [[nodiscard]] const dgtl::machine::TargetInstructionInfo& instruction_info() const noexcept override {
        return subtarget_.instr_info;
    }
    [[nodiscard]] const dgtl::machine::TargetFrameInfo& frame_info() const noexcept override {
        return subtarget_.frame_info;
    }
    [[nodiscard]] const dgtl::machine::TargetABI& abi() const noexcept override {
        return subtarget_.abi(is_windows_);
    }
    bool encode_function(
        const dgtl::machine::MachineFunction& function,
        dgtl::object::ObjectFile& object,
        std::string_view symbol_name,
        const std::unordered_map<std::uint32_t, std::string>& symbol_table,
        std::string* out_diag = nullptr) const override {
        return dgtl::isa::x64::X64MCEncoder::encode_function(function, object, symbol_name, symbol_table, out_diag);
    }

private:
    bool is_windows_ = true;
    dgtl::isa::x64::X64TargetSubtarget subtarget_{};
};

class ARM64Backend final : public TargetBackend {
public:
    [[nodiscard]] const dgtl::machine::TargetRegisterInfo& register_info() const noexcept override {
        return subtarget_.reg_info;
    }
    [[nodiscard]] const dgtl::machine::TargetInstructionInfo& instruction_info() const noexcept override {
        return subtarget_.instr_info;
    }
    [[nodiscard]] const dgtl::machine::TargetFrameInfo& frame_info() const noexcept override {
        return subtarget_.frame_info;
    }
    [[nodiscard]] const dgtl::machine::TargetABI& abi() const noexcept override {
        return subtarget_.abi();
    }
    bool encode_function(
        const dgtl::machine::MachineFunction& function,
        dgtl::object::ObjectFile& object,
        std::string_view symbol_name,
        const std::unordered_map<std::uint32_t, std::string>& symbol_table,
        std::string* out_diag = nullptr) const override {
        return dgtl::isa::arm64::ARM64MCEncoder::encode_function(function, object, symbol_name, symbol_table, out_diag);
    }

private:
    dgtl::isa::arm64::ARM64TargetSubtarget subtarget_{};
};

class RiscV64Backend final : public TargetBackend {
public:
    [[nodiscard]] const dgtl::machine::TargetRegisterInfo& register_info() const noexcept override {
        return subtarget_.reg_info;
    }
    [[nodiscard]] const dgtl::machine::TargetInstructionInfo& instruction_info() const noexcept override {
        return subtarget_.instr_info;
    }
    [[nodiscard]] const dgtl::machine::TargetFrameInfo& frame_info() const noexcept override {
        return subtarget_.frame_info;
    }
    [[nodiscard]] const dgtl::machine::TargetABI& abi() const noexcept override {
        return subtarget_.abi();
    }
    bool encode_function(
        const dgtl::machine::MachineFunction& function,
        dgtl::object::ObjectFile& object,
        std::string_view symbol_name,
        const std::unordered_map<std::uint32_t, std::string>& symbol_table,
        std::string* out_diag = nullptr) const override {
        return dgtl::isa::riscv64::RiscV64MCEncoder::encode_function(function, object, symbol_name, symbol_table, out_diag);
    }

private:
    dgtl::isa::riscv64::RiscV64TargetSubtarget subtarget_{};
};

class WasmBackend final : public TargetBackend {
public:
    [[nodiscard]] const dgtl::machine::TargetRegisterInfo& register_info() const noexcept override {
        return subtarget_.reg_info;
    }
    [[nodiscard]] const dgtl::machine::TargetInstructionInfo& instruction_info() const noexcept override {
        return subtarget_.instr_info;
    }
    [[nodiscard]] const dgtl::machine::TargetFrameInfo& frame_info() const noexcept override {
        return subtarget_.frame_info;
    }
    [[nodiscard]] const dgtl::machine::TargetABI& abi() const noexcept override {
        return subtarget_.abi();
    }
    bool encode_function(
        const dgtl::machine::MachineFunction& function,
        dgtl::object::ObjectFile& object,
        std::string_view symbol_name,
        const std::unordered_map<std::uint32_t, std::string>& symbol_table,
        std::string* out_diag = nullptr) const override {
        (void)symbol_table;
        dgtl::isa::wasm::DGTLWasmEncoder encoder;
        encoder.encode(function);
        const auto& bytes = encoder.get_binary();
        if (bytes.empty()) {
            if (out_diag) *out_diag = "WASM bytecode emission returned 0 bytes";
            return false;
        }
        auto& text_sec = object.get_text_section();
        const auto offset = static_cast<std::uint64_t>(text_sec.bytes.size());
        text_sec.bytes.insert(text_sec.bytes.end(), bytes.begin(), bytes.end());
        object.add_function_symbol(
            std::string(symbol_name), offset, static_cast<std::uint64_t>(bytes.size()), true);
        return true;
    }

private:
    dgtl::isa::wasm::WasmTargetSubtarget subtarget_{};
};

class CUDABackend final : public TargetBackend {
public:
    [[nodiscard]] const dgtl::machine::TargetRegisterInfo& register_info() const noexcept override {
        return subtarget_.reg_info;
    }
    [[nodiscard]] const dgtl::machine::TargetInstructionInfo& instruction_info() const noexcept override {
        return subtarget_.instr_info;
    }
    [[nodiscard]] const dgtl::machine::TargetFrameInfo& frame_info() const noexcept override {
        return subtarget_.frame_info;
    }
    [[nodiscard]] const dgtl::machine::TargetABI& abi() const noexcept override {
        return subtarget_.abi();
    }
    bool encode_function(
        const dgtl::machine::MachineFunction& function,
        dgtl::object::ObjectFile& object,
        std::string_view symbol_name,
        const std::unordered_map<std::uint32_t, std::string>& symbol_table,
        std::string* out_diag = nullptr) const override {
        (void)symbol_table;
        dgtl::isa::ptx::CUDAEmitter emitter;
        auto result = emitter.emit(function);
        if (!result.success || result.bytes.empty()) {
            if (out_diag) *out_diag = "CUDA PTX emission failed or produced 0 bytes";
            return false;
        }
        auto& text_sec = object.get_text_section();
        const auto offset = static_cast<std::uint64_t>(text_sec.bytes.size());
        text_sec.bytes.insert(text_sec.bytes.end(), result.bytes.begin(), result.bytes.end());
        object.add_function_symbol(
            std::string(symbol_name), offset, static_cast<std::uint64_t>(result.bytes.size()), true);
        return true;
    }

private:
    dgtl::isa::ptx::PTXTargetSubtarget subtarget_{};
};

class ROCmBackend final : public TargetBackend {
public:
    [[nodiscard]] const dgtl::machine::TargetRegisterInfo& register_info() const noexcept override {
        return subtarget_.reg_info;
    }
    [[nodiscard]] const dgtl::machine::TargetInstructionInfo& instruction_info() const noexcept override {
        return subtarget_.instr_info;
    }
    [[nodiscard]] const dgtl::machine::TargetFrameInfo& frame_info() const noexcept override {
        return subtarget_.frame_info;
    }
    [[nodiscard]] const dgtl::machine::TargetABI& abi() const noexcept override {
        return subtarget_.abi();
    }
    bool encode_function(
        const dgtl::machine::MachineFunction& function,
        dgtl::object::ObjectFile& object,
        std::string_view symbol_name,
        const std::unordered_map<std::uint32_t, std::string>& symbol_table,
        std::string* out_diag = nullptr) const override {
        (void)symbol_table;
        dgtl::isa::amdgpu::AMDGPUEmitter emitter;
        auto result = emitter.emit(function);
        if (!result.success || result.bytes.empty()) {
            if (out_diag) *out_diag = "AMDGPU emission failed or produced 0 bytes";
            return false;
        }
        auto& text_sec = object.get_text_section();
        const auto offset = static_cast<std::uint64_t>(text_sec.bytes.size());
        text_sec.bytes.insert(text_sec.bytes.end(), result.bytes.begin(), result.bytes.end());
        object.add_function_symbol(
            std::string(symbol_name), offset, static_cast<std::uint64_t>(result.bytes.size()), true);
        return true;
    }

private:
    dgtl::isa::amdgpu::AMDGPUTargetSubtarget subtarget_{};
};

class SPIRVBackend final : public TargetBackend {
public:
    [[nodiscard]] const dgtl::machine::TargetRegisterInfo& register_info() const noexcept override {
        return subtarget_.reg_info;
    }
    [[nodiscard]] const dgtl::machine::TargetInstructionInfo& instruction_info() const noexcept override {
        return subtarget_.instr_info;
    }
    [[nodiscard]] const dgtl::machine::TargetFrameInfo& frame_info() const noexcept override {
        return subtarget_.frame_info;
    }
    [[nodiscard]] const dgtl::machine::TargetABI& abi() const noexcept override {
        return subtarget_.abi();
    }
    bool encode_function(
        const dgtl::machine::MachineFunction& function,
        dgtl::object::ObjectFile& object,
        std::string_view symbol_name,
        const std::unordered_map<std::uint32_t, std::string>& symbol_table,
        std::string* out_diag = nullptr) const override {
        (void)symbol_table;
        const auto words = dgtl::isa::spirv::emitter::SPIRVCodeEmitter::encode_function_module(function);
        if (words.empty()) {
            if (out_diag) *out_diag = "SPIR-V module encoding produced 0 words";
            return false;
        }
        const auto byte_count = words.size() * sizeof(std::uint32_t);
        auto& text_sec = object.get_text_section();
        const auto offset = static_cast<std::uint64_t>(text_sec.bytes.size());
        const auto* raw_bytes = reinterpret_cast<const std::uint8_t*>(words.data());
        text_sec.bytes.insert(text_sec.bytes.end(), raw_bytes, raw_bytes + byte_count);
        object.add_function_symbol(
            std::string(symbol_name), offset, static_cast<std::uint64_t>(byte_count), true);
        return true;
    }

private:
    dgtl::isa::spirv::SPIRVTargetSubtarget subtarget_{};
};

class TargetRegistry {
public:
    static std::unique_ptr<TargetBackend> lookup(dgtl::core::TargetFamily family, bool is_windows = true) {
        switch (family) {
            case dgtl::core::TargetFamily::X64:
                return std::make_unique<X64Backend>(is_windows);
            case dgtl::core::TargetFamily::ARM64:
                return std::make_unique<ARM64Backend>();
            case dgtl::core::TargetFamily::RISCV64:
                return std::make_unique<RiscV64Backend>();
            case dgtl::core::TargetFamily::WASM:
                return std::make_unique<WasmBackend>();
            case dgtl::core::TargetFamily::CUDA:
                return std::make_unique<CUDABackend>();
            case dgtl::core::TargetFamily::ROCm:
                return std::make_unique<ROCmBackend>();
            case dgtl::core::TargetFamily::SPIRV:
                return std::make_unique<SPIRVBackend>();
            default:
                return std::make_unique<X64Backend>(is_windows);
        }
    }
};

} // namespace dgtl::codegen
