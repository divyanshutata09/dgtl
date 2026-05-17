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

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "dgtl/analysis/Polyhedral_Scop_Unified.hpp"
#include "dgtl/core/Target.hpp"
#include "dgtl/ir/gen/DGTL_Gen_Ops.hpp"
#include "dgtl/isa/emitter/Arm32neon_RiscV64_WasmGSFMA.hpp"

#if __has_include("dgtl/isa/emitter/GPGPU_Fusion_FastEmission.hpp")
#include "dgtl/isa/emitter/GPGPU_Fusion_FastEmission.hpp"
#define DGTL_HAS_GPGPU_FUSION_ENGINE 1
#else
#define DGTL_HAS_GPGPU_FUSION_ENGINE 0
#endif

namespace dgtl::backend::driver::shared {

inline std::string to_lower_ascii(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
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
        if (ec) {
            return false;
        }
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
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
        if (ec) {
            return false;
        }
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    if (!bytes.empty()) {
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    return out.good();
}

inline bool emit_arm32_text(std::string& out_text) {
    std::vector<std::string> lines;
    lines.reserve(64);
    lines.push_back(".arch armv7-a");
    lines.push_back(".fpu neon");
    lines.push_back(".syntax unified");
    lines.push_back("DGTL_arm32_entry:");
    dgtl::isa::ARM32NEONGatherScatter::emit_gather(lines, 0, 0, 1, 4, 4);
    dgtl::isa::ARM32NEONGatherScatter::emit_scatter(lines, 2, 2, 1, 4);
    lines.push_back("bx lr");

    std::ostringstream oss;
    for (const auto& line : lines) {
        oss << line << '\n';
    }
    out_text = oss.str();
    return !out_text.empty();
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
    const dgtl::ir::IRStream& stream,
    std::string& out_text,
    const dgtl::analysis::polyhedral::PolyhedralSchedule* schedule = nullptr) {
#if DGTL_HAS_GPGPU_FUSION_ENGINE
    dgtl::core::TargetMachine target(dgtl::core::ArchType::X86_64_AVX512);
    dgtl::dialect::GPGPUFusionEngine engine(dgtl::dialect::DeviceType::NVIDIA_PTX, target);
    if (schedule != nullptr && schedule->is_valid() && schedule->has_any_tiled_regions()) {
        out_text = engine.fuse_to_source_tiled(stream, *schedule);
    } else {
        out_text = engine.fuse_to_source(stream);
    }
    return !out_text.empty();
#else
    (void)schedule;
    std::ostringstream oss;
    oss << "// DGTL generated CUDA-style fallback from declarative op metadata\n";
    oss << "// GPU fusion engine header not present in source tree\n";
    oss << "extern \"C\" __global__ void dgtl_kernel() {\n";
    for (size_t i = 0; i < stream.size(); ++i) {
        const auto n = stream.get_node_readonly(i);
        if (auto* def = dgtl::ir::gen::find_generated_op_def(n.opcode)) {
            if ((def->interface_bits & static_cast<uint32_t>(dgtl::ir::gen::GenInterface::GPU)) != 0u) {
                oss << "  // gpu-op: " << def->name << "\n";
            }
        }
    }
    oss << "}\n";
    out_text = oss.str();
    return !out_text.empty();
#endif
}

inline bool emit_rocm_text(
    const dgtl::ir::IRStream& stream,
    std::string& out_text,
    const dgtl::analysis::polyhedral::PolyhedralSchedule* schedule = nullptr) {
#if DGTL_HAS_GPGPU_FUSION_ENGINE
    dgtl::core::TargetMachine target(dgtl::core::ArchType::AARCH64_SVE);
    dgtl::dialect::GPGPUFusionEngine engine(dgtl::dialect::DeviceType::AMD_GCN_CDNA, target);
    if (schedule != nullptr && schedule->is_valid() && schedule->has_any_tiled_regions()) {
        out_text = engine.fuse_to_source_tiled(stream, *schedule);
    } else {
        out_text = engine.fuse_to_source(stream);
    }
    return !out_text.empty();
#else
    (void)schedule;
    std::ostringstream oss;
    oss << "// DGTL generated ROCm-style fallback from declarative op metadata\n";
    oss << "extern \"C\" __attribute__((global)) void dgtl_kernel() {\n";
    for (size_t i = 0; i < stream.size(); ++i) {
        const auto n = stream.get_node_readonly(i);
        if (auto* def = dgtl::ir::gen::find_generated_op_def(n.opcode)) {
            if ((def->interface_bits & static_cast<uint32_t>(dgtl::ir::gen::GenInterface::GPU)) != 0u) {
                oss << "  // gpu-op: " << def->name << "\n";
            }
        }
    }
    oss << "}\n";
    out_text = oss.str();
    return !out_text.empty();
#endif
}

} // namespace dgtl::backend::driver::shared
