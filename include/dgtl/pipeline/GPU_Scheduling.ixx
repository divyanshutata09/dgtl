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

/* * DGTL (Deep General-purpose Transformation Layer) version 1.0.2 : THE COMPILER SPEC
 * STEP 43: DISTRIBUTED GPU SCHEDULING (MULTI-DEVICE ORCHESTRATION)
 * PILLAR 2: TRANSACTIONAL SCHEDULING (SHADOW-BUFFERED)
 * PILLAR 3: THE SENTINEL (SYNCHRONIZATION CORRECTNESS)
 */

module;

#include <cstdlib>

#include <cstdint>
#include <cstddef>
#include <vector>
#include <unordered_map>
#include <algorithm>

// FIXED: Bind directly to your new core intermediate macro sub-module space
export module dgtl.ir_core.gpu_scheduling;

// Import standalone primitive targets safely
import dgtl.ir_node;
import dgtl.ir_enums;

// FIXED CYCLIC LOOP: Import local core leaf siblings natively via short-form shortcuts

import dgtl.ir_core;



import dgtl.ir_core.pass_manager_config;

export namespace dgtl::pipeline {

namespace detail {

    [[nodiscard]] constexpr dgtl::ir::OpCode gpu_mem_transfer_opcode() noexcept {
        return dgtl::ir::OpCode::GPU_MEM_TRANSFER;
}

} // namespace detail

/**
 * @class DGTLDistributedGPUScheduling
 * @brief Optimizes work distribution across CPU and multiple heterogeneous GPU devices.
 * 
 * ABI PAYLOAD CONTRACT:
 * OpCode::GPU_MEM_TRANSFER operands:
 * - op_a: Pointer / buffer register virtual ID
 * - op_b: Stream slot index (position) of the corresponding LOAD_IMM instruction holding the 64-bit size
 * - op_c: Transfer direction (0 = Host-to-Device/Read, 1 = Device-to-Host/Write)
 * 
 * ALGORITHM:
 * 1. Analysis Stage: Identify GPU-offloadable kernels using actual basic block boundaries. 
 *    Estimate compute vs memory latency using a detailed cost model, and extract read/write pointer registers.
 * 2. Planning Stage: Dynamic load balancing across multiple target devices, considering
 *    NUMA interconnect latency penalties (UPI/QPI hop costs). Plan memory transfers for unique buffer registers.
 * 3. Emission Stage: Pre-allocate stream capacity, sort transfers, and insert LOAD_IMM + GPU_MEM_TRANSFER pairs.
 */
class DGTLDistributedGPUScheduling : public dgtl::pipeline::DGTLOptimizationPass {
private:
    struct TargetDevice {
        dgtl::ir::BackendProfile::TargetISA isa{dgtl::ir::BackendProfile::TargetISA::X64_AVX512};
        uint32_t device_id{0};
        uint32_t numa_node{0};
    };

    struct KernelInfo {
        uint32_t kernel_start_idx{0};
        uint32_t kernel_end_idx{0};  // Exclusive boundary
        uint64_t operation_count{0};
        uint64_t memory_bytes{0};
        double compute_cost{0.0};
        double memory_cost{0.0};
        double compute_intensity{0.0};  // ops per byte
        TargetDevice assigned_device{};
        std::vector<uint32_t> read_buffers;
        std::vector<uint32_t> write_buffers;
    };

    struct MemoryTransfer {
        uint32_t position;
        uint64_t size_bytes;
        uint32_t ptr_reg;
        bool is_write;  // false = host->device, true = device->host
    };

public:
    explicit DGTLDistributedGPUScheduling(uint32_t device_count = 1) noexcept
        : num_devices(device_count) {
        initialize_device_pool();
    }

    void execute(dgtl::ir::IRStream& stream) override {
        if (stream.is_null() || stream.size() < 20) [[unlikely]] return;
        if (num_devices < 2) [[unlikely]] return;  // Single device, skip execution

        // Phase 1: Analysis Stage (Kernels, cost heuristics, and buffer registers)
        std::vector<KernelInfo> kernels = analyze_kernels_and_buffers(stream);
        if (kernels.empty()) {
            return;
        }

        // Phase 2: Planning Stage (Dynamic load balancing and transfer routing)
        assign_to_devices(kernels);
        std::vector<MemoryTransfer> transfers = plan_memory_transfers(kernels);
        if (transfers.empty()) {
            return;
        }

        // Phase 3: Emission / Code Insertion Stage (Shadow emission & capacity pre-growth)
        apply_scheduling(stream, transfers);
    }

    [[nodiscard]] const char* get_pass_name() const noexcept override {
        return "DGTLDistributedGPUScheduling";
    }

    [[nodiscard]] dgtl::pipeline::PassStage get_stage() const noexcept override {
        return dgtl::pipeline::PassStage::PostInlining;
    }

    [[nodiscard]] dgtl::pipeline::PassAffinity hot_path_affinity() const noexcept override {
        return dgtl::pipeline::PassAffinity::Medium;
    }

    [[nodiscard]] dgtl::pipeline::PassAffinity memory_affinity() const noexcept override {
        return dgtl::pipeline::PassAffinity::High;
    }

    [[nodiscard]] dgtl::pipeline::PassAffinity vector_affinity() const noexcept override {
        return dgtl::pipeline::PassAffinity::High;
    }

    [[nodiscard]] dgtl::pipeline::PassAffinity speculation_affinity() const noexcept override {
        return dgtl::pipeline::PassAffinity::Low;
    }

private:
    uint32_t num_devices;
    std::vector<TargetDevice> devices;

    void initialize_device_pool() noexcept {
        devices.clear();
        // Host CPU is always device 0
        devices.push_back(TargetDevice{
            .isa = dgtl::ir::BackendProfile::TargetISA::X64_AVX512,
            .device_id = 0,
            .numa_node = 0
        });

        // Add accelerators/GPUs mapped to physical NUMA nodes
        const uint32_t numa_count = std::max(1u, dgtl::ir::IRStream::numa_node_count());
        for (uint32_t i = 1; i < num_devices; ++i) {
            uint32_t device_numa = (i - 1) % numa_count;
            // Heterogeneous accelerators (alternating CUDA & ROCM ISAs)
            auto isa = (i % 2 == 1)
                ? dgtl::ir::BackendProfile::TargetISA::CUDA_PTX
                : dgtl::ir::BackendProfile::TargetISA::ROCM_LLVMIR;

            devices.push_back(TargetDevice{
                .isa = isa,
                .device_id = i,
                .numa_node = device_numa
            });
        }
    }

    [[nodiscard]] double get_instruction_latency(dgtl::ir::OpCode opcode) const noexcept {
        switch (opcode) {
            case dgtl::ir::OpCode::ADD_I32:
            case dgtl::ir::OpCode::SUB_I32:
            case dgtl::ir::OpCode::ADD_I64:
            case dgtl::ir::OpCode::SUB_I64:
                return 1.0;
            case dgtl::ir::OpCode::MUL_I32:
            case dgtl::ir::OpCode::MUL_I64:
                return 3.0;
            case dgtl::ir::OpCode::DIV_I32:
            case dgtl::ir::OpCode::DIV_I64:
            case dgtl::ir::OpCode::MOD_I32:
            case dgtl::ir::OpCode::MOD_I64:
                return 12.0;
            case dgtl::ir::OpCode::ADD_F32:
            case dgtl::ir::OpCode::SUB_F32:
            case dgtl::ir::OpCode::ADD_F64:
            case dgtl::ir::OpCode::SUB_F64:
                return 3.0;
            case dgtl::ir::OpCode::MUL_F32:
            case dgtl::ir::OpCode::MUL_F64:
                return 4.0;
            case dgtl::ir::OpCode::DIV_F32:
            case dgtl::ir::OpCode::DIV_F64:
                return 15.0;
            case dgtl::ir::OpCode::FMA_F32:
                return 4.0;
            case dgtl::ir::OpCode::LOAD:
            case dgtl::ir::OpCode::STORE:
                return 10.0;
            case dgtl::ir::OpCode::ATOMIC_LOAD:
            case dgtl::ir::OpCode::ATOMIC_STORE:
            case dgtl::ir::OpCode::ATOMIC_FENCE:
                return 40.0;
            case dgtl::ir::OpCode::VEC_LOAD:
            case dgtl::ir::OpCode::VEC_STORE:
                return 15.0;
            default:
                return 1.0;
        }
    }

    [[nodiscard]] bool is_arithmetic_op(dgtl::ir::OpCode opcode) const noexcept {
        return opcode == dgtl::ir::OpCode::ADD_F32 ||
               opcode == dgtl::ir::OpCode::MUL_F32 ||
               opcode == dgtl::ir::OpCode::ADD_I32 ||
               opcode == dgtl::ir::OpCode::MUL_I32 ||
               opcode == dgtl::ir::OpCode::SUB_I32 ||
               opcode == dgtl::ir::OpCode::DIV_I32 ||
               opcode == dgtl::ir::OpCode::MOD_I32 ||
               opcode == dgtl::ir::OpCode::FMA_F32;
    }

    [[nodiscard]] bool is_memory_op(dgtl::ir::OpCode opcode) const noexcept {
        return opcode == dgtl::ir::OpCode::LOAD ||
               opcode == dgtl::ir::OpCode::ATOMIC_LOAD ||
               opcode == dgtl::ir::OpCode::STORE ||
               opcode == dgtl::ir::OpCode::ATOMIC_STORE ||
               opcode == dgtl::ir::OpCode::ATOMIC_FENCE ||
               opcode == dgtl::ir::OpCode::VEC_LOAD ||
               opcode == dgtl::ir::OpCode::VEC_STORE;
    }

    std::vector<KernelInfo> analyze_kernels_and_buffers(const dgtl::ir::IRStream& stream) {
        std::vector<KernelInfo> kernels;
        const uint32_t stream_size = static_cast<uint32_t>(stream.size());

        for (uint32_t i = 0; i < stream_size; ++i) {
            const auto& node = stream.get_node_readonly(i);

            // Detect kernel boundaries
            if (node.opcode == dgtl::ir::OpCode::CALL ||
                (node.flags & dgtl::ir::Flags::IS_SIMD_BURST) != 0) {
                
                uint32_t kernel_end = stream_size;
                uint32_t block_id = stream.get_block_id_for_node(i);
                if (block_id != dgtl::ir::INVALID_SLOT) {
                    const auto& blocks = stream.get_basic_blocks();
                    if (block_id < blocks.size()) {
                        kernel_end = static_cast<uint32_t>(blocks[block_id].end);
                    }
                }

                if (kernel_end > stream_size) {
                    kernel_end = stream_size;
                }

                if (kernel_end <= i) continue;

                uint64_t ops = 0;
                uint64_t mem_bytes = 0;
                double compute_cost = 0.0;
                double memory_cost = 0.0;
                std::vector<uint32_t> read_buffers;
                std::vector<uint32_t> write_buffers;

                for (uint32_t j = i; j < kernel_end; ++j) {
                    const auto& k_node = stream.get_node_readonly(j);
                    double latency = get_instruction_latency(k_node.opcode);

                    if (is_arithmetic_op(k_node.opcode)) {
                        ops++;
                        compute_cost += latency;
                    } else if (is_memory_op(k_node.opcode)) {
                        mem_bytes += 8;
                        memory_cost += latency;

                        uint32_t ptr_reg = k_node.op_a;
                        if (ptr_reg != dgtl::ir::INVALID_SLOT && ptr_reg != 0) {
                            if (k_node.opcode == dgtl::ir::OpCode::STORE ||
                                k_node.opcode == dgtl::ir::OpCode::ATOMIC_STORE ||
                                k_node.opcode == dgtl::ir::OpCode::VEC_STORE) {
                                if (std::find(write_buffers.begin(), write_buffers.end(), ptr_reg) == write_buffers.end()) {
                                    write_buffers.push_back(ptr_reg);
                                }
                            } else {
                                if (std::find(read_buffers.begin(), read_buffers.end(), ptr_reg) == read_buffers.end()) {
                                    read_buffers.push_back(ptr_reg);
                                }
                            }
                        }
                    }
                }

                if (mem_bytes > 0) {
                    kernels.push_back(KernelInfo{
                        .kernel_start_idx = i,
                        .kernel_end_idx = kernel_end,
                        .operation_count = ops,
                        .memory_bytes = mem_bytes,
                        .compute_cost = compute_cost,
                        .memory_cost = memory_cost,
                        .compute_intensity = static_cast<double>(ops) / mem_bytes,
                        .assigned_device = TargetDevice{
                            .isa = dgtl::ir::BackendProfile::TargetISA::X64_AVX512,
                            .device_id = 0,
                            .numa_node = 0
                        },
                        .read_buffers = std::move(read_buffers),
                        .write_buffers = std::move(write_buffers)
                    });
                }
                
                i = kernel_end - 1;
            }
        }

        return kernels;
    }

    void assign_to_devices(std::vector<KernelInfo>& kernels) {
        if (devices.empty()) return;
        
        const double GPU_INTENSITY_THRESHOLD = 0.5;
        const double OFF_LOAD_LATENCY_THRESHOLD = 15.0;

        std::vector<double> device_loads(devices.size(), 0.0);
        const uint32_t current_cpu_node = dgtl::ir::IRStream::current_numa_node();

        for (auto& kernel : kernels) {
            bool can_offload = (kernel.compute_intensity >= GPU_INTENSITY_THRESHOLD) &&
                               (kernel.compute_cost >= OFF_LOAD_LATENCY_THRESHOLD) &&
                               (devices.size() > 1);

            if (can_offload) {
                uint32_t best_dev_idx = 1;
                double best_dev_cost = -1.0;

                for (uint32_t d = 1; d < static_cast<uint32_t>(devices.size()); ++d) {
                    const auto& dev = devices[d];
                    // UPI interconnect penalty: 1.5x interconnect overhead if on different NUMA node
                    double numa_overhead = (dev.numa_node == current_cpu_node) ? 1.0 : 1.5;
                    double transfer_cost = kernel.memory_bytes * 0.05 * numa_overhead;
                    double total_metric = device_loads[d] + transfer_cost;

                    if (best_dev_cost < 0.0 || total_metric < best_dev_cost) {
                        best_dev_idx = d;
                        best_dev_cost = total_metric;
                    }
                }

                kernel.assigned_device = devices[best_dev_idx];
                device_loads[best_dev_idx] += kernel.compute_cost;
            } else {
                kernel.assigned_device = devices[0];
                device_loads[0] += kernel.compute_cost;
            }
        }
    }

    std::vector<MemoryTransfer> plan_memory_transfers(const std::vector<KernelInfo>& kernels) {
        std::vector<MemoryTransfer> transfers;

        for (const auto& kernel : kernels) {
            if (kernel.assigned_device.device_id != 0) {
                uint64_t total_bufs = kernel.read_buffers.size() + kernel.write_buffers.size();
                uint64_t transfer_size_per_buffer = kernel.memory_bytes / std::max<uint64_t>(1u, total_bufs);

                std::transform(kernel.read_buffers.begin(), kernel.read_buffers.end(), std::back_inserter(transfers),
                    [kernel, transfer_size_per_buffer](uint32_t read_ptr) noexcept {
                        return MemoryTransfer{
                            .position = kernel.kernel_start_idx,
                            .size_bytes = transfer_size_per_buffer,
                            .ptr_reg = read_ptr,
                            .is_write = false
                        };
                    });

                std::transform(kernel.write_buffers.begin(), kernel.write_buffers.end(), std::back_inserter(transfers),
                    [kernel, transfer_size_per_buffer](uint32_t write_ptr) noexcept {
                        return MemoryTransfer{
                            .position = kernel.kernel_end_idx,
                            .size_bytes = transfer_size_per_buffer,
                            .ptr_reg = write_ptr,
                            .is_write = true
                        };
                    });
            }
        }

        std::sort(transfers.begin(), transfers.end(), [](const MemoryTransfer& a, const MemoryTransfer& b) {
            if (a.position != b.position) return a.position < b.position;
            if (a.is_write != b.is_write) return a.is_write < b.is_write;
            return a.ptr_reg < b.ptr_reg;
        });

        return transfers;
    }

    static void copy_sidecars(const dgtl::ir::IRStream& src, dgtl::ir::IRStream& dst) {
        dst.set_frontend_language(src.get_frontend_language());
        dst.set_default_type_context(src.get_default_type_context());
        
        const size_t src_size = src.size();
        for (size_t i = 0; i < src_size; ++i) {
            auto call_args = src.get_call_overflow_args(i);
            if (!call_args.empty()) dst.set_call_overflow_args(i, call_args);
            
            auto abi_words = src.get_call_abi_layout_words(i);
            if (!abi_words.empty()) dst.set_call_abi_layout_words(i, abi_words);
            
            auto op_overflow = src.get_operand_overflow(i);
            if (!op_overflow.empty()) dst.set_operand_overflow(i, op_overflow);
            
            auto res_ids = src.get_result_ids(i);
            if (!res_ids.empty()) dst.set_result_ids(i, res_ids);
            
            uint32_t effects = src.get_node_effects(i);
            if (effects != 0) dst.set_node_effects(i, effects);
        }
        
        uint32_t max_id = src.get_max_id();
        for (uint32_t id = 1; id <= max_id; ++id) {
            uint32_t type = src.get_result_type(id);
            if (type != 0) dst.set_result_type(id, type);
        }
    }

    bool apply_scheduling(dgtl::ir::IRStream& stream,
                          const std::vector<MemoryTransfer>& transfers) {
        
        const size_t extra_nodes = transfers.size() * 2;
        const size_t required_capacity = stream.size() + extra_nodes;

        if (required_capacity > stream.capacity()) {
            size_t new_capacity = required_capacity + 64;
            dgtl::ir::IRStream larger_stream(nullptr, new_capacity);
            larger_stream.copy_from(stream);
            copy_sidecars(stream, larger_stream);
            stream = std::move(larger_stream);
        }

        // RAII transaction rollback guard
        dgtl::ir::IRTransactionGuard guard(stream);

        uint32_t shadow_cursor = 0;
        size_t transfer_idx = 0;
        bool modified = false;
        
        uint32_t next_reg_id = stream.get_max_id() + 1;
        const size_t stream_size = stream.size();

        std::unordered_map<size_t, std::vector<uint32_t>> new_call_overflow_args;
        std::unordered_map<size_t, std::vector<uint32_t>> new_call_abi_layout_words;
        std::unordered_map<size_t, std::vector<uint32_t>> new_operand_overflow;
        std::unordered_map<size_t, std::vector<uint32_t>> new_result_ids;
        std::unordered_map<size_t, uint32_t> new_node_effects;

        for (uint32_t i = 0; i < stream_size; ++i) {
            while (transfer_idx < transfers.size() &&
                   i == transfers[transfer_idx].position) {
                
                const auto& transfer = transfers[transfer_idx];
                
                if (shadow_cursor + 2 > stream.capacity()) {
                    return false;
                }

                // 1. Emit LOAD_IMM
                auto& limm_atom = stream.get_shadow_atom(shadow_cursor);
                auto& limm_core = stream.get_shadow_core(shadow_cursor);
                
                uint32_t size_reg = next_reg_id++;
                limm_atom.opcode = dgtl::ir::OpCode::LOAD_IMM;
                limm_atom.flags = dgtl::ir::Flags::IS_CONST;
                limm_core.result_id = size_reg;
                limm_core.op_a = static_cast<uint32_t>(transfer.size_bytes >> 32);
                limm_core.op_b = static_cast<uint32_t>(transfer.size_bytes & 0xFFFFFFFFu);
                limm_core.op_c = 0;
                limm_core.intent_hash = dgtl::ir::IRNode::calculate_hash(
                    limm_atom.opcode, limm_core.result_id, limm_core.op_a, limm_core.op_b, limm_core.op_c
                );

                uint32_t size_slot = shadow_cursor;
                shadow_cursor++;

                // 2. Emit GPU_MEM_TRANSFER
                auto& t_atom = stream.get_shadow_atom(shadow_cursor);
                auto& t_core = stream.get_shadow_core(shadow_cursor);

                t_atom.opcode = detail::gpu_mem_transfer_opcode();
                t_atom.flags = static_cast<decltype(t_atom.flags)>(
                    dgtl::ir::Flags::IS_SIMD_BURST
                );
                t_core.result_id = dgtl::ir::INVALID_SLOT;
                
                t_core.op_a = transfer.ptr_reg;
                t_core.op_b = size_slot;
                t_core.op_c = transfer.is_write ? 1u : 0u;
                t_core.intent_hash = dgtl::ir::IRNode::calculate_hash(
                    t_atom.opcode, t_core.result_id, t_core.op_a, t_core.op_b, t_core.op_c
                );

                shadow_cursor++;
                transfer_idx++;
                modified = true;
            }

            if (shadow_cursor >= stream.capacity()) {
                return false;
            }

            const auto& node = stream.get_node_readonly(i);
            auto& s_atom = stream.get_shadow_atom(shadow_cursor);
            auto& s_core = stream.get_shadow_core(shadow_cursor);

            s_atom.opcode = node.opcode;
            s_atom.flags = node.flags | static_cast<decltype(s_atom.flags)>(
                dgtl::ir::Flags::IS_OPTIMIZED
            );
            s_core.result_id = node.result_id;
            s_core.op_a = node.op_a;
            s_core.op_b = node.op_b;
            s_core.op_c = node.op_c;
            s_core.intent_hash = dgtl::ir::IRNode::calculate_hash(
                s_atom.opcode, s_core.result_id, s_core.op_a, s_core.op_b, s_core.op_c
            );

            // Remap sidecars to avoid read-after-write hazards
            auto call_args = stream.get_call_overflow_args(i);
            if (!call_args.empty()) new_call_overflow_args[shadow_cursor] = std::move(call_args);
            
            auto abi_words = stream.get_call_abi_layout_words(i);
            if (!abi_words.empty()) new_call_abi_layout_words[shadow_cursor] = std::move(abi_words);
            
            auto op_overflow = stream.get_operand_overflow(i);
            if (!op_overflow.empty()) new_operand_overflow[shadow_cursor] = std::move(op_overflow);
            
            auto res_ids = stream.get_result_ids(i);
            if (!res_ids.empty()) new_result_ids[shadow_cursor] = std::move(res_ids);
            
            uint32_t effects = stream.get_node_effects(i);
            if (effects != 0) new_node_effects[shadow_cursor] = effects;

            shadow_cursor++;
        }

        stream.set_shadow_count(shadow_cursor);

        if (modified && stream.validate_shadow_integrity()) {
            guard.commit();
            
            // Clean up and copy new sidecar mappings
            for (uint32_t i = 0; i < shadow_cursor; ++i) {
                stream.clear_call_overflow_args(i);
                stream.clear_call_abi_layout_words(i);
                stream.clear_operand_overflow(i);
                stream.clear_result_ids(i);
                stream.clear_node_effects(i);
            }
            
            for (auto& [idx, args] : new_call_overflow_args) {
                stream.set_call_overflow_args(idx, args);
            }
            for (auto& [idx, words] : new_call_abi_layout_words) {
                stream.set_call_abi_layout_words(idx, words);
            }
            for (auto& [idx, op_overflow] : new_operand_overflow) {
                stream.set_operand_overflow(idx, op_overflow);
            }
            for (auto& [idx, rids] : new_result_ids) {
                stream.set_result_ids(idx, rids);
            }
            for (auto& [idx, effects] : new_node_effects) {
                stream.set_node_effects(idx, effects);
            }
            
            return true;
        }

        return false;
    }
};

} // namespace dgtl::pipeline
