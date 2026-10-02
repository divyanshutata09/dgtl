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

module;

#include <cstdlib>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

export module dgtl.linker.utils;

import dgtl.target;
import dgtl.isa.riscv64.riscv64_emitter;
import dgtl.mir.machine_ir;

export namespace dgtl::linker::driver::shared {

inline std::string to_lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

inline std::string trim_ascii(std::string value) {
    auto is_ws = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!value.empty() && is_ws(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    size_t begin = 0;
    while (begin < value.size() && is_ws(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    return value.substr(begin);
}

template <typename PolyhedralModeT>
inline bool parse_polyhedral_mode(std::string mode_str,
                                  PolyhedralModeT& out_mode,
                                  std::string& out_name) {
    mode_str = to_lower_ascii(std::move(mode_str));
    if (mode_str == "off") {
        out_mode = PolyhedralModeT::off;
        out_name = "off";
        return true;
    }
    if (mode_str == "safe") {
        out_mode = PolyhedralModeT::safe;
        out_name = "safe";
        return true;
    }
    if (mode_str == "aggressive") {
        out_mode = PolyhedralModeT::aggressive;
        out_name = "aggressive";
        return true;
    }
    return false;
}

template <typename PolyhedralParallelPolicyT>
inline bool parse_polyhedral_parallel_policy(std::string policy_str,
                                             PolyhedralParallelPolicyT& out_policy,
                                             std::string& out_name) {
    policy_str = to_lower_ascii(std::move(policy_str));
    if (policy_str == "auto") {
        out_policy = PolyhedralParallelPolicyT::auto_policy;
        out_name = "auto";
        return true;
    }
    if (policy_str == "off") {
        out_policy = PolyhedralParallelPolicyT::off;
        out_name = "off";
        return true;
    }
    if (policy_str == "on") {
        out_policy = PolyhedralParallelPolicyT::on;
        out_name = "on";
        return true;
    }
    return false;
}

inline std::string bytes_to_hex(const std::vector<uint8_t>& bytes) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(bytes.size() * 3);
    for (size_t i = 0; i < bytes.size(); ++i) {
        const uint8_t b = bytes[i];
        out.push_back(kHex[(b >> 4) & 0xF]);
        out.push_back(kHex[b & 0xF]);
        if (i + 1 < bytes.size()) {
            out.push_back(' ');
        }
    }
    return out;
}

inline bool write_text_file(const std::string& path, const std::string& content) {
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
        if (ec) [[unlikely]] {
            return false;
        }
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) [[unlikely]] {
        return false;
    }

    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    return out.good();
}

inline bool write_binary_file(const std::string& path, const std::vector<uint8_t>& bytes) {
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
        if (ec) [[unlikely]] {
            return false;
        }
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) [[unlikely]] {
        return false;
    }

    if (!bytes.empty()) {
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    return out.good();
}

inline bool emit_arm32_text(std::string& /*out_text*/) {
    return false; // ARM32 backend removed in modern DGTL pipeline
}

inline bool emit_riscv64_text(std::string& out_text) {
    std::vector<std::string> lines;
    lines.reserve(64);
    lines.push_back("# RISC-V 64 (RV64GC + V-extension) Emission");
    lines.push_back(".arch rv64gcv");
    lines.push_back("DGTL_riscv64_entry:");
    dgtl::isa::RiscV64GatherScatter::emit_gather(lines, 0, 0, 1, 4, true);
    dgtl::isa::RiscV64GatherScatter::emit_scatter(lines, 1, 0, 1, 4, true);
    lines.push_back("ret");

    std::ostringstream oss;
    for (const auto& line : lines) {
        oss << line << '\n';
    }
    out_text = oss.str();
    return !out_text.empty();
}

inline bool emit_cuda_text(
    const dgtl::machine::MachineFunction& function,
    std::string& out_text) {
    std::ostringstream oss;
    oss << "// DGTL generated CUDA-style kernel from MachineFunction: " << function.name() << "\n";
    oss << "extern \"C\" __global__ void " << (function.name().empty() ? "dgtl_kernel" : function.name()) << "() {\n";
    for (const auto& blk : function.blocks()) {
        oss << "bb_" << blk.id << ":\n";
        for (auto iid : blk.instructions) {
            const auto* inst = function.instruction(iid);
            if (!inst) continue;
            oss << "  // form: " << static_cast<uint32_t>(inst->form) << " opcode: 0x" << std::hex << inst->opcode << std::dec << "\n";
        }
    }
    oss << "}\n";
    out_text = oss.str();
    return !out_text.empty();
}

inline bool emit_rocm_text(
    const dgtl::machine::MachineFunction& function,
    std::string& out_text) {
    std::ostringstream oss;
    oss << "// DGTL generated ROCm-style HIP kernel from MachineFunction: " << function.name() << "\n";
    oss << "extern \"C\" __attribute__((global)) void " << (function.name().empty() ? "dgtl_kernel" : function.name()) << "() {\n";
    for (const auto& blk : function.blocks()) {
        oss << "bb_" << blk.id << ":\n";
        for (auto iid : blk.instructions) {
            const auto* inst = function.instruction(iid);
            if (!inst) continue;
            oss << "  // form: " << static_cast<uint32_t>(inst->form) << " opcode: 0x" << std::hex << inst->opcode << std::dec << "\n";
        }
    }
    oss << "}\n";
    out_text = oss.str();
    return !out_text.empty();
}

} // namespace dgtl::linker::driver::shared

// Backward compatibility namespaces
export namespace dgtl::linker::driver::utils {
    using namespace dgtl::linker::driver::shared;
}

export namespace dgtl::backend::driver::shared {
    using namespace dgtl::linker::driver::shared;
}
