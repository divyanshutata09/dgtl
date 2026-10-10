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

class TargetRegistry {
public:
    static std::unique_ptr<TargetBackend> lookup(dgtl::core::TargetFamily family, bool is_windows = true) {
        if (family == dgtl::core::TargetFamily::X64) {
            return std::make_unique<X64Backend>(is_windows);
        }
        // Default to x64 for host execution
        return std::make_unique<X64Backend>(is_windows);
    }
};

} // namespace dgtl::codegen
