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

#include "dgtl/ir/base/IR_Stream.hpp"
#include "dgtl/ir/gen/DGTL_Gen_Ops.hpp"
#include <cstddef>
#include <concepts>
#include <cstdint>
#include <unordered_map>
#include <utility>

namespace dgtl {
namespace api {

class Context;

template <typename Op, typename... Args>
concept DGTLBuildableOp = requires(Args&&... args) {
    { Op::build(std::forward<Args>(args)...) } -> std::same_as<ir::IRNode>;
};

/**
 * @class IRBuilder
 * @brief Factory for constructing IR nodes and streams.
 * * Provides high-level methods for building IR programs programmatically.
 * Encapsulates low-level IR Atom/Core details and ensures structural validity.
 * * [STAFF NOTE]: Builder objects are strictly Thread-Local. Do not share 
 * across threads without external synchronization. 
 */
class alignas(64) IRBuilder final {
public:
    /**
     * @brief Create an IRBuilder for the given context.
     * @param context The execution context managing memory
     */
    explicit IRBuilder(Context& context) noexcept : context_(context) {}

    ~IRBuilder() = default;

    IRBuilder(const IRBuilder&) = delete;
    IRBuilder& operator=(const IRBuilder&) = delete;

    /**
     * @brief Create a new IRStream with pre-allocated capacity.
     * @param capacity Number of IR nodes to pre-allocate
     * @return New IRStream handle
     */
    [[nodiscard]] ir::IRStream create_stream(size_t capacity) noexcept {
        return ir::IRStream::allocate_transactional(capacity == 0 ? 1 : capacity);
    }

    // 1) Insertion point API: set active stream for builder-driven emission.
    void set_insertion_point(ir::IRStream* stream) noexcept {
        // Constant IDs are stream-local. Switching streams must invalidate cache.
        if (active_stream_ != stream) {
            constant_i32_cache_.clear();
        }
        active_stream_ = stream;
    }

    [[nodiscard]] ir::IRStream* insertion_point() const noexcept {
        return active_stream_;
    }

    // 2) Universal op factory for generated/builder ops exposing Op::build(...).
    template <typename Op, typename... Args>
    requires DGTLBuildableOp<Op, Args...>
    [[nodiscard]] ir::IRNode create(Args&&... args) {
        ir::IRNode node = Op::build(std::forward<Args>(args)...);
        if (active_stream_ == nullptr || active_stream_->is_null()) {
            return node;
        }
        node.intent_hash = ir::IRNode::calculate_hash(node.opcode, node.result_id, node.op_a, node.op_b, node.op_c);
        active_stream_->push_back(node);
        return node;
    }

    // 2b) Universal node factory directly from opcode + operands.
    [[nodiscard]] ir::IRNode create_node(ir::OpCode opcode,
                                         uint32_t result_id = ir::INVALID_SLOT,
                                         uint32_t op_a = ir::INVALID_SLOT,
                                         uint32_t op_b = ir::INVALID_SLOT,
                                         uint32_t op_c = ir::INVALID_SLOT,
                                         uint16_t flags = ir::Flags::NONE) {
        ir::IRNode node = ir::gen::make_generated_node(opcode, result_id, op_a, op_b, op_c, flags);
        if (active_stream_ != nullptr && !active_stream_->is_null()) {
            active_stream_->push_back(node);
        }
        return node;
    }

    /**
     * @brief Emit a function entry point.
     * @param stream Target stream to emit into
     * @param function_name Symbolic name of the function
     * @return ID of the created node
     */
    [[nodiscard]] uint32_t create_function_entry(ir::IRStream& stream, const char* function_name) noexcept {
        (void)function_name;
        const uint32_t fn_id = allocate_temp_id();
        stream.push_back(ir::gen::make_generated_node(ir::OpCode::LABEL, fn_id));
        return fn_id;
    }

    /**
     * @brief Emit a general arithmetic/logic operation.
     * @param stream Target stream
     * @param result_id Destination register/slot
     * @param lhs_id Left operand
     * @param rhs_id Right operand
     * @param opcode The operation (e.g., OpCode::ADD_I32, OpCode::MUL_F32)
     * @return ID of the created node
     */
    [[nodiscard]] uint32_t create_mul(ir::IRStream& stream, uint32_t result_id,
                                      uint32_t lhs_id, uint32_t rhs_id,
                                      ir::OpCode opcode = ir::OpCode::MUL_I32) noexcept {
        stream.push_back(ir::gen::make_generated_node(opcode, result_id, lhs_id, rhs_id));
        return result_id;
    }

    /**
     * @brief Emit a Fused Multiply-Add (FMA) operation.
     * @param stream Target stream
     * @param result_id Destination register
     * @param a_id Multiplicand 1
     * @param b_id Multiplicand 2
     * @param c_id Addend
     * @return ID of the created node
     */
    [[nodiscard]] uint32_t create_fma(ir::IRStream& stream, uint32_t result_id,
                                      uint32_t a_id, uint32_t b_id, uint32_t c_id) noexcept {
        stream.push_back(ir::gen::make_generated_node(ir::OpCode::FMA_F32, result_id, a_id, b_id, c_id));
        return result_id;
    }

    /**
     * @brief Emit an immediate/constant load.
     * @param stream Target stream
     * @param result_id Destination register
     * @param value The immediate value
     * @return ID of the created node
     */
    [[nodiscard]] uint32_t create_immediate(ir::IRStream& stream, uint32_t result_id,
                                           uint64_t value) noexcept {
        const uint32_t hi = static_cast<uint32_t>(value >> 32u);
        const uint32_t lo = static_cast<uint32_t>(value & 0xFFFFFFFFu);
        stream.push_back(ir::gen::make_generated_node(ir::OpCode::LOAD_IMM, result_id, hi, lo));
        return result_id;
    }

    // 3) Convenience sugar API
    [[nodiscard]] uint32_t get_constant_i32(int32_t value) {
        if (active_stream_ == nullptr || active_stream_->is_null()) {
            return ir::INVALID_SLOT;
        }

        auto it = constant_i32_cache_.find(value);
        if (it != constant_i32_cache_.end()) {
            return it->second;
        }

        const uint32_t slot = allocate_temp_id();
        (void)create_immediate(*active_stream_, slot, static_cast<uint64_t>(static_cast<int64_t>(value)));
        constant_i32_cache_.insert_or_assign(value, slot);
        return slot;
    }

    void reset_temp_ids(uint32_t next_id = 1) noexcept {
        next_temp_id_ = next_id;
    }

    void clear_constant_cache() noexcept {
        constant_i32_cache_.clear();
    }

    /**
     * @brief Get the current instruction count in the stream.
     * @param stream The stream to query
     * @return Number of nodes
     */
    [[nodiscard]] size_t get_instruction_count(const ir::IRStream& stream) const noexcept {
        return stream.size();
    }

private:
    Context& context_;
    ir::IRStream* active_stream_ = nullptr;
    uint32_t next_temp_id_ = 1;
    std::unordered_map<int32_t, uint32_t> constant_i32_cache_;

    [[nodiscard]] uint32_t allocate_temp_id() noexcept {
        return next_temp_id_++;
    }
};

} // namespace api
} // namespace dgtl