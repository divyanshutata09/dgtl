/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Unified Codegen Driver (IR -> MIR -> ISA -> ObjectFile)
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
#include <vector>

export module dgtl.codegen.driver;

import dgtl.target;
import dgtl.ir.stream;
import dgtl.mir.machine_ir;
import dgtl.mir.module;
import dgtl.mir.pipeline;
import dgtl.object.file;
import dgtl.codegen.target_registry;

export namespace dgtl::codegen {

struct CodegenResult {
    bool success = false;
    std::string error_message;
    dgtl::object::ObjectFile object;
};

class CodegenDriver {
public:
    static CodegenResult compile(
        const dgtl::ir::IRStream& stream,
        const dgtl::core::TargetMachine& target,
        const std::unordered_map<std::uint32_t, std::string>& symbol_names = {},
        std::string_view entry_name = "main",
        const dgtl::mir::MachinePipelineOptions& options = {}) {

        CodegenResult result{};
        if (stream.is_null() || stream.size() == 0) {
            result.error_message = "IR stream is empty";
            return result;
        }

#if defined(_WIN32) || defined(_WIN64)
        const bool is_windows = true;
#else
        const bool is_windows = false;
#endif

        auto backend = TargetRegistry::lookup(target.family(), is_windows);
        if (!backend) {
            result.error_message = "No backend found for target family";
            return result;
        }

        // 1. Build MIR MachineModule and Function
        dgtl::machine::MachineModule module("dgtl_module");
        module.set_entry_point(entry_name);
        dgtl::machine::MachineFunction& fn = module.add_function(entry_name);

        for (const auto& [slot, name] : symbol_names) {
            (void)module.get_or_create_symbol(name);
        }

        // 2. Run MIR Compilation Pipeline
        auto mir_res = dgtl::mir::MachinePipeline::compile(
            fn,
            stream,
            target,
            backend->register_info(),
            &backend->instruction_info(),
            &backend->frame_info(),
            &backend->abi(),
            dgtl::mir::make_builtin_instruction_selector_program(),
            options
        );

        if (!mir_res.success) {
            result.error_message = "MIR pipeline failure: " + mir_res.error_message;
            return result;
        }

        // 3. Target MC Encoding into ObjectFile
        dgtl::object::ObjectFile& obj = result.object;
        obj.set_format(is_windows ? dgtl::object::ObjectFormat::COFF : dgtl::object::ObjectFormat::ELF);

        std::string diag;
        const bool enc_ok = backend->encode_function(fn, obj, entry_name, symbol_names, &diag);
        if (!enc_ok) {
            result.error_message = "Target MC encoder failed to encode function " + std::string(entry_name) +
                (diag.empty() ? "" : ": " + diag);
            return result;
        }

        result.success = true;
        return result;
    }

    static CodegenResult compile(
        const dgtl::machine::MachineFunction& function,
        const dgtl::core::TargetMachine& target,
        const std::unordered_map<std::uint32_t, std::string>& symbol_names = {},
        std::string_view entry_name = "main") {

        CodegenResult result{};
        if (function.instructions().empty()) {
            result.error_message = "MachineFunction has no instructions";
            return result;
        }

#if defined(_WIN32) || defined(_WIN64)
        const bool is_windows = true;
#else
        const bool is_windows = false;
#endif

        auto backend = TargetRegistry::lookup(target.family(), is_windows);
        if (!backend) {
            result.error_message = "No backend found for target family";
            return result;
        }

        dgtl::object::ObjectFile& obj = result.object;
        obj.set_format(is_windows ? dgtl::object::ObjectFormat::COFF : dgtl::object::ObjectFormat::ELF);

        std::string diag;
        const bool enc_ok = backend->encode_function(function, obj, entry_name, symbol_names, &diag);
        if (!enc_ok) {
            result.error_message = "Target MC encoder failed to encode function " + std::string(entry_name) +
                (diag.empty() ? "" : ": " + diag);
            return result;
        }

        result.success = true;
        return result;
    }
};

} // namespace dgtl::codegen
