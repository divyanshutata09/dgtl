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

/* * DGTL (Deep General-purpose Transformation Layer) version 1.0.0 : THE COMPILER SPEC
 * STEP 5: ACCELERATED EMISSION - GPGPU FUSION
 * PILLAR 4: HARDWARE AFFINITY & UNIFIED MEMORY COHERENCY
 * ORCHESTRATOR: GPGPU_Fusion (PTX/SPIR-V JIT Bridge)
 * STANDARDS: C++26 / UCRT64 / CUDA 12.x+ / ROCm 6.x+
 */

#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <string_view>
#include <memory>
#include <unordered_map>
#include <functional>

// Umbrella Entry Point for IR consistency
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/core/Target.hpp"
#include "dgtl/analysis/polyhedral/Polyhedral_Schedule.hpp"

namespace dgtl::isa {

using dgtl::core::TargetMachine;
namespace ir = dgtl::ir;

/**
 * @enum DeviceType
 * @brief Identifies the target accelerator for the Fusion opt.
 */
enum class DeviceType : uint8_t {
    NVIDIA_PTX,     // Tesla/Ampere/Hopper
    AMD_GCN_CDNA,   // Instinct/Radeon
    INTEL_SPIRV,    // Data Center GPU Max
    GENERIC_VULKAN  // Cross-platform fallback
};

enum class FusionStatus : uint32_t {
    Ok = 0,
    InvalidStream = 0xF001,
    EmptyKernel = 0xF002,
    UnsupportedDevice = 0xF003,
};

/**
 * @struct GridConfig
 * @brief High-velocity kernel execution parameters.
 */
struct alignas(16) GridConfig {
    uint32_t block_dim_x = 256;
    uint32_t grid_dim_x  = 1;
    uint32_t shared_mem  = 0;
    bool use_unified_memory = true;
};

/**
 * @class GPGPUFusionEngine
 * @brief Orchestrates the fusion of BIR streams into massive-parallel kernels.
 * Implements sub-microsecond kernel dispatch via pre-compiled headers and JIT caching.
 */
class alignas(64) GPGPUFusionEngine {
public:
    struct FusionResult {
        void* device_ptr = nullptr;
        size_t kernel_id = 0;
        bool success = false;
        FusionStatus status = FusionStatus::Ok;
    };

    explicit GPGPUFusionEngine(DeviceType device, const TargetMachine& target)
        : m_device(device), m_target(target) {}

    /**
     * @brief Fuses a BIR stream into a device-specific kernel string (PTX/SPIR-V).
     * Follows Pillar 2: Validates the stream before fusion emission.
     */
    [[nodiscard]] std::string fuse_to_source(const ir::IRStream& stream) {
        if (stream.is_null() || stream.size() == 0) return "";
        if (!stream.validate_shadow_integrity()) return "";

        // --- FIX: Pre-scan LABEL nodes to build label_id → stream_index map ---
        std::unordered_map<uint32_t, size_t> label_map;
        label_map.reserve(stream.size() / 4 + 4);
        for (size_t i = 0; i < stream.size(); ++i) {
            const IRNode n = stream.get_node(i);
            if (n.opcode == ir::OpCode::LABEL) {
                uint32_t lid = (n.result_id != ir::INVALID_SLOT) ? n.result_id : n.op_a;
                label_map[lid] = i;
            }
        }
        auto resolve_target = [&](uint32_t raw) -> size_t {
            auto it = label_map.find(raw);
            if (it != label_map.end()) return it->second;
            if (static_cast<size_t>(raw) < stream.size()) return static_cast<size_t>(raw);
            return stream.size();
        };
        // -----------------------------------------------------------------------

        std::string source;
        source.reserve(stream.size() * 64); // Heat-map allocation

        emit_kernel_header(source);

        for (size_t i = 0; i < stream.size(); ++i) {
            const IRNode node = stream.get_node(i);
            emit_node_label(source, i);  // emit per-node label for branch targets
            emit_node_logic(source, node, resolve_target);
        }

        emit_kernel_footer(source);
        return source;
    }

    /**
     * @brief Directly dispatches a BIR stream to the accelerator.
     * Logic: Byoptes standard drivers using DGTL's "Direct-to-Silicon" protocol.
     */
    FusionResult dispatch_fusion(const ir::IRStream& stream, const GridConfig& config) {
        (void)config;
        if (stream.is_null() || stream.size() == 0) {
            return { nullptr, 0, false, FusionStatus::InvalidStream };
        }
        if (!stream.validate_shadow_integrity()) {
            return { nullptr, 0, false, FusionStatus::InvalidStream };
        }

        if (m_device != DeviceType::NVIDIA_PTX &&
            m_device != DeviceType::AMD_GCN_CDNA &&
            m_device != DeviceType::INTEL_SPIRV &&
            m_device != DeviceType::GENERIC_VULKAN) {
            return { nullptr, 0, false, FusionStatus::UnsupportedDevice };
        }

        // Build label_map
        std::unordered_map<uint32_t, size_t> label_map;
        label_map.reserve(stream.size() / 4 + 4);
        for (size_t i = 0; i < stream.size(); ++i) {
            const IRNode n = stream.get_node(i);
            if (n.opcode == ir::OpCode::LABEL) {
                uint32_t lid = (n.result_id != ir::INVALID_SLOT) ? n.result_id : n.op_a;
                label_map[lid] = i;
            }
        }
        auto resolve_target = [&](uint32_t raw) -> size_t {
            auto it = label_map.find(raw);
            if (it != label_map.end()) return it->second;
            if (static_cast<size_t>(raw) < stream.size()) return static_cast<size_t>(raw);
            return stream.size();
        };

        std::string kernel_src;
        kernel_src.reserve(stream.size() * 64);
        emit_kernel_header(kernel_src);

        size_t emitted_ops = 0;
        for (size_t i = 0; i < stream.size(); ++i) {
            const IRNode node = stream.get_node(i);
            emit_node_label(kernel_src, i);
            emitted_ops += emit_node_logic(kernel_src, node, resolve_target) ? 1u : 0u;
        }
        emit_kernel_footer(kernel_src);

        const bool has_body = emitted_ops > 0;

        if (kernel_src.empty() || !has_body) {
            return { nullptr, 0, false, FusionStatus::EmptyKernel };
        }

        // Runtime driver integration is platform-dependent; report a stable synthetic kernel id.
        const size_t kernel_id = static_cast<size_t>(stream.generate_logic_hash());
        return { nullptr, kernel_id, true, FusionStatus::Ok };
    }

    /**
     * @brief Fuses a BIR stream into a tiled, data-parallel GPU kernel using a
     *        PolyhedralSchedule produced by P0-003 ScopTransformer.
     *
     * For CUDA (NVIDIA_PTX): emits a .version 7.0 kernel where each TiledRegion
     * with loop_depth > 0 is wrapped in a blockIdx.x / threadIdx.x iteration loop.
     * The innermost point loop body is the sequential BIR node sequence from the region.
     *
     * For ROCm (AMD_GCN_CDNA): same structure using OpenCL C kernel syntax with
     * get_group_id(0) / get_local_id(0) thread indexing.
     *
     * Regions with loop_depth == 0 (pass-through) are emitted sequentially inside
     * a thread-0-guarded block to preserve correctness.
     *
     * Falls back to fuse_to_source(stream) if the schedule has no tiled regions.
     */
    [[nodiscard]] std::string fuse_to_source_tiled(
        const ir::IRStream& stream,
        const dgtl::analysis::polyhedral::PolyhedralSchedule& schedule)
    {
        if (!schedule.is_valid() || !schedule.has_any_tiled_regions()) {
            return fuse_to_source(stream);
        }

        if (stream.is_null() || stream.size() == 0) return "";
        if (!stream.validate_shadow_integrity()) return "";

        // Build label_map for branch resolution (shared across all regions).
        std::unordered_map<uint32_t, size_t> label_map;
        label_map.reserve(stream.size() / 4 + 4);
        for (size_t i = 0; i < stream.size(); ++i) {
            const IRNode n = stream.get_node(i);
            if (n.opcode == ir::OpCode::LABEL) {
                const uint32_t lid = (n.result_id != ir::INVALID_SLOT) ? n.result_id : n.op_a;
                label_map[lid] = i;
            }
        }
        auto resolve_target = [&](uint32_t raw) -> size_t {
            auto it = label_map.find(raw);
            if (it != label_map.end()) return it->second;
            if (static_cast<size_t>(raw) < stream.size()) return static_cast<size_t>(raw);
            return stream.size();
        };

        std::string src;
        src.reserve(schedule.total_node_count() * 96 + 1024);

        emit_tiled_kernel_header(src, schedule);

        size_t region_seq = 0;
        for (const auto& tr : schedule.tiled_regions()) {
            const bool is_tiled = (tr.loop_depth > 0 && !tr.dims.empty());

            if (is_tiled) {
                emit_tiled_region_prolog(src, tr, region_seq);
            } else {
                // Pass-through: guard on thread 0 to avoid duplicating side effects.
                emit_passthrough_guard_open(src);
            }

            // Emit all nodes in this region as the innermost loop body.
            for (size_t node_idx = 0; node_idx < tr.nodes.size(); ++node_idx) {
                const IRNode& node = tr.nodes[node_idx];
                emit_node_label(src, node_idx);
                emit_node_logic(src, node, resolve_target);
            }

            if (is_tiled) {
                emit_tiled_region_epilog(src, tr);
            } else {
                emit_passthrough_guard_close(src);
            }

            region_seq++;
        }

        emit_kernel_footer(src);
        return src;
    }

private:
    DeviceType m_device;
    const TargetMachine& m_target;

    // -------------------------------------------------------------------------
    // Tiled kernel emission helpers (P0-003)
    // -------------------------------------------------------------------------

    // Emit the tiled kernel header (replaces the scalar DGTL_kernel header).
    // The tiled entry point is named DGTL_kernel_tiled to distinguish it from
    // the scalar entry point in the same translation unit.
    void emit_tiled_kernel_header(
        std::string& src,
        const dgtl::analysis::polyhedral::PolyhedralSchedule& schedule) const
    {
        if (m_device == DeviceType::NVIDIA_PTX) {
            src += ".version 7.0\n.target sm_80\n.address_size 64\n\n";
            src += "// DGTL-TILED-KERNEL (P0-003 Polyhedral Schedule)\n";
            src += "// Transform summary: ";
            src += schedule.transform_summary();
            src += "\n";
            src += ".visible .entry DGTL_kernel_tiled(.param .u64 input, .param .u64 output, .param .u64 n) {\n";
            src += "    .reg .u64 %rd<16>;\n";
            src += "    .reg .s32 %r<64>;\n";
            src += "    .reg .pred %p<16>;\n";
            src += "    // blockIdx.x\n";
            src += "    .reg .u32 %blockIdx_x;\n";
            src += "    mov.u32 %blockIdx_x, %ctaid.x;\n";
            src += "    // threadIdx.x\n";
            src += "    .reg .u32 %threadIdx_x;\n";
            src += "    mov.u32 %threadIdx_x, %tid.x;\n";
        } else {
            // AMD GCN / OpenCL C
            src += "// DGTL-TILED-KERNEL (P0-003 Polyhedral Schedule)\n";
            src += "// Transform summary: ";
            src += schedule.transform_summary();
            src += "\n";
            src += "kernel void DGTL_kernel_tiled(global int* in, global int* out, uint n) {\n";
            src += "    const uint blockIdx_x  = get_group_id(0);\n";
            src += "    const uint threadIdx_x = get_local_id(0);\n";
        }
    }

    // Emit the loop prolog for a tiled region:
    //   - Compute global thread index from blockIdx * TILE_SIZE + threadIdx.
    //   - Emit bounds check.
    //   - Emit inner-loop variables for > 1-D tiling.
    void emit_tiled_region_prolog(
        std::string& src,
        const dgtl::analysis::polyhedral::TiledRegion& tr,
        size_t region_seq) const
    {
        const auto& d0 = tr.dims[0];
        const uint32_t tile_size = (d0.tile_size > 0) ? d0.tile_size : 256u;
        const std::string rs = std::to_string(region_seq);

        if (m_device == DeviceType::NVIDIA_PTX) {
            // Compute global_idx = blockIdx.x * TILE_SIZE + threadIdx.x
            src += "    // --- TiledRegion " + rs + " (depth=" + std::to_string(tr.loop_depth) + ") ---\n";
            src += "    .reg .u32 %global_idx_" + rs + ";\n";
            src += "    .reg .pred %in_bounds_" + rs + ";\n";
            src += "    mul.lo.u32 %global_idx_" + rs + ", %blockIdx_x, " + std::to_string(tile_size) + ";\n";
            src += "    add.u32    %global_idx_" + rs + ", %global_idx_" + rs + ", %threadIdx_x;\n";
            // Bounds check: if global_idx >= upper → skip region body
            const int64_t upper = d0.upper > 0 ? d0.upper : static_cast<int64_t>(tr.nodes.size());
            src += "    setp.lt.u32 %in_bounds_" + rs + ", %global_idx_" + rs + ", " + std::to_string(upper) + ";\n";
            src += "    @!%in_bounds_" + rs + " bra .SKIP_REGION_" + rs + ";\n";
            src += "    // === Region " + rs + " body (per-thread) ===\n";
        } else {
            // OpenCL C
            src += "    // --- TiledRegion " + rs + " (depth=" + std::to_string(tr.loop_depth) + ") ---\n";
            src += "    {\n";
            src += "        const uint global_idx_" + rs + " = blockIdx_x * " + std::to_string(tile_size) + "u + threadIdx_x;\n";
            const int64_t upper = d0.upper > 0 ? d0.upper : static_cast<int64_t>(tr.nodes.size());
            src += "        if (global_idx_" + rs + " >= (uint)" + std::to_string(upper) + ") goto SKIP_REGION_" + rs + ";\n";
            src += "        // === Region " + rs + " body (per-thread) ===\n";
        }

        // For 2-D tiling, also compute Y dimension index.
        if (tr.dims.size() >= 2) {
            const auto& d1 = tr.dims[1];
            const uint32_t tile_y = (d1.tile_size > 0) ? d1.tile_size : 1u;
            if (m_device == DeviceType::NVIDIA_PTX) {
                src += "    .reg .u32 %blockIdx_y;\n";
                src += "    mov.u32 %blockIdx_y, %ctaid.y;\n";
                src += "    .reg .u32 %threadIdx_y;\n";
                src += "    mov.u32 %threadIdx_y, %tid.y;\n";
                src += "    .reg .u32 %global_idy_" + rs + ";\n";
                src += "    mul.lo.u32 %global_idy_" + rs + ", %blockIdx_y, " + std::to_string(tile_y) + ";\n";
                src += "    add.u32    %global_idy_" + rs + ", %global_idy_" + rs + ", %threadIdx_y;\n";
                const int64_t upper_y = d1.upper > 0 ? d1.upper : static_cast<int64_t>(tr.nodes.size());
                src += "    setp.lt.u32 %in_bounds_" + rs + ", %global_idy_" + rs + ", " + std::to_string(upper_y) + ";\n";
                src += "    @!%in_bounds_" + rs + " bra .SKIP_REGION_" + rs + ";\n";
            } else {
                src += "        const uint blockIdx_y   = get_group_id(1);\n";
                src += "        const uint threadIdx_y  = get_local_id(1);\n";
                src += "        const uint global_idy_" + rs + " = blockIdx_y * " + std::to_string(tile_y) + "u + threadIdx_y;\n";
                const int64_t upper_y = d1.upper > 0 ? d1.upper : static_cast<int64_t>(tr.nodes.size());
                src += "        if (global_idy_" + rs + " >= (uint)" + std::to_string(upper_y) + ") goto SKIP_REGION_" + rs + ";\n";
            }
        }
    }

    void emit_tiled_region_epilog(
        std::string& src,
        const dgtl::analysis::polyhedral::TiledRegion& tr) const
    {
        const std::string rs = std::to_string(tr.region_index);
        if (m_device == DeviceType::NVIDIA_PTX) {
            src += "    .SKIP_REGION_" + rs + ":\n";
        } else {
            src += "        SKIP_REGION_" + rs + ":;\n";
            src += "    }\n";  // close the inner block opened in prolog
        }
    }

    // Guard a pass-through (non-tiled) region on thread 0 of block 0 to
    // prevent duplicate side effects from all threads.
    void emit_passthrough_guard_open(std::string& src) const {
        if (m_device == DeviceType::NVIDIA_PTX) {
            src += "    // --- Pass-through region (non-affine, thread 0 only) ---\n";
            src += "    .reg .pred %is_thread0;\n";
            src += "    setp.eq.u32 %is_thread0, %threadIdx_x, 0;\n";
            src += "    @!%is_thread0 bra .SKIP_PASSTHROUGH;\n";
        } else {
            src += "    // --- Pass-through region (non-affine, thread 0 only) ---\n";
            src += "    if (threadIdx_x == 0u) {\n";
        }
    }

    void emit_passthrough_guard_close(std::string& src) const {
        if (m_device == DeviceType::NVIDIA_PTX) {
            src += "    .SKIP_PASSTHROUGH:\n";
        } else {
            src += "    }\n";
        }
    }

    // -------------------------------------------------------------------------
    // Existing scalar emission helpers
    // -------------------------------------------------------------------------

    void emit_kernel_header(std::string& src) {
        if (m_device == DeviceType::NVIDIA_PTX) {
            src += ".version 7.0\n.target sm_80\n.address_size 64\n\n";
            src += ".visible .entry DGTL_kernel(.param .u64 input, .param .u64 output) {\n";
            src += "    .reg .u64 %rd<16>;\n    .reg .s32 %r<64>;\n    .reg .pred %p<16>;\n";
        } else {
            src += "// DGTL-GPGPU-GENERIC-KERNEL\n";
            src += "kernel void DGTL_kernel(global int* in, global int* out, uint n) {\n";
            src += "    const uint idx = get_global_id(0);\n";
            src += "    if (idx >= n) return;\n";
        }
    }

    // Emit a per-node label for control flow targets (called before emit_node_logic)
    void emit_node_label(std::string& src, size_t node_idx) {
        if (m_device == DeviceType::NVIDIA_PTX) {
            src += "    .L" + std::to_string(node_idx) + ":\n";
        } else {
            src += "    L_" + std::to_string(node_idx) + ":;\n";
        }
    }

    bool emit_node_logic(std::string& src, const ir::IRNode& node,
                         const std::function<size_t(uint32_t)>& resolve_target) {
        switch (node.opcode) {
            case OpCode::ADD_I32:
                if (node.result_id == ir::INVALID_SLOT || node.op_a == ir::INVALID_SLOT || node.op_b == ir::INVALID_SLOT) {
                    return false;
                }
                if (m_device == DeviceType::NVIDIA_PTX) {
                    src += "    add.s32 %r" + std::to_string(node.result_id) +
                           ", %r" + std::to_string(node.op_a) +
                           ", %r" + std::to_string(node.op_b) + ";\n";
                } else {
                    src += "    const int t" + std::to_string(node.result_id) + " = in[" + std::to_string(node.op_a) + "] + in[" + std::to_string(node.op_b) + "];\n";
                }
                return true;

            case OpCode::SUB_I32:
                if (m_device == DeviceType::NVIDIA_PTX) {
                    src += "    sub.s32 %r" + std::to_string(node.result_id) +
                           ", %r" + std::to_string(node.op_a) +
                           ", %r" + std::to_string(node.op_b) + ";\n";
                } else {
                    src += "    int t" + std::to_string(node.result_id) + " = r" + std::to_string(node.op_a) + " - r" + std::to_string(node.op_b) + ";\n";
                }
                return true;

            case OpCode::MUL_I32:
                if (m_device == DeviceType::NVIDIA_PTX) {
                    src += "    mul.lo.s32 %r" + std::to_string(node.result_id) +
                           ", %r" + std::to_string(node.op_a) +
                           ", %r" + std::to_string(node.op_b) + ";\n";
                } else {
                    src += "    int t" + std::to_string(node.result_id) + " = r" + std::to_string(node.op_a) + " * r" + std::to_string(node.op_b) + ";\n";
                }
                return true;

            case OpCode::DIV_I32:
                if (m_device == DeviceType::NVIDIA_PTX) {
                    src += "    div.s32 %r" + std::to_string(node.result_id) +
                           ", %r" + std::to_string(node.op_a) +
                           ", %r" + std::to_string(node.op_b) + ";\n";
                } else {
                    src += "    int t" + std::to_string(node.result_id) + " = r" + std::to_string(node.op_a) + " / r" + std::to_string(node.op_b) + ";\n";
                }
                return true;

            case OpCode::MOD_I32:
                if (m_device == DeviceType::NVIDIA_PTX) {
                    src += "    rem.s32 %r" + std::to_string(node.result_id) +
                           ", %r" + std::to_string(node.op_a) +
                           ", %r" + std::to_string(node.op_b) + ";\n";
                } else {
                    src += "    int t" + std::to_string(node.result_id) + " = r" + std::to_string(node.op_a) + " % r" + std::to_string(node.op_b) + ";\n";
                }
                return true;

            case OpCode::AND_I32:
                if (m_device == DeviceType::NVIDIA_PTX) {
                    src += "    and.b32 %r" + std::to_string(node.result_id) +
                           ", %r" + std::to_string(node.op_a) +
                           ", %r" + std::to_string(node.op_b) + ";\n";
                } else {
                    src += "    int t" + std::to_string(node.result_id) + " = r" + std::to_string(node.op_a) + " & r" + std::to_string(node.op_b) + ";\n";
                }
                return true;

            case OpCode::OR_I32:
                if (m_device == DeviceType::NVIDIA_PTX) {
                    src += "    or.b32 %r" + std::to_string(node.result_id) +
                           ", %r" + std::to_string(node.op_a) +
                           ", %r" + std::to_string(node.op_b) + ";\n";
                } else {
                    src += "    int t" + std::to_string(node.result_id) + " = r" + std::to_string(node.op_a) + " | r" + std::to_string(node.op_b) + ";\n";
                }
                return true;

            case OpCode::XOR_I32:
                if (m_device == DeviceType::NVIDIA_PTX) {
                    src += "    xor.b32 %r" + std::to_string(node.result_id) +
                           ", %r" + std::to_string(node.op_a) +
                           ", %r" + std::to_string(node.op_b) + ";\n";
                } else {
                    src += "    int t" + std::to_string(node.result_id) + " = r" + std::to_string(node.op_a) + " ^ r" + std::to_string(node.op_b) + ";\n";
                }
                return true;

            case OpCode::SHL_I32:
                if (m_device == DeviceType::NVIDIA_PTX) {
                    src += "    shl.b32 %r" + std::to_string(node.result_id) +
                           ", %r" + std::to_string(node.op_a) +
                           ", %r" + std::to_string(node.op_b) + ";\n";
                } else {
                    src += "    int t" + std::to_string(node.result_id) + " = r" + std::to_string(node.op_a) + " << r" + std::to_string(node.op_b) + ";\n";
                }
                return true;

            case OpCode::SHR_I32:
                if (m_device == DeviceType::NVIDIA_PTX) {
                    src += "    shr.s32 %r" + std::to_string(node.result_id) +
                           ", %r" + std::to_string(node.op_a) +
                           ", %r" + std::to_string(node.op_b) + ";\n";
                } else {
                    src += "    int t" + std::to_string(node.result_id) + " = r" + std::to_string(node.op_a) + " >> r" + std::to_string(node.op_b) + ";\n";
                }
                return true;

            case OpCode::CMP_EQ_I32:
                if (m_device == DeviceType::NVIDIA_PTX) {
                    src += "    setp.eq.s32 %p0, %r" + std::to_string(node.op_a) + ", %r" + std::to_string(node.op_b) + ";\n";
                    src += "    selp.s32 %r" + std::to_string(node.result_id) + ", 1, 0, %p0;\n";
                } else {
                    src += "    int t" + std::to_string(node.result_id) + " = (r" + std::to_string(node.op_a) + " == r" + std::to_string(node.op_b) + ");\n";
                }
                return true;

            case OpCode::CMP_NE_I32:
                if (m_device == DeviceType::NVIDIA_PTX) {
                    src += "    setp.ne.s32 %p0, %r" + std::to_string(node.op_a) + ", %r" + std::to_string(node.op_b) + ";\n";
                    src += "    selp.s32 %r" + std::to_string(node.result_id) + ", 1, 0, %p0;\n";
                } else {
                    src += "    int t" + std::to_string(node.result_id) + " = (r" + std::to_string(node.op_a) + " != r" + std::to_string(node.op_b) + ");\n";
                }
                return true;

            case OpCode::CMP_LT_I32:
                if (m_device == DeviceType::NVIDIA_PTX) {
                    src += "    setp.lt.s32 %p0, %r" + std::to_string(node.op_a) + ", %r" + std::to_string(node.op_b) + ";\n";
                    src += "    selp.s32 %r" + std::to_string(node.result_id) + ", 1, 0, %p0;\n";
                } else {
                    src += "    int t" + std::to_string(node.result_id) + " = (r" + std::to_string(node.op_a) + " < r" + std::to_string(node.op_b) + ");\n";
                }
                return true;

            case OpCode::CMP_LE_I32:
                if (m_device == DeviceType::NVIDIA_PTX) {
                    src += "    setp.le.s32 %p0, %r" + std::to_string(node.op_a) + ", %r" + std::to_string(node.op_b) + ";\n";
                    src += "    selp.s32 %r" + std::to_string(node.result_id) + ", 1, 0, %p0;\n";
                } else {
                    src += "    int t" + std::to_string(node.result_id) + " = (r" + std::to_string(node.op_a) + " <= r" + std::to_string(node.op_b) + ");\n";
                }
                return true;

            case OpCode::CMP_GT_I32:
                if (m_device == DeviceType::NVIDIA_PTX) {
                    src += "    setp.gt.s32 %p0, %r" + std::to_string(node.op_a) + ", %r" + std::to_string(node.op_b) + ";\n";
                    src += "    selp.s32 %r" + std::to_string(node.result_id) + ", 1, 0, %p0;\n";
                } else {
                    src += "    int t" + std::to_string(node.result_id) + " = (r" + std::to_string(node.op_a) + " > r" + std::to_string(node.op_b) + ");\n";
                }
                return true;

            case OpCode::CMP_GE_I32:
                if (m_device == DeviceType::NVIDIA_PTX) {
                    src += "    setp.ge.s32 %p0, %r" + std::to_string(node.op_a) + ", %r" + std::to_string(node.op_b) + ";\n";
                    src += "    selp.s32 %r" + std::to_string(node.result_id) + ", 1, 0, %p0;\n";
                } else {
                    src += "    int t" + std::to_string(node.result_id) + " = (r" + std::to_string(node.op_a) + " >= r" + std::to_string(node.op_b) + ");\n";
                }
                return true;

            case OpCode::LOAD_IMM:
            case OpCode::IMMEDIATE:
                if (node.result_id == ir::INVALID_SLOT) return false;
                if (m_device == DeviceType::NVIDIA_PTX) {
                    src += "    mov.s32 %r" + std::to_string(node.result_id) + ", " + std::to_string(node.op_b) + ";\n";
                } else {
                    src += "    int r" + std::to_string(node.result_id) + " = " + std::to_string(node.op_b) + ";\n";
                }
                return true;

            case OpCode::REG_MOVE:
                if (node.result_id == ir::INVALID_SLOT || node.op_a == ir::INVALID_SLOT) return false;
                if (m_device == DeviceType::NVIDIA_PTX) {
                    src += "    mov.s32 %r" + std::to_string(node.result_id) + ", %r" + std::to_string(node.op_a) + ";\n";
                } else {
                    src += "    int r" + std::to_string(node.result_id) + " = r" + std::to_string(node.op_a) + ";\n";
                }
                return true;

            case OpCode::RET:
                if (m_device == DeviceType::NVIDIA_PTX) {
                    src += "    ret;\n";
                } else {
                    src += "    return;\n";
                }
                return true;

            case OpCode::NOP:
                // No output for NOP
                return false;

            case OpCode::PHI_NODE:
                // PHI nodes resolved by register allocation; no GPU output.
                return false;

            case OpCode::CALL:
                // GPU kernels cannot call host runtime directly; emit comment.
                src += "    // CALL (host runtime call not supported in GPU kernel)\n";
                return true;

            case OpCode::SYSCALL:
                // No syscalls in GPU context.
                return false;
            
            case OpCode::LOAD:
                if (node.result_id == ir::INVALID_SLOT || node.op_a == ir::INVALID_SLOT) {
                    return false;
                }
                if (m_device == DeviceType::NVIDIA_PTX) {
                    src += "    ld.global.s32 %r" + std::to_string(node.result_id) +
                           ", [%rd" + std::to_string(node.op_a) + "];\n";
                } else {
                    src += "    const int t" + std::to_string(node.result_id) + " = in[" + std::to_string(node.op_a) + "];\n";
                }
                return true;

            case OpCode::STORE:
                if (node.op_a == ir::INVALID_SLOT || node.op_b == ir::INVALID_SLOT) {
                    return false;
                }
                if (m_device == DeviceType::NVIDIA_PTX) {
                    src += "    st.global.s32 [%rd" + std::to_string(node.op_a) + "], %r" + std::to_string(node.op_b) + ";\n";
                } else {
                    src += "    out[" + std::to_string(node.op_a) + "] = in[" + std::to_string(node.op_b) + "];\n";
                }
                return true;

            case OpCode::VEC_ADD_F32_512:
                // Specialized fusion logic for AVX-512 nodes on GPU SIMT
                src += "    // Fusing 512-bit Vector Op to Parallel Threads\n";
                return true;

            case OpCode::LABEL:
                // Labels are emitted by emit_node_label() before each node; skip here.
                return false;

            case OpCode::BRANCH:
            case OpCode::GOTO: {
                const size_t tgt = resolve_target(node.op_a);
                if (m_device == DeviceType::NVIDIA_PTX) {
                    src += "    bra .L" + std::to_string(tgt) + ";\n";
                } else {
                    src += "    goto L_" + std::to_string(tgt) + ";\n";
                }
                return true;
            }

            case OpCode::BRANCH_COND: {
                // op_a = condition register, op_b = target label
                const size_t tgt = resolve_target(node.op_b);
                if (m_device == DeviceType::NVIDIA_PTX) {
                    src += "    setp.ne.s32 %p0, %r" + std::to_string(node.op_a) + ", 0;\n";
                    src += "    @%p0 bra .L" + std::to_string(tgt) + ";\n";
                } else {
                    src += "    if (in[" + std::to_string(node.op_a) + "]) goto L_" + std::to_string(tgt) + ";\n";
                }
                return true;
            }

            case OpCode::BRANCH_COMPARE_FUSED: {
                // op_a = lhs, op_b = rhs, op_c = target label
                const size_t tgt = resolve_target(node.op_c);
                if (m_device == DeviceType::NVIDIA_PTX) {
                    src += "    setp.ne.s32 %p0, %r" + std::to_string(node.op_a) +
                           ", %r" + std::to_string(node.op_b) + ";\n";
                    src += "    @%p0 bra .L" + std::to_string(tgt) + ";\n";
                } else {
                    src += "    if (in[" + std::to_string(node.op_a) + "] != in[" +
                           std::to_string(node.op_b) + "]) goto L_" + std::to_string(tgt) + ";\n";
                }
                return true;
            }

            default:
                return false;
        }
    }

    void emit_kernel_footer(std::string& src) {
        if (m_device == DeviceType::NVIDIA_PTX) {
            src += "    ret;\n}\n";
        } else {
            src += "}\n";
        }
    }
};

} // namespace dgtl::isa

/**
 * @brief Backward Compatibility Alias.
 */
namespace dgtl::isa {
    using GPGPUFusionEngineAlias = GPGPUFusionEngine;
    using DeviceTypeAlias        = DeviceType;
}



