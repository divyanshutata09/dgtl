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

#include "dgtl/ir/Global_IR.hpp"
#include <cstdint>
#include <memory>

namespace dgtl {
namespace api {

class Context;

/**
 * @class IRBuilder
 * @brief Factory for constructing IR nodes and streams.
 * 
 * Provides high-level methods for building IR programs programmatically.
 * Encapsulates low-level IR Atom/Core details and ensures structural validity.
 */
class IRBuilder {
public:
    /**
     * @brief Create an IRBuilder for the given context.
     * @param context The execution context managing memory
     */
    explicit IRBuilder(Context& context) noexcept;

    ~IRBuilder() = default;

    IRBuilder(const IRBuilder&) = delete;
    IRBuilder& operator=(const IRBuilder&) = delete;

    /**
     * @brief Create a new IRStream with pre-allocated capacity.
     * @param capacity Number of IR nodes to pre-allocate
     * @return New IRStream handle
     */
    [[nodiscard]] ir::IRStream create_stream(size_t capacity) noexcept;

    /**
     * @brief Emit a function entry point.
     * @param stream Target stream to emit into
     * @param function_name Name of the function
     * @return ID of the entry point node
     */
    [[nodiscard]] uint32_t create_function(ir::IRStream& stream, 
                                           const char* function_name) noexcept;

    /**
     * @brief Emit a return instruction.
     * @param stream Target stream
     * @param return_value_id The value ID to return (result register)
     * @return ID of the return node
     */
    [[nodiscard]] uint32_t create_return(ir::IRStream& stream, uint32_t return_value_id) noexcept;

    /**
     * @brief Emit an ADD operation (integer or FP based on OpCode).
     * @param stream Target stream
     * @param result_id Destination register
     * @param lhs_id Left operand
     * @param rhs_id Right operand
     * @param opcode OpCode (e.g., OpCode::ADD_I32, OpCode::ADD_F32)
     * @return ID of the created node
     */
    [[nodiscard]] uint32_t create_add(ir::IRStream& stream, uint32_t result_id,
                                      uint32_t lhs_id, uint32_t rhs_id,
                                      ir::OpCode opcode = ir::OpCode::ADD_I32) noexcept;

    /**
     * @brief Emit a MUL operation.
     * @param stream Target stream
     * @param result_id Destination register
     * @param lhs_id Left operand
     * @param rhs_id Right operand
     * @param opcode OpCode (e.g., OpCode::MUL_I32, OpCode::MUL_F32)
     * @return ID of the created node
     */
    [[nodiscard]] uint32_t create_mul(ir::IRStream& stream, uint32_t result_id,
                                      uint32_t lhs_id, uint32_t rhs_id,
                                      ir::OpCode opcode = ir::OpCode::MUL_I32) noexcept;

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
                                      uint32_t a_id, uint32_t b_id, uint32_t c_id) noexcept;

    /**
     * @brief Emit an immediate/constant load.
     * @param stream Target stream
     * @param result_id Destination register
     * @param value The immediate value
     * @return ID of the created node
     */
    [[nodiscard]] uint32_t create_immediate(ir::IRStream& stream, uint32_t result_id,
                                           uint64_t value) noexcept;

    /**
     * @brief Get the current instruction count in the stream.
     * @param stream The stream to query
     * @return Number of instructions
     */
    [[nodiscard]] size_t get_instruction_count(const ir::IRStream& stream) const noexcept;

private:
    Context& context_;
    uint32_t next_node_id_ = 1;
};

} // namespace api
} // namespace dgtl
