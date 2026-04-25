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

namespace dgtl::core {

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
};

struct BackendDriverRequest {
    std::string dgtl_executable_path;
    BackendDriverTarget target = BackendDriverTarget::arm64;
    std::string output_path;
    std::string frontend_payload_path;
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
    };
}

struct FrontendEmitRequest {
    std::string dgtl_executable_path;
    std::string module_name;
    std::string output_dir = "build/out";
    std::string frontend_payload_path;
    bool emit_all = false;
    std::optional<BackendDriverTarget> target;
    bool quiet = true;
    bool no_print = true;
    std::string ir_mode = "int";
};

struct FrontendEmitResult {
    bool ok = false;
    std::vector<BackendDriverResult> per_target;
    std::string message;
};

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

    if (!req.frontend_payload_path.empty()) {
        const std::filesystem::path payload_path =
            std::filesystem::absolute(std::filesystem::path(req.frontend_payload_path)).lexically_normal();
        cmd << " --frontend-payload '" << quote_ps_single(payload_path.string()) << "'";
    }

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

    if (!req.frontend_payload_path.empty()) {
        const std::filesystem::path payload_path =
            std::filesystem::absolute(std::filesystem::path(req.frontend_payload_path)).lexically_normal();
        cmd << " --frontend-payload " << quote_cmd_arg(payload_path.string());
    }

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

    if (!req.frontend_payload_path.empty()) {
        const std::filesystem::path payload_path =
            std::filesystem::absolute(std::filesystem::path(req.frontend_payload_path)).lexically_normal();
        if (!std::filesystem::exists(payload_path)) {
            if (error) {
                *error = "Frontend payload file not found at " + payload_path.string();
            }
            return false;
        }
    }

    return true;
}

inline BackendDriverRequest make_backend_driver_request(std::string_view dgtl_executable_path,
                                                        BackendDriverTarget target,
                                                        std::string_view output_path,
                                                        std::string_view frontend_payload_path = "",
                                                        bool quiet = true,
                                                        bool no_print = true,
                                                        std::string_view ir_mode = "int") {
    BackendDriverRequest req;
    req.dgtl_executable_path = std::string(dgtl_executable_path);
    req.target = target;
    req.output_path = std::string(output_path);
    req.frontend_payload_path = std::string(frontend_payload_path);
    req.quiet = quiet;
    req.no_print = no_print;
    req.ir_mode = std::string(ir_mode);
    return req;
}

inline FrontendEmitResult emit_frontend_module(const FrontendEmitRequest& req) {
    FrontendEmitResult result;

    if (req.dgtl_executable_path.empty()) {
        result.message = "DGTL executable path is empty";
        return result;
    }
    if (req.module_name.empty()) {
        result.message = "Frontend module name is empty";
        return result;
    }

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
        std::string file_name;
        if (target == BackendDriverTarget::wasm ||
            target == BackendDriverTarget::elfobj ||
            target == BackendDriverTarget::staticlib ||
            target == BackendDriverTarget::coffobj ||
            target == BackendDriverTarget::cofflib ||
            target == BackendDriverTarget::macho) {
            file_name = req.module_name + backend_driver_target_extension(target);
        } else {
            file_name = req.module_name + "." + backend_driver_target_name(target) + backend_driver_target_extension(target);
        }

        std::filesystem::path out_file = std::filesystem::path(req.output_dir) / file_name;

        auto driver_req = make_backend_driver_request(req.dgtl_executable_path,
                                                      target,
                                                      out_file.string(),
                                                      req.frontend_payload_path,
                                                      req.quiet,
                                                      req.no_print,
                                                      req.ir_mode);

        const auto driver_result = run_backend_driver(driver_req);
        if (!driver_result.ok) {
            all_ok = false;
        }
        result.per_target.push_back(driver_result);
    }

    result.ok = all_ok;
    if (!result.ok && result.message.empty()) {
        result.message = "one or more backend driver emissions failed";
    }

    return result;
}

} // namespace dgtl::core
