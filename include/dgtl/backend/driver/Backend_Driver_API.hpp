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

#pragma once

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "dgtl/backend/lowering/ABI_Lowering.hpp"
#include "dgtl/backend/linker/Linker_Backend.hpp"
#include "dgtl/core/ABI.hpp"
#include "dgtl/ir/base/IR_Stream.hpp"
#include "dgtl/semantic/Symbol_Table.hpp"
#include "dgtl/isa/emitter/ARM64_Emitter.hpp"
#include "dgtl/isa/emitter/NASM_Emitter.hpp"
#include "dgtl/isa/emitter/MachO_Emitter.hpp"
#include "dgtl/isa/lowering/X64_Machine_Form_Lowering.hpp"
#include "dgtl/isa/target/Wasm_Encoder.hpp"
#include "dgtl/backend/driver/Backend_Driver_Shared_Utils.hpp"

namespace dgtl::backend::driver {

using dgtl::backend::lowering::ABICallLayout;
using dgtl::backend::lowering::ABIConvention;
using dgtl::backend::lowering::ABIValueClass;

enum class BackendDriverTarget : unsigned char {
    arm64,
    x64,
    nasm,
    wasm,
    elfobj,
    staticlib,
    coffobj,
    cofflib,
    macho,
    arm32,
    riscv64,
    cuda,
    rocm,
};

struct BackendDriverRequest {
    std::string dgtl_executable_path;
    BackendDriverTarget target = BackendDriverTarget::arm64;
    std::string output_path;
    bool quiet = true;
    bool no_print = true;
    std::string ir_mode = "int";
};

struct BackendDriverResult {
    bool ok = false;
    int exit_code = -1;
    std::string backend_name;
    std::string output_path;
    std::string command_line;
    std::string message;
};

struct BackendDriverABIBridgeConfig {
    // Client may provide a callback to install external hooks (optional)
    void (*install_external_intrinsic_hooks)() = nullptr;
    // Client may provide a callback to register adapter signatures (optional)
    void (*register_adapter_signatures)(dgtl::core::DGTLABIRegistry&) = nullptr;
    dgtl::core::DGTLCallValidationOptions call_validation =
        dgtl::core::DGTLCallValidationOptions::lenient_runtime_intrinsics();
    ABIConvention convention = ABIConvention::SystemV_x64;
};

[[nodiscard]] inline ABIConvention backend_target_abi_convention(BackendDriverTarget target) noexcept {
    switch (target) {
    case BackendDriverTarget::arm64:
    case BackendDriverTarget::macho:
        return ABIConvention::ARM64_EABI;
    case BackendDriverTarget::wasm:
        return ABIConvention::WebAssembly;
    case BackendDriverTarget::coffobj:
    case BackendDriverTarget::cofflib:
        return ABIConvention::Microsoft_x64;
    case BackendDriverTarget::x64:
    case BackendDriverTarget::nasm:
#ifdef _WIN32
        return ABIConvention::Microsoft_x64;
#else
        return ABIConvention::SystemV_x64;
#endif
    case BackendDriverTarget::arm32:
    case BackendDriverTarget::riscv64:
    case BackendDriverTarget::cuda:
    case BackendDriverTarget::rocm:
    case BackendDriverTarget::elfobj:
    case BackendDriverTarget::staticlib:
    default:
        return ABIConvention::SystemV_x64;
    }
}

inline void install_default_external_intrinsic_hooks() noexcept {
    // Intentionally empty in DGTL core. Client adapters register hooks via
    // BackendDriverABIBridgeConfig callbacks.
}

inline void seed_default_runtime_abi_registry(
    dgtl::core::DGTLABIRegistry& abi,
    const BackendDriverABIBridgeConfig& cfg = BackendDriverABIBridgeConfig{}) {
    if (cfg.install_external_intrinsic_hooks) {
        cfg.install_external_intrinsic_hooks();
    }
    if (cfg.register_adapter_signatures) {
        cfg.register_adapter_signatures(abi);
    }
}

[[nodiscard]] inline ABIValueClass to_lowering_abi_value_class(
    dgtl::core::DGTLABIValueClass value_class) noexcept {
    switch (value_class) {
    case dgtl::core::DGTLABIValueClass::f32:
    case dgtl::core::DGTLABIValueClass::f64:
        return ABIValueClass::FloatingPoint;
    case dgtl::core::DGTLABIValueClass::aggregate:
        return ABIValueClass::Memory;
    case dgtl::core::DGTLABIValueClass::none:
    case dgtl::core::DGTLABIValueClass::i32:
    case dgtl::core::DGTLABIValueClass::i64:
    case dgtl::core::DGTLABIValueClass::pointer:
    case dgtl::core::DGTLABIValueClass::custom:
    default:
        return ABIValueClass::IntegerOrPointer;
    }
}

[[nodiscard]] inline uint32_t dgtl_abi_value_size_bytes(
    dgtl::core::DGTLABIValueClass value_class) noexcept {
    switch (value_class) {
    case dgtl::core::DGTLABIValueClass::i32:
    case dgtl::core::DGTLABIValueClass::f32:
        return 4u;
    case dgtl::core::DGTLABIValueClass::none:
        return 0u;
    case dgtl::core::DGTLABIValueClass::aggregate:
        return 16u;
    case dgtl::core::DGTLABIValueClass::i64:
    case dgtl::core::DGTLABIValueClass::f64:
    case dgtl::core::DGTLABIValueClass::pointer:
    case dgtl::core::DGTLABIValueClass::custom:
    default:
        return 8u;
    }
}

[[nodiscard]] inline ABICallLayout lower_call_signature_layout(
    const dgtl::core::DGTLCallSignature& signature,
    ABIConvention convention,
    const BackendDriverABIBridgeConfig& cfg = BackendDriverABIBridgeConfig{}) {
    dgtl::backend::lowering::ABI_Lowering lowering(convention);

    std::vector<ABIValueClass> classes;
    classes.reserve(signature.parameters.size());

    std::vector<uint32_t> sizes;
    sizes.reserve(signature.parameters.size());

    for (const auto& param : signature.parameters) {
        classes.push_back(to_lowering_abi_value_class(param.value_class));
        sizes.push_back(dgtl_abi_value_size_bytes(param.value_class));
    }

    const size_t fixed_param_count = signature.is_variadic
        ? signature.parameters.size()
        : signature.parameters.size();

    (void)cfg.call_validation;
    return lowering.plan_call_layout(
        classes,
        sizes,
        to_lowering_abi_value_class(signature.return_value.value_class),
        signature.is_variadic,
        fixed_param_count);
}

inline std::vector<BackendDriverTarget> all_backend_driver_targets() {
    return {
        BackendDriverTarget::arm64,
        BackendDriverTarget::x64,
        BackendDriverTarget::nasm,
        BackendDriverTarget::wasm,
        BackendDriverTarget::elfobj,
        BackendDriverTarget::staticlib,
        BackendDriverTarget::coffobj,
        BackendDriverTarget::cofflib,
        BackendDriverTarget::macho,
        BackendDriverTarget::arm32,
        BackendDriverTarget::riscv64,
        BackendDriverTarget::cuda,
        BackendDriverTarget::rocm,
    };
}

struct IRStreamEmitRequest {
    std::string module_name;
    std::string output_dir = "build/out";
    const dgtl::ir::IRStream* stream = nullptr;
    const dgtl::semantic::SymbolTable* symbols = nullptr;
    const std::unordered_map<uint32_t, std::string>* slot_to_callee = nullptr;
    bool emit_all = false;
    std::optional<BackendDriverTarget> target;
};

struct FrontendEmitResult {
    bool ok = false;
    std::vector<BackendDriverResult> per_target;
    std::string message;
};

namespace detail {

inline bool emit_arm64_direct(const dgtl::ir::IRStream& stream,
                              const dgtl::semantic::SymbolTable& symbols,
                              std::string& out_text) {
    dgtl::isa::ARM64Emitter emitter(stream, symbols);
    dgtl::isa::ARM64Emitter::Config cfg;
    cfg.verbose_asm = true;
    std::ostringstream oss;
    emitter.emit_assembly(oss, cfg);
    out_text = oss.str();
    return !out_text.empty();
}

inline bool emit_x64_direct(const dgtl::ir::IRStream& stream,
                            std::vector<uint8_t>& out_bytes,
                            std::string& out_text) {
    dgtl::isa::X64StrikeEmitter emitter;
    out_bytes.clear();
    if (!emitter.process_stream(stream, out_bytes) || out_bytes.empty()) {
        return false;
    }
    out_text = dgtl::backend::driver::shared::bytes_to_hex(out_bytes);
    return true;
}

inline bool emit_nasm_direct(const dgtl::ir::IRStream& stream,
                             const dgtl::semantic::SymbolTable& symbols,
                             std::string& out_text) {
    dgtl::isa::DGTLNasmEmitter emitter;
    emitter.emit(stream, symbols);
    out_text = emitter.get_output();
    return !out_text.empty();
}

inline bool emit_wasm_direct(const dgtl::ir::IRStream& stream,
                             std::vector<uint8_t>& out_bytes,
                             std::string& out_text) {
    dgtl::exec::DGTLWasmEncoder encoder;
    encoder.encode(stream);
    out_bytes = encoder.get_binary();
    if (out_bytes.empty()) return false;
    out_text = dgtl::backend::driver::shared::bytes_to_hex(out_bytes);
    return true;
}

inline bool emit_elfobj_direct(const dgtl::ir::IRStream& stream, std::vector<uint8_t>& out_bytes) {
    dgtl::exec::DGTLLinker linker;
    return linker.build_elf_relocatable_binary(stream, out_bytes, "DGTL_kernel_entry");
}

inline bool emit_staticlib_direct(const dgtl::ir::IRStream& stream, std::vector<uint8_t>& out_bytes) {
    dgtl::exec::DGTLLinker linker;
    return linker.build_static_archive_binary(stream, out_bytes, "module.o");
}

inline bool emit_coffobj_direct(const dgtl::ir::IRStream& stream, std::vector<uint8_t>& out_bytes) {
    dgtl::exec::DGTLLinker linker;
    return linker.build_coff_relocatable_binary(stream, out_bytes, "DGTL_kernel_entry");
}

inline bool emit_cofflib_direct(const dgtl::ir::IRStream& stream, std::vector<uint8_t>& out_bytes) {
    dgtl::exec::DGTLLinker linker;
    return linker.build_coff_archive_binary(stream, out_bytes, "module.obj", "DGTL_kernel_entry");
}

inline bool emit_macho_direct(const dgtl::ir::IRStream& stream,
                              const dgtl::semantic::SymbolTable& symbols,
                              std::vector<uint8_t>& out_bytes) {
    dgtl::isa::MachOEmitter emitter(stream, symbols);
    auto result = emitter.emit_to_binary();
    if (!result.success) return false;
    out_bytes = std::move(result.binary_data);
    return !out_bytes.empty();
}

} // namespace detail

inline const char* backend_driver_target_name(BackendDriverTarget target) {
    switch (target) {
    case BackendDriverTarget::arm64: return "arm64";
    case BackendDriverTarget::x64: return "x64";
    case BackendDriverTarget::nasm: return "nasm";
    case BackendDriverTarget::wasm: return "wasm";
    case BackendDriverTarget::elfobj: return "elfobj";
    case BackendDriverTarget::staticlib: return "staticlib";
    case BackendDriverTarget::coffobj: return "coffobj";
    case BackendDriverTarget::cofflib: return "cofflib";
    case BackendDriverTarget::macho: return "macho";
    case BackendDriverTarget::arm32: return "arm32";
    case BackendDriverTarget::riscv64: return "riscv64";
    case BackendDriverTarget::cuda: return "cuda";
    case BackendDriverTarget::rocm: return "rocm";
    default: return "arm64";
    }
}

inline const char* backend_driver_target_extension(BackendDriverTarget target) {
    switch (target) {
    case BackendDriverTarget::arm64: return ".s";
    case BackendDriverTarget::x64: return ".bin";
    case BackendDriverTarget::nasm: return ".asm";
    case BackendDriverTarget::wasm: return ".wasm";
    case BackendDriverTarget::elfobj: return ".o";
    case BackendDriverTarget::staticlib: return ".a";
    case BackendDriverTarget::coffobj: return ".obj";
    case BackendDriverTarget::cofflib: return ".lib";
    case BackendDriverTarget::macho: return ".macho";
    case BackendDriverTarget::arm32: return ".s";
    case BackendDriverTarget::riscv64: return ".s";
    case BackendDriverTarget::cuda: return ".cu";
    case BackendDriverTarget::rocm: return ".hip";
    default: return ".out";
    }
}

inline bool parse_backend_driver_target(std::string value, BackendDriverTarget& out) {
    for (char& ch : value) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }

    if (value == "arm64") {
        out = BackendDriverTarget::arm64;
        return true;
    }
    if (value == "x64") {
        out = BackendDriverTarget::x64;
        return true;
    }
    if (value == "nasm") {
        out = BackendDriverTarget::nasm;
        return true;
    }
    if (value == "wasm") {
        out = BackendDriverTarget::wasm;
        return true;
    }
    if (value == "elfobj" || value == "obj" || value == "o") {
        out = BackendDriverTarget::elfobj;
        return true;
    }
    if (value == "staticlib" || value == "archive" || value == "a" || value == "lib") {
        out = BackendDriverTarget::staticlib;
        return true;
    }
    if (value == "coffobj" || value == "coff") {
        out = BackendDriverTarget::coffobj;
        return true;
    }
    if (value == "cofflib" || value == "coffarchive") {
        out = BackendDriverTarget::cofflib;
        return true;
    }
    if (value == "macho" || value == "mach-o" || value == "macho64") {
        out = BackendDriverTarget::macho;
        return true;
    }
    if (value == "arm32") {
        out = BackendDriverTarget::arm32;
        return true;
    }
    if (value == "riscv64" || value == "riscv" || value == "rv64") {
        out = BackendDriverTarget::riscv64;
        return true;
    }
    if (value == "cuda" || value == "ptx" || value == "nvptx") {
        out = BackendDriverTarget::cuda;
        return true;
    }
    if (value == "rocm" || value == "amdgcn" || value == "hip") {
        out = BackendDriverTarget::rocm;
        return true;
    }
    return false;
}

inline std::string quote_cmd_arg(std::string_view value) {
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('"');
    for (char c : value) {
        if (c == '"') {
            out += "\\\"";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('"');
    return out;
}

inline BackendDriverResult run_backend_driver(const BackendDriverRequest& req) {
    BackendDriverResult result;
    result.backend_name = backend_driver_target_name(req.target);
    result.output_path = req.output_path;

    if (req.dgtl_executable_path.empty()) {
        result.message = "DGTL executable path is empty";
        return result;
    }

    const std::filesystem::path exe_path = std::filesystem::absolute(std::filesystem::path(req.dgtl_executable_path)).lexically_normal();
    if (!std::filesystem::exists(exe_path)) {
        result.message = "DGTL executable not found at " + exe_path.string();
        return result;
    }

    if (req.output_path.empty()) {
        result.message = "Output path is empty";
        return result;
    }

    const std::filesystem::path out_path = std::filesystem::absolute(std::filesystem::path(req.output_path)).lexically_normal();
    if (out_path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(out_path.parent_path(), ec);
        if (ec) {
            result.message = "Failed to create output directory for " + req.output_path;
            return result;
        }
    }

    std::ostringstream cmd;
#ifdef _WIN32
    auto quote_ps_single = [](std::string value) {
        std::string out;
        out.reserve(value.size() + 2);
        for (char c : value) {
            if (c == '\'') {
                out += "''";
            } else {
                out.push_back(c);
            }
        }
        return out;
    };

    cmd << "powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \"& '"
        << quote_ps_single(exe_path.string())
        << "' --backend " << backend_driver_target_name(req.target)
        << " --ir-mode " << req.ir_mode
        << " --emit-output '" << quote_ps_single(out_path.string()) << "'";

    if (req.no_print) {
        cmd << " --no-print";
    }
    if (req.quiet) {
        cmd << " --quiet";
    }
    cmd << "\"";
#else
    cmd << quote_cmd_arg(exe_path.string())
        << " --backend " << backend_driver_target_name(req.target)
        << " --ir-mode " << req.ir_mode
        << " --emit-output " << quote_cmd_arg(out_path.string());

    if (req.no_print) {
        cmd << " --no-print";
    }
    if (req.quiet) {
        cmd << " --quiet";
    }
#endif

    result.command_line = cmd.str();
    result.exit_code = std::system(result.command_line.c_str());
    result.ok = (result.exit_code == 0);

    if (!result.ok && result.message.empty()) {
        result.message = "DGTL backend driver failed with exit code " + std::to_string(result.exit_code);
    }

    return result;
}

inline bool validate_backend_driver_request(const BackendDriverRequest& req,
                                            std::string* error = nullptr) {
    if (req.dgtl_executable_path.empty()) {
        if (error) {
            *error = "DGTL executable path is empty";
        }
        return false;
    }

    const std::filesystem::path exe_path =
        std::filesystem::absolute(std::filesystem::path(req.dgtl_executable_path)).lexically_normal();
    if (!std::filesystem::exists(exe_path)) {
        if (error) {
            *error = "DGTL executable not found at " + exe_path.string();
        }
        return false;
    }

    if (req.output_path.empty()) {
        if (error) {
            *error = "Output path is empty";
        }
        return false;
    }

    return true;
}

inline BackendDriverRequest make_backend_driver_request(std::string_view dgtl_executable_path,
                                                        BackendDriverTarget target,
                                                        std::string_view output_path,
                                                        bool quiet = true,
                                                        bool no_print = true,
                                                        std::string_view ir_mode = "int") {
    BackendDriverRequest req;
    req.dgtl_executable_path = std::string(dgtl_executable_path);
    req.target = target;
    req.output_path = std::string(output_path);
    req.quiet = quiet;
    req.no_print = no_print;
    req.ir_mode = std::string(ir_mode);
    return req;
}

inline FrontendEmitResult emit_ir_stream(const IRStreamEmitRequest& req) {
    FrontendEmitResult result;

    if (req.stream == nullptr || req.stream->is_null()) {
        result.message = "IR stream is null";
        return result;
    }
    if (req.module_name.empty()) {
        result.message = "module name is empty";
        return result;
    }

    dgtl::core::DGTL_Arena local_arena(1, 64);
    dgtl::semantic::SymbolTable local_symbols(local_arena);
    const auto& symbols = req.symbols != nullptr ? *req.symbols : local_symbols;

    std::vector<BackendDriverTarget> targets;
    if (req.emit_all) {
        targets = all_backend_driver_targets();
    } else if (req.target.has_value()) {
        targets.push_back(*req.target);
    } else {
        result.message = "Neither emit_all nor a specific target was provided";
        return result;
    }

    bool all_ok = true;
    for (const auto target : targets) {
        BackendDriverResult per{};
        per.backend_name = backend_driver_target_name(target);
        const std::string filename = req.module_name +
            ((target == BackendDriverTarget::wasm || target == BackendDriverTarget::elfobj ||
              target == BackendDriverTarget::staticlib || target == BackendDriverTarget::coffobj ||
              target == BackendDriverTarget::cofflib || target == BackendDriverTarget::macho)
                 ? std::string(backend_driver_target_extension(target))
                 : std::string(".") + backend_driver_target_name(target) + backend_driver_target_extension(target));
        const std::filesystem::path out_path = std::filesystem::path(req.output_dir) / filename;
        std::error_code ec;
        std::filesystem::create_directories(out_path.parent_path(), ec);

        std::vector<uint8_t> bytes;
        std::string text;
        bool ok = false;
        switch (target) {
            case BackendDriverTarget::arm64: ok = detail::emit_arm64_direct(*req.stream, symbols, text) && shared::write_text_file(out_path.string(), text); break;
            case BackendDriverTarget::x64: ok = detail::emit_x64_direct(*req.stream, bytes, text) && shared::write_binary_file(out_path.string(), bytes); break;
            case BackendDriverTarget::nasm: ok = detail::emit_nasm_direct(*req.stream, symbols, text) && shared::write_text_file(out_path.string(), text); break;
            case BackendDriverTarget::wasm: ok = detail::emit_wasm_direct(*req.stream, bytes, text) && shared::write_binary_file(out_path.string(), bytes); break;
            case BackendDriverTarget::elfobj: ok = detail::emit_elfobj_direct(*req.stream, bytes) && shared::write_binary_file(out_path.string(), bytes); break;
            case BackendDriverTarget::staticlib: ok = detail::emit_staticlib_direct(*req.stream, bytes) && shared::write_binary_file(out_path.string(), bytes); break;
            case BackendDriverTarget::coffobj: {
                if (req.slot_to_callee != nullptr && !req.slot_to_callee->empty()) {
                    dgtl::exec::DGTLLinker linker;
                    const auto relocs = linker.collect_call_relocs(*req.stream, *req.slot_to_callee);
                    ok = linker.build_coff_relocatable_binary_with_relocs(*req.stream, bytes, "DGTL_kernel_entry", relocs) &&
                         shared::write_binary_file(out_path.string(), bytes);
                } else {
                    ok = detail::emit_coffobj_direct(*req.stream, bytes) && shared::write_binary_file(out_path.string(), bytes);
                }
                break;
            }
            case BackendDriverTarget::cofflib: ok = detail::emit_cofflib_direct(*req.stream, bytes) && shared::write_binary_file(out_path.string(), bytes); break;
            case BackendDriverTarget::macho: ok = detail::emit_macho_direct(*req.stream, symbols, bytes) && shared::write_binary_file(out_path.string(), bytes); break;
            case BackendDriverTarget::arm32: ok = shared::emit_arm32_text(text) && shared::write_text_file(out_path.string(), text); break;
            case BackendDriverTarget::riscv64: ok = shared::emit_riscv64_text(text) && shared::write_text_file(out_path.string(), text); break;
            case BackendDriverTarget::cuda: ok = shared::emit_cuda_text(*req.stream, text, nullptr) && shared::write_text_file(out_path.string(), text); break;
            case BackendDriverTarget::rocm: ok = shared::emit_rocm_text(*req.stream, text, nullptr) && shared::write_text_file(out_path.string(), text); break;
            default: ok = false; break;
        }

        per.ok = ok;
        per.output_path = out_path.string();
        per.exit_code = ok ? 0 : -1;
        if (!ok) {
            per.message = "IRStream backend emission failed";
            all_ok = false;
        }
        result.per_target.push_back(std::move(per));
    }

    result.ok = all_ok;
    if (!result.ok && result.message.empty()) {
        result.message = "one or more backend driver emissions failed";
    }
    return result;
}

} // namespace dgtl::backend::driver

// ================================
// COMPATIBILITY ALIAS (dgtl::core)
// ================================
namespace dgtl::core {
    using BackendDriverTarget = dgtl::backend::driver::BackendDriverTarget;
    using BackendDriverRequest = dgtl::backend::driver::BackendDriverRequest;
    using BackendDriverResult = dgtl::backend::driver::BackendDriverResult;
    using IRStreamEmitRequest = dgtl::backend::driver::IRStreamEmitRequest;
    using FrontendEmitResult = dgtl::backend::driver::FrontendEmitResult;
    using BackendDriverABIBridgeConfig = dgtl::backend::driver::BackendDriverABIBridgeConfig;

    inline auto all_backend_driver_targets() {
        return dgtl::backend::driver::all_backend_driver_targets();
    }

    inline auto backend_driver_target_name(BackendDriverTarget target) {
        return dgtl::backend::driver::backend_driver_target_name(target);
    }

    inline auto backend_driver_target_extension(BackendDriverTarget target) {
        return dgtl::backend::driver::backend_driver_target_extension(target);
    }

    inline auto backend_target_abi_convention(BackendDriverTarget target) {
        return dgtl::backend::driver::backend_target_abi_convention(target);
    }

    inline void install_default_external_intrinsic_hooks() {
        dgtl::backend::driver::install_default_external_intrinsic_hooks();
    }

    inline void seed_default_runtime_abi_registry(
        dgtl::core::DGTLABIRegistry& abi,
        const BackendDriverABIBridgeConfig& cfg = BackendDriverABIBridgeConfig{}) {
        dgtl::backend::driver::seed_default_runtime_abi_registry(abi, cfg);
    }

    inline auto to_lowering_abi_value_class(dgtl::core::DGTLABIValueClass value_class) {
        return dgtl::backend::driver::to_lowering_abi_value_class(value_class);
    }

    inline uint32_t dgtl_abi_value_size_bytes(dgtl::core::DGTLABIValueClass value_class) {
        return dgtl::backend::driver::dgtl_abi_value_size_bytes(value_class);
    }

    inline auto lower_call_signature_layout(
        const dgtl::core::DGTLCallSignature& signature,
        dgtl::backend::lowering::ABIConvention convention,
        const BackendDriverABIBridgeConfig& cfg = BackendDriverABIBridgeConfig{}) {
        return dgtl::backend::driver::lower_call_signature_layout(signature, convention, cfg);
    }

    inline bool parse_backend_driver_target(std::string value, BackendDriverTarget& out) {
        return dgtl::backend::driver::parse_backend_driver_target(value, out);
    }

    inline auto run_backend_driver(const BackendDriverRequest& req) {
        return dgtl::backend::driver::run_backend_driver(req);
    }

    inline bool validate_backend_driver_request(const BackendDriverRequest& req,
                                                std::string* error = nullptr) {
        return dgtl::backend::driver::validate_backend_driver_request(req, error);
    }

    inline auto make_backend_driver_request(std::string_view dgtl_executable_path,
                                           BackendDriverTarget target,
                                           std::string_view output_path,
                                           bool quiet = true,
                                           bool no_print = true,
                                           std::string_view ir_mode = "int") {
        return dgtl::backend::driver::make_backend_driver_request(dgtl_executable_path, target, output_path, quiet, no_print, ir_mode);
    }

    inline auto emit_ir_stream(const IRStreamEmitRequest& req) {
        return dgtl::backend::driver::emit_ir_stream(req);
    }
}
