/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

export module dgtl.internal.api_ir_builder;

// Import native compiler base interfaces and consolidated parent modules
import dgtl.ir_core;

// FIXED: Point directly to the unified consolidated pass infrastructure to resolve traits/gen bindings
import dgtl.ir_core;
import dgtl.op_traits;

import dgtl.compiler_context;
import dgtl.numa_runtime;
import dgtl.dgtl_bulletins;
import dgtl.hashing;

export namespace dgtl {
namespace api {

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
    // Public ABI-class alias so clients can stay on IRBuilder facade only.
    using ABIValueClass = dgtl::core::DGTLABIValueClass;
    using GeneratedTrait = ir::gen::GenTrait;
    using GeneratedInterface = ir::gen::GenInterface;
    using GeneratedAttribute = ir::gen::GenAttribute;

    [[nodiscard]] static ABIValueClass classify_abi_value_class(std::string_view raw_type_name) noexcept {
        const std::string ty(raw_type_name);
        if (ty.empty() || ty == "void") return ABIValueClass::none;

        if (ty == "i64" || ty == "u64" || ty == "i128" || ty == "u128" ||
            ty == "i256" || ty == "u256" || ty == "i512" || ty == "u512" ||
            ty == "int" || ty == "usize" || ty == "isize") {
            return ABIValueClass::i64;
        }
        if (ty == "i32" || ty == "u32" || ty == "i16" || ty == "u16" ||
            ty == "i8" || ty == "u8" || ty == "i4" || ty == "u4" ||
            ty == "bool" || ty == "char") {
            return ABIValueClass::i32;
        }
        if (ty == "f64" || ty == "f128") return ABIValueClass::f64;
        if (ty == "f32" || ty == "f16" || ty == "bf16" || ty == "f8") return ABIValueClass::f32;
        if (ty == "string" || ty == "ptr" || ty == "any") return ABIValueClass::pointer;
        return ABIValueClass::i64;
    }

    // Client-facing typed operation set that avoids exposing IR opcodes.
    enum class BinaryI32Op : uint8_t {
        Add,
        Sub,
        Mul,
        Div,
        Mod,
        And,
        Or,
        Xor,
        Shl,
        Shr
    };

    enum class CompareI32Op : uint8_t {
        Eq,
        Ne,
        Lt,
        Le,
        Gt,
        Ge
    };

    enum class RuntimeIntrinsic : uint8_t {
        StringLength,
        StringCompare,
        StringConcat,
        StringSubstring,
        ReadTsc,
        PrefetchT0,
        Clflush,
        SpinAcquire,
        RingPushFree,
        WasmInvoke,
        FiberYieldLoop,
        SuspendCtx,
        ResumeCtx,
        NeonVaddQ,
        MatmulBf16,
        MatmulFp8,
        SdpaF16,
        SoftmaxFast,
        LayerNorm,
        RopeEmbed,
        FastInvSqrt,
        VecAddF32512,
        VecMulF32512,
        StoreV512
    };

    enum class EmitStatus : uint8_t {
        Ok = 0,
        MissingInsertionPoint,
        UnknownOperation,
        OperandArityMismatch
    };

    struct EmitOutcome {
        EmitStatus status = EmitStatus::Ok;
        uint32_t result_id = ir::INVALID_SLOT;
        size_t node_index = static_cast<size_t>(-1);

        [[nodiscard]] bool ok() const noexcept { return status == EmitStatus::Ok; }
    };

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
        pre_emit_numa_();
        return ir::IRStream::allocate_transactional(capacity == 0 ? 1 : capacity);
    }

    void set_numa_locality_enabled(bool enabled) noexcept {
        numa_locality_enabled_ = enabled;
    }

    [[nodiscard]] bool numa_locality_enabled() const noexcept {
        return numa_locality_enabled_;
    }

    void set_preferred_numa_node(uint32_t node) noexcept {
        preferred_numa_node_ = node;
        if (numa_locality_enabled_) {
            (void)dgtl::runtime::numa::pin_current_thread_to_node(preferred_numa_node_);
        }
    }

    [[nodiscard]] uint32_t get_preferred_numa_node() const noexcept {
        return preferred_numa_node_;
    }

    void apply_numa_worker_affinity(uint32_t worker_index) noexcept {
        if (!numa_locality_enabled_) {
            return;
        }
        const uint32_t node = dgtl::runtime::numa::recommend_worker_node(worker_index);
        preferred_numa_node_ = node;
        (void)dgtl::runtime::numa::pin_current_thread_to_node(node);
        (void)dgtl::runtime::numa::pin_thread_to_core(
            dgtl::runtime::numa::recommend_worker_core(worker_index));
    }

    // 1) Insertion point API: set active stream for builder-driven emission.
    void set_insertion_point(ir::IRStream* stream) noexcept {
        // Constant IDs are stream-local. Switching streams must invalidate cache.
        if (active_stream_ != stream) {
            constant_i32_cache_.clear();
            string_constant_slot_cache_.clear();
        }
        active_stream_ = stream;
    }

    [[nodiscard]] ir::IRStream* insertion_point() const noexcept {
        return active_stream_;
    }

    [[nodiscard]] bool has_insertion_point() const noexcept {
        return active_stream_ != nullptr && !active_stream_->is_null();
    }

    [[nodiscard]] uint32_t emit_i32(BinaryI32Op op,
                                    uint32_t lhs,
                                    uint32_t rhs,
                                    uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_binary_op_i32_(map_i32_binary_op_(op), lhs, rhs, result_id);
    }

    [[nodiscard]] uint32_t emit_compare_i32(CompareI32Op op,
                                            uint32_t lhs,
                                            uint32_t rhs,
                                            uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_binary_op_i32_(map_i32_compare_op_(op), lhs, rhs, result_id);
    }

    [[nodiscard]] uint32_t emit_runtime(RuntimeIntrinsic intrinsic,
                                        std::span<const uint32_t> args,
                                        uint32_t result_id = ir::INVALID_SLOT,
                                        uint16_t flags = ir::Flags::NONE) noexcept {
        if (args.size() > 3) {
            return ir::INVALID_SLOT;
        }
        const uint32_t a = args.size() > 0 ? args[0] : ir::INVALID_SLOT;
        const uint32_t b = args.size() > 1 ? args[1] : ir::INVALID_SLOT;
        const uint32_t c = args.size() > 2 ? args[2] : ir::INVALID_SLOT;
        return emit_runtime_intrinsic3(map_runtime_intrinsic_(intrinsic), a, b, c, result_id, flags);
    }

    // Universal client-facing emission path keyed by generated operation name
    // (from .dgtl_def metadata), without exposing internal opcode enums.
    [[nodiscard]] EmitOutcome emit_generated(std::string_view op_name,
                                             std::span<const uint32_t> operands = {},
                                             uint32_t result_id = ir::INVALID_SLOT,
                                             uint16_t flags = ir::Flags::NONE,
                                             std::span<const uint32_t> extra_result_ids = {},
                                             uint32_t effect_set = 0) noexcept {
        if (!has_insertion_point()) {
            return EmitOutcome{EmitStatus::MissingInsertionPoint, ir::INVALID_SLOT, static_cast<size_t>(-1)};
        }
        pre_emit_numa_();

        const ir::gen::OpDefMetadata* meta = lookup_generated_op_def_(op_name);
        if (meta == nullptr) {
            return EmitOutcome{EmitStatus::UnknownOperation, ir::INVALID_SLOT, static_cast<size_t>(-1)};
        }

        if (operands.size() < meta->operand_count) {
            return EmitOutcome{EmitStatus::OperandArityMismatch, ir::INVALID_SLOT, static_cast<size_t>(-1)};
        }

        const uint32_t rid = meta->has_result
                     ? ((result_id == ir::INVALID_SLOT) ? allocate_temp_id() : result_id)
                     : result_id;

        const uint32_t a = operands.size() > 0 ? operands[0] : ir::INVALID_SLOT;
        const uint32_t b = operands.size() > 1 ? operands[1] : ir::INVALID_SLOT;
        const uint32_t c = operands.size() > 2 ? operands[2] : ir::INVALID_SLOT;

        const size_t node_idx = active_stream_->size();
        ir::IRNode node = ir::gen::make_generated_node(meta->opcode, rid, a, b, c, flags);
        if (operands.size() > 3) {
            node.logic_flags = operands[3];
            node.refresh_intent_hash();
        }
        active_stream_->push_back(node);
        annotate_result_type_(*active_stream_, meta->opcode, rid);
        try_annotate_debug_and_type_(*active_stream_, node_idx);

        if (operands.size() > 4) {
            const std::span<const uint32_t> overflow(operands.data() + 4, operands.size() - 4);
            if (meta->opcode == ir::OpCode::CALL || meta->opcode == ir::OpCode::CALL_VARIADIC) {
                active_stream_->set_call_overflow_args(node_idx, overflow);
            }
            active_stream_->set_operand_overflow(node_idx, overflow);
        }

        if (!extra_result_ids.empty()) {
            active_stream_->set_result_ids(node_idx, extra_result_ids);
        }
        if (effect_set != 0) {
            active_stream_->set_node_effects(node_idx, effect_set);
        }

        return EmitOutcome{EmitStatus::Ok, rid, node_idx};
    }

    [[nodiscard]] bool supports_generated_op(std::string_view op_name) const noexcept {
        return lookup_generated_op_def_(op_name) != nullptr;
    }

    [[nodiscard]] bool supports_generated_op(ir::OpCode opcode) const noexcept {
        return lookup_generated_op_def_(opcode) != nullptr;
    }

    [[nodiscard]] uint32_t generated_op_arity(std::string_view op_name) const noexcept {
        const ir::gen::OpDefMetadata* meta = lookup_generated_op_def_(op_name);
        return meta == nullptr ? 0u : meta->operand_count;
    }

    [[nodiscard]] uint32_t generated_op_arity(ir::OpCode opcode) const noexcept {
        const ir::gen::OpDefMetadata* meta = lookup_generated_op_def_(opcode);
        return meta == nullptr ? 0u : meta->operand_count;
    }

    [[nodiscard]] bool generated_op_has_result(std::string_view op_name) const noexcept {
        const ir::gen::OpDefMetadata* meta = lookup_generated_op_def_(op_name);
        return meta != nullptr && meta->has_result;
    }

    [[nodiscard]] bool generated_op_has_result(ir::OpCode opcode) const noexcept {
        const ir::gen::OpDefMetadata* meta = lookup_generated_op_def_(opcode);
        return meta != nullptr && meta->has_result;
    }

    [[nodiscard]] std::string_view generated_op_result_type(std::string_view op_name) const noexcept {
        const ir::gen::OpDefMetadata* meta = lookup_generated_op_def_(op_name);
        return meta == nullptr ? std::string_view{} : meta->result_type;
    }

    [[nodiscard]] std::string_view generated_op_result_type(ir::OpCode opcode) const noexcept {
        const ir::gen::OpDefMetadata* meta = lookup_generated_op_def_(opcode);
        return meta == nullptr ? std::string_view{} : meta->result_type;
    }

    [[nodiscard]] std::string_view generated_op_dialect(std::string_view op_name) const noexcept {
        const ir::gen::OpDefMetadata* meta = lookup_generated_op_def_(op_name);
        return meta == nullptr ? std::string_view{} : meta->dialect;
    }

    [[nodiscard]] std::string_view generated_op_dialect(ir::OpCode opcode) const noexcept {
        const ir::gen::OpDefMetadata* meta = lookup_generated_op_def_(opcode);
        return meta == nullptr ? std::string_view{} : meta->dialect;
    }

    [[nodiscard]] std::optional<ir::OpCode> generated_op_opcode(std::string_view op_name) const noexcept {
        const ir::gen::OpDefMetadata* meta = lookup_generated_op_def_(op_name);
        if (meta == nullptr) {
            return std::nullopt;
        }
        return meta->opcode;
    }

    [[nodiscard]] std::string_view generated_op_name(ir::OpCode opcode) const noexcept {
        const ir::gen::OpDefMetadata* meta = lookup_generated_op_def_(opcode);
        return meta == nullptr ? std::string_view{} : meta->name;
    }

    [[nodiscard]] uint64_t generated_op_trait_bits(std::string_view op_name) const noexcept {
        const ir::gen::OpDefMetadata* meta = lookup_generated_op_def_(op_name);
        return meta == nullptr ? 0ULL : meta->trait_bits;
    }

    [[nodiscard]] uint64_t generated_op_interface_bits(std::string_view op_name) const noexcept {
        const ir::gen::OpDefMetadata* meta = lookup_generated_op_def_(op_name);
        return meta == nullptr ? 0ULL : meta->interface_bits;
    }

    [[nodiscard]] uint64_t generated_op_attribute_bits(std::string_view op_name) const noexcept {
        const ir::gen::OpDefMetadata* meta = lookup_generated_op_def_(op_name);
        return meta == nullptr ? 0ULL : meta->attribute_bits;
    }

    [[nodiscard]] bool generated_op_has_trait(std::string_view op_name, GeneratedTrait trait) const noexcept {
        const uint64_t bits = generated_op_trait_bits(op_name);
        return (bits & static_cast<uint64_t>(trait)) != 0ULL;
    }

    [[nodiscard]] bool generated_op_has_interface(std::string_view op_name, GeneratedInterface iface) const noexcept {
        const uint64_t bits = generated_op_interface_bits(op_name);
        return (bits & static_cast<uint64_t>(iface)) != 0ULL;
    }

    [[nodiscard]] bool generated_op_has_attribute(std::string_view op_name, GeneratedAttribute attr) const noexcept {
        const uint64_t bits = generated_op_attribute_bits(op_name);
        return (bits & static_cast<uint64_t>(attr)) != 0ULL;
    }

    [[nodiscard]] EmitOutcome emit_generated(ir::OpCode opcode,
                                             std::span<const uint32_t> operands = {},
                                             uint32_t result_id = ir::INVALID_SLOT,
                                             uint16_t flags = ir::Flags::NONE,
                                             std::span<const uint32_t> extra_result_ids = {},
                                             uint32_t effect_set = 0) noexcept {
        const ir::gen::OpDefMetadata* meta = lookup_generated_op_def_(opcode);
        if (meta == nullptr) {
            return EmitOutcome{EmitStatus::UnknownOperation, ir::INVALID_SLOT, static_cast<size_t>(-1)};
        }
        return emit_generated(meta->name, operands, result_id, flags, extra_result_ids, effect_set);
    }

    // 2) Universal op factory for generated/builder ops exposing Op::build(...).
    template <typename Op, typename... Args>
    requires DGTLBuildableOp<Op, Args...>
    [[nodiscard]] ir::IRNode create(Args&&... args) {
        pre_emit_numa_();
        ir::IRNode node = Op::build(std::forward<Args>(args)...);
        if (active_stream_ == nullptr || active_stream_->is_null()) {
            return node;
        }
        node.intent_hash = ir::IRNode::calculate_hash(node.opcode, node.result_id, node.op_a, node.op_b, node.op_c);
        active_stream_->push_back(node);
        annotate_result_type_(*active_stream_, node.opcode, node.result_id);
        try_annotate_debug_and_type_(*active_stream_, active_stream_->size() - 1);
        return node;
    }

    // 2b) Universal node factory directly from opcode + operands.
    [[nodiscard]] ir::IRNode create_node(ir::OpCode opcode,
                                         uint32_t result_id = ir::INVALID_SLOT,
                                         uint32_t op_a = ir::INVALID_SLOT,
                                         uint32_t op_b = ir::INVALID_SLOT,
                                         uint32_t op_c = ir::INVALID_SLOT,
                                         uint16_t flags = ir::Flags::NONE) {
        pre_emit_numa_();
        ir::IRNode node = ir::gen::make_generated_node(opcode, result_id, op_a, op_b, op_c, flags);
        if (active_stream_ != nullptr && !active_stream_->is_null()) {
            active_stream_->push_back(node);
            annotate_result_type_(*active_stream_, opcode, result_id);
            try_annotate_debug_and_type_(*active_stream_, active_stream_->size() - 1);
        }
        return node;
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
        try_annotate_debug_and_type_(stream, stream.size() - 1);
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
        try_annotate_debug_and_type_(stream, stream.size() - 1);
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
        annotate_result_type_(stream, ir::OpCode::LOAD_IMM, result_id);
        try_annotate_debug_and_type_(stream, stream.size() - 1);
        return result_id;
    }

    [[nodiscard]] uint32_t create_string_const(ir::IRStream& stream,
                                               uint32_t result_id,
                                               uint32_t string_handle) noexcept {
        stream.push_back(ir::gen::make_generated_node(ir::OpCode::STRING_CONST, result_id, string_handle));
        annotate_result_type_(stream, ir::OpCode::STRING_CONST, result_id);
        try_annotate_debug_and_type_(stream, stream.size() - 1);
        return result_id;
    }

    [[nodiscard]] uint32_t create_string_const(ir::IRStream& stream,
                                               uint32_t result_id,
                                               std::string_view literal) {
        return create_string_const(stream, result_id, intern_string_handle(literal));
    }

    [[nodiscard]] uint32_t create_string_length(ir::IRStream& stream,
                                                uint32_t result_id,
                                                uint32_t string_id) noexcept {
        stream.push_back(ir::gen::make_generated_node(ir::OpCode::STRING_LENGTH, result_id, string_id));
        annotate_result_type_(stream, ir::OpCode::STRING_LENGTH, result_id);
        try_annotate_debug_and_type_(stream, stream.size() - 1);
        return result_id;
    }

    [[nodiscard]] uint32_t create_string_compare(ir::IRStream& stream,
                                                 uint32_t result_id,
                                                 uint32_t lhs_string_id,
                                                 uint32_t rhs_string_id) noexcept {
        stream.push_back(ir::gen::make_generated_node(ir::OpCode::STRING_COMPARE, result_id, lhs_string_id, rhs_string_id));
        annotate_result_type_(stream, ir::OpCode::STRING_COMPARE, result_id);
        try_annotate_debug_and_type_(stream, stream.size() - 1);
        return result_id;
    }

    [[nodiscard]] uint32_t create_string_concat(ir::IRStream& stream,
                                                uint32_t result_id,
                                                uint32_t lhs_string_id,
                                                uint32_t rhs_string_id) noexcept {
        stream.push_back(ir::gen::make_generated_node(ir::OpCode::STRING_CONCAT, result_id, lhs_string_id, rhs_string_id));
        annotate_result_type_(stream, ir::OpCode::STRING_CONCAT, result_id);
        try_annotate_debug_and_type_(stream, stream.size() - 1);
        return result_id;
    }

    [[nodiscard]] uint32_t create_string_substring(ir::IRStream& stream,
                                                   uint32_t result_id,
                                                   uint32_t string_id,
                                                   uint32_t start_id,
                                                   uint32_t length_id) noexcept {
        stream.push_back(ir::gen::make_generated_node(ir::OpCode::STRING_SUBSTRING,
                                                      result_id,
                                                      string_id,
                                                      start_id,
                                                      length_id));
        annotate_result_type_(stream, ir::OpCode::STRING_SUBSTRING, result_id);
        try_annotate_debug_and_type_(stream, stream.size() - 1);
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

    [[nodiscard]] uint32_t get_string_handle(std::string_view literal) {
        return intern_string_handle(literal);
    }

    [[nodiscard]] uint32_t get_string_constant(std::string_view literal) {
        if (active_stream_ == nullptr || active_stream_->is_null()) {
            return ir::INVALID_SLOT;
        }

        const uint32_t handle = intern_string_handle(literal);
        auto it = string_constant_slot_cache_.find(handle);
        if (it != string_constant_slot_cache_.end()) {
            return it->second;
        }

        const uint32_t slot = allocate_temp_id();
        (void)create_string_const(*active_stream_, slot, handle);
        string_constant_slot_cache_.insert_or_assign(handle, slot);
        return slot;
    }

    [[nodiscard]] uint32_t create_string_const(uint32_t string_handle) {
        if (active_stream_ == nullptr || active_stream_->is_null()) {
            return ir::INVALID_SLOT;
        }

        const uint32_t slot = allocate_temp_id();
        (void)create_string_const(*active_stream_, slot, string_handle);
        return slot;
    }

    [[nodiscard]] uint32_t create_string_const(std::string_view literal) {
        if (active_stream_ == nullptr || active_stream_->is_null()) {
            return ir::INVALID_SLOT;
        }

        const uint32_t slot = allocate_temp_id();
        (void)create_string_const(*active_stream_, slot, literal);
        return slot;
    }

    [[nodiscard]] uint32_t create_string_length(uint32_t string_id) {
        if (active_stream_ == nullptr || active_stream_->is_null()) {
            return ir::INVALID_SLOT;
        }

        const uint32_t slot = allocate_temp_id();
        (void)create_string_length(*active_stream_, slot, string_id);
        return slot;
    }

    [[nodiscard]] uint32_t create_string_compare(uint32_t lhs_string_id,
                                                 uint32_t rhs_string_id) {
        if (active_stream_ == nullptr || active_stream_->is_null()) {
            return ir::INVALID_SLOT;
        }

        const uint32_t slot = allocate_temp_id();
        (void)create_string_compare(*active_stream_, slot, lhs_string_id, rhs_string_id);
        return slot;
    }

    [[nodiscard]] uint32_t create_string_concat(uint32_t lhs_string_id,
                                                uint32_t rhs_string_id) {
        if (active_stream_ == nullptr || active_stream_->is_null()) {
            return ir::INVALID_SLOT;
        }

        const uint32_t slot = allocate_temp_id();
        (void)create_string_concat(*active_stream_, slot, lhs_string_id, rhs_string_id);
        return slot;
    }

    [[nodiscard]] uint32_t create_string_substring(uint32_t string_id,
                                                   uint32_t start_id,
                                                   uint32_t length_id) {
        if (active_stream_ == nullptr || active_stream_->is_null()) {
            return ir::INVALID_SLOT;
        }

        const uint32_t slot = allocate_temp_id();
        (void)create_string_substring(*active_stream_, slot, string_id, start_id, length_id);
        return slot;
    }

    void reset_temp_ids(uint32_t next_id = 1) noexcept {
        next_temp_id_ = next_id;
    }

    void clear_constant_cache() noexcept {
        constant_i32_cache_.clear();
        string_constant_slot_cache_.clear();
    }

    /**
     * @brief Get the current instruction count in the stream.
     * @param stream The stream to query
     * @return Number of nodes
     */
    [[nodiscard]] static size_t get_instruction_count(const ir::IRStream& stream) noexcept {
        return stream.size();
    }

    /**
     * @brief Slot ID allocation proxy.
     * Frontend SDK consumers use this to obtain unique SSA value IDs that
     * are stable and non-overlapping with the builder's internal counter.
     */
    [[nodiscard]] uint32_t allocate_id() noexcept { return allocate_temp_id(); }

    // =========================================================================
    // FRONTEND SDK FACADE
    // =========================================================================
    // Named-method API for SDK consumers (Myzon, DSLs, SQL compilers, etc.).
    // These methods operate on the active insertion point set via
    // set_insertion_point().  They return uint32_t value slot IDs and accept
    // only primitive types â€” no internal IR types are exposed.
    //
    // Rule: SDK consumers MUST use only these methods.  create_node() and
    // direct OpCode references are reserved for DGTL-internal use.
    // =========================================================================

    // ---- Control Flow -------------------------------------------------------

    /**
     * @brief Emit a function entry point.
     * @param stream Target stream to emit into
     * @param function_name Symbolic name of the function
     * @return ID of the created node
     */
    [[nodiscard]] uint32_t create_function_entry(ir::IRStream& stream, const char* function_name) noexcept {
        const uint32_t fn_id = allocate_temp_id();
        
        // Infer canonical visibility flag bits for the driver entry point
        uint16_t automatic_flags = ir::Flags::NONE;
        if (function_name != nullptr && std::string_view(function_name) == "DGTL_kernel_entry") {
            automatic_flags |= (ir::Flags::HAS_SIDE_EFFECTS | ir::Flags::IS_VOLATILE);
        }

        stream.push_back(ir::gen::make_generated_node(ir::OpCode::LABEL, fn_id, fn_id,
                                                      ir::INVALID_SLOT, ir::INVALID_SLOT,
                                                      automatic_flags));
        return fn_id;
    }

    /// Emit a function-entry label.  @p fn_slot is the unique slot ID for this
    /// function (obtain one via allocate_id() or a stable hash of the name).
    void emit_function_label(uint32_t fn_slot) noexcept {
        if (active_stream_ == nullptr || active_stream_->is_null()) return;

        // Facade Boundary Optimization: Force every global function label to act
        // as a volatile side-effect barrier. This completely locks the label into
        // the text segment and prevents the backend from stripping it out.
        const uint16_t automatic_flags = ir::Flags::HAS_SIDE_EFFECTS | ir::Flags::IS_VOLATILE;

        active_stream_->push_back(
            ir::gen::make_generated_node(ir::OpCode::LABEL, fn_slot, fn_slot,
                                         ir::INVALID_SLOT, ir::INVALID_SLOT,
                                         automatic_flags));
        try_annotate_debug_and_type_(*active_stream_, active_stream_->size() - 1);
    }

    /// Emit a basic-block label with the given @p label_id.
    void emit_label(uint32_t label_id) noexcept {
        if (active_stream_ == nullptr || active_stream_->is_null()) return;
        active_stream_->push_back(
            ir::gen::make_generated_node(ir::OpCode::LABEL,
                                         static_cast<uint32_t>(label_id)));
        try_annotate_debug_and_type_(*active_stream_, active_stream_->size() - 1);
    }

    /// Emit an unconditional jump to @p target_label_id.
    void emit_goto(uint32_t target_label_id) noexcept {
        if (active_stream_ == nullptr || active_stream_->is_null()) return;
        active_stream_->push_back(
            ir::gen::make_generated_node(ir::OpCode::GOTO,
                                         allocate_temp_id(),
                                         target_label_id,
                                         ir::INVALID_SLOT,
                                         ir::INVALID_SLOT,
                                         ir::Flags::HAS_SIDE_EFFECTS));
        try_annotate_debug_and_type_(*active_stream_, active_stream_->size() - 1);
    }

    /// Emit a conditional branch.  Jumps to @p then_label_id when @p cond_id
    /// is non-zero, falls through otherwise (else_label_id = INVALID_SLOT
    /// means fall-through).
    void emit_branch_cond(uint32_t cond_id,
                          uint32_t then_label_id,
                          uint32_t else_label_id = ir::INVALID_SLOT) noexcept {
        if (active_stream_ == nullptr || active_stream_->is_null()) return;
        active_stream_->push_back(
            ir::gen::make_generated_node(ir::OpCode::BRANCH_COND,
                                         allocate_temp_id(),
                                         cond_id,
                                         then_label_id,
                                         else_label_id,
                                         ir::Flags::HAS_SIDE_EFFECTS));
        try_annotate_debug_and_type_(*active_stream_, active_stream_->size() - 1);
    }

    /// Emit a function call.  @p callee_slot is the slot ID of the callee
    /// (produced by a prior emit_function_label or a stable name hash).
    /// Up to three inline argument IDs are packed; overflow args are attached
    /// via the stream's overflow-args sidecar.  Returns the result slot ID.
    [[nodiscard]] uint32_t emit_call(uint32_t callee_slot,
                                     std::span<const uint32_t> args,
                                     uint32_t result_id = ir::INVALID_SLOT) noexcept {
        if (!has_insertion_point()) {
            return ir::INVALID_SLOT;
        }
        const uint32_t rid = (result_id == ir::INVALID_SLOT)
                                 ? allocate_temp_id()
                                 : result_id;
        const uint32_t arg0 = args.size() > 0 ? args[0] : ir::INVALID_SLOT;
        const uint32_t arg1 = args.size() > 1 ? args[1] : ir::INVALID_SLOT;
        const uint32_t arg2 = args.size() > 2 ? args[2] : ir::INVALID_SLOT;
        const uint16_t call_flags =
            static_cast<uint16_t>(ir::Flags::IS_VOLATILE | ir::Flags::HAS_SIDE_EFFECTS);
        const size_t node_idx = active_stream_->size();
        ir::IRNode call_node = ir::gen::make_generated_node(ir::OpCode::CALL,
                                                            rid,
                                                            callee_slot,
                                                            arg0,
                                                            arg1,
                                                            call_flags);
        call_node.logic_flags = arg2;
        call_node.refresh_intent_hash();
        active_stream_->push_back(call_node);
        annotate_result_type_(*active_stream_, ir::OpCode::CALL, rid);
        try_annotate_debug_and_type_(*active_stream_, node_idx);
        // Attach overflow args beyond the first three packed args.
        if (args.size() > 3 && active_stream_->size() > node_idx) {
            active_stream_->set_call_overflow_args(
                node_idx,
                std::span<const uint32_t>(args.data() + 3, args.size() - 3));
        }
        return rid;
    }

    [[nodiscard]] uint32_t emit_call_variadic(uint32_t callee_slot,
                                              std::span<const uint32_t> args,
                                              uint32_t result_id = ir::INVALID_SLOT) noexcept {
        if (!has_insertion_point()) {
            return ir::INVALID_SLOT;
        }
        const uint32_t rid = (result_id == ir::INVALID_SLOT)
                                 ? allocate_temp_id()
                                 : result_id;
        const uint32_t arg0 = args.size() > 0 ? args[0] : ir::INVALID_SLOT;
        const uint32_t arg1 = args.size() > 1 ? args[1] : ir::INVALID_SLOT;
        const uint32_t arg2 = args.size() > 2 ? args[2] : ir::INVALID_SLOT;
        const uint16_t call_flags =
            static_cast<uint16_t>(ir::Flags::IS_VOLATILE | ir::Flags::HAS_SIDE_EFFECTS);
        const size_t node_idx = active_stream_->size();
        ir::IRNode call_node = ir::gen::make_generated_node(ir::OpCode::CALL_VARIADIC,
                                                            rid,
                                                            callee_slot,
                                                            arg0,
                                                            arg1,
                                                            call_flags);
        call_node.logic_flags = arg2;
        call_node.refresh_intent_hash();
        active_stream_->push_back(call_node);
        annotate_result_type_(*active_stream_, ir::OpCode::CALL_VARIADIC, rid);
        try_annotate_debug_and_type_(*active_stream_, node_idx);
        if (args.size() > 3 && active_stream_->size() > node_idx) {
            active_stream_->set_call_overflow_args(
                node_idx,
                std::span<const uint32_t>(args.data() + 3, args.size() - 3));
        }
        return rid;
    }
    
    /// Emit a return, yielding @p value_id as the function's return value.
    void emit_ret(uint32_t value_id) noexcept {
        if (active_stream_ == nullptr || active_stream_->is_null()) return;
        active_stream_->push_back(
            ir::gen::make_generated_node(ir::OpCode::RET,
                                         allocate_temp_id(),
                                         value_id,
                                         0u,
                                         0u,
                                         ir::Flags::IS_RETURN));
        try_annotate_debug_and_type_(*active_stream_, active_stream_->size() - 1);
    }

    // ---- Constants ----------------------------------------------------------

    /// Emit a 64-bit integer constant.  Returns the allocated result slot ID.
    [[nodiscard]] uint32_t emit_constant_i64(int64_t value,
                                             uint32_t result_id = ir::INVALID_SLOT) noexcept {
        if (active_stream_ == nullptr || active_stream_->is_null()) {
            return ir::INVALID_SLOT;
        }
        const uint32_t rid = (result_id == ir::INVALID_SLOT)
                                 ? allocate_temp_id()
                                 : result_id;
        (void)create_immediate(*active_stream_, rid, static_cast<uint64_t>(value));
        return rid;
    }

    // ---- Integer 64-bit Arithmetic ------------------------------------------

    [[nodiscard]] uint32_t emit_add_i64(uint32_t lhs, uint32_t rhs,
                                        uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_binary_op_i64_(ir::OpCode::ADD_I64, lhs, rhs, result_id);
    }
    [[nodiscard]] uint32_t emit_sub_i64(uint32_t lhs, uint32_t rhs,
                                        uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_binary_op_i64_(ir::OpCode::SUB_I64, lhs, rhs, result_id);
    }
    [[nodiscard]] uint32_t emit_mul_i64(uint32_t lhs, uint32_t rhs,
                                        uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_binary_op_i64_(ir::OpCode::MUL_I64, lhs, rhs, result_id);
    }
    [[nodiscard]] uint32_t emit_div_i64(uint32_t lhs, uint32_t rhs,
                                        uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_binary_op_i64_(ir::OpCode::DIV_I64, lhs, rhs, result_id);
    }
    [[nodiscard]] uint32_t emit_mod_i64(uint32_t lhs, uint32_t rhs,
                                        uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_binary_op_i64_(ir::OpCode::MOD_I64, lhs, rhs, result_id);
    }

    // ---- Integer 32-bit Bitwise ---------------------------------------------

    [[nodiscard]] uint32_t emit_and_i32(uint32_t lhs, uint32_t rhs,
                                        uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_binary_op_i32_(ir::OpCode::AND_I32, lhs, rhs, result_id);
    }
    [[nodiscard]] uint32_t emit_or_i32(uint32_t lhs, uint32_t rhs,
                                       uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_binary_op_i32_(ir::OpCode::OR_I32, lhs, rhs, result_id);
    }
    [[nodiscard]] uint32_t emit_xor_i32(uint32_t lhs, uint32_t rhs,
                                        uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_binary_op_i32_(ir::OpCode::XOR_I32, lhs, rhs, result_id);
    }
    [[nodiscard]] uint32_t emit_shl_i32(uint32_t lhs, uint32_t rhs,
                                        uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_binary_op_i32_(ir::OpCode::SHL_I32, lhs, rhs, result_id);
    }
    [[nodiscard]] uint32_t emit_shr_i32(uint32_t lhs, uint32_t rhs,
                                        uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_binary_op_i32_(ir::OpCode::SHR_I32, lhs, rhs, result_id);
    }

    // ---- Integer 32-bit Arithmetic -----------------------------------------

    [[nodiscard]] uint32_t emit_add_i32(uint32_t lhs, uint32_t rhs,
                                        uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_binary_op_i32_(ir::OpCode::ADD_I32, lhs, rhs, result_id);
    }
    [[nodiscard]] uint32_t emit_sub_i32(uint32_t lhs, uint32_t rhs,
                                        uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_binary_op_i32_(ir::OpCode::SUB_I32, lhs, rhs, result_id);
    }
    [[nodiscard]] uint32_t emit_mul_i32(uint32_t lhs, uint32_t rhs,
                                        uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_binary_op_i32_(ir::OpCode::MUL_I32, lhs, rhs, result_id);
    }
    [[nodiscard]] uint32_t emit_div_i32(uint32_t lhs, uint32_t rhs,
                                        uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_binary_op_i32_(ir::OpCode::DIV_I32, lhs, rhs, result_id);
    }
    [[nodiscard]] uint32_t emit_mod_i32(uint32_t lhs, uint32_t rhs,
                                        uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_binary_op_i32_(ir::OpCode::MOD_I32, lhs, rhs, result_id);
    }

    // ---- Integer 64-bit Bitwise --------------------------------------------

    [[nodiscard]] uint32_t emit_and_i64(uint32_t lhs, uint32_t rhs,
                                        uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_binary_op_i64_(ir::OpCode::AND_I64, lhs, rhs, result_id);
    }
    [[nodiscard]] uint32_t emit_or_i64(uint32_t lhs, uint32_t rhs,
                                       uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_binary_op_i64_(ir::OpCode::OR_I64, lhs, rhs, result_id);
    }
    [[nodiscard]] uint32_t emit_xor_i64(uint32_t lhs, uint32_t rhs,
                                        uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_binary_op_i64_(ir::OpCode::XOR_I64, lhs, rhs, result_id);
    }
    [[nodiscard]] uint32_t emit_shl_i64(uint32_t lhs, uint32_t rhs,
                                        uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_binary_op_i64_(ir::OpCode::SHL_I64, lhs, rhs, result_id);
    }
    [[nodiscard]] uint32_t emit_shr_i64(uint32_t lhs, uint32_t rhs,
                                        uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_binary_op_i64_(ir::OpCode::SHR_I64, lhs, rhs, result_id);
    }

    // ---- Floating Point -----------------------------------------------------

    [[nodiscard]] uint32_t emit_add_f32(uint32_t lhs, uint32_t rhs,
                                        uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_binary_op_i32_(ir::OpCode::ADD_F32, lhs, rhs, result_id);
    }
    [[nodiscard]] uint32_t emit_sub_f32(uint32_t lhs, uint32_t rhs,
                                        uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_binary_op_i32_(ir::OpCode::SUB_F32, lhs, rhs, result_id);
    }
    [[nodiscard]] uint32_t emit_mul_f32(uint32_t lhs, uint32_t rhs,
                                        uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_binary_op_i32_(ir::OpCode::MUL_F32, lhs, rhs, result_id);
    }
    [[nodiscard]] uint32_t emit_div_f32(uint32_t lhs, uint32_t rhs,
                                        uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_binary_op_i32_(ir::OpCode::DIV_F32, lhs, rhs, result_id);
    }
    [[nodiscard]] uint32_t emit_add_f64(uint32_t lhs, uint32_t rhs,
                                        uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_binary_op_i64_(ir::OpCode::ADD_F64, lhs, rhs, result_id);
    }
    [[nodiscard]] uint32_t emit_sub_f64(uint32_t lhs, uint32_t rhs,
                                        uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_binary_op_i64_(ir::OpCode::SUB_F64, lhs, rhs, result_id);
    }
    [[nodiscard]] uint32_t emit_mul_f64(uint32_t lhs, uint32_t rhs,
                                        uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_binary_op_i64_(ir::OpCode::MUL_F64, lhs, rhs, result_id);
    }
    [[nodiscard]] uint32_t emit_div_f64(uint32_t lhs, uint32_t rhs,
                                        uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_binary_op_i64_(ir::OpCode::DIV_F64, lhs, rhs, result_id);
    }
    [[nodiscard]] uint32_t emit_fma_f32(uint32_t a,
                                        uint32_t b,
                                        uint32_t c,
                                        uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_ternary_op_(ir::OpCode::FMA_F32, a, b, c, result_id);
    }

    [[nodiscard]] uint32_t emit_vec_add_f32_512(uint32_t lhs, uint32_t rhs,
                                                uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_binary_op_i32_(ir::OpCode::VEC_ADD_F32_512, lhs, rhs, result_id);
    }

    [[nodiscard]] uint32_t emit_vec_mul_f32_512(uint32_t lhs, uint32_t rhs,
                                                uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_binary_op_i32_(ir::OpCode::VEC_MUL_F32_512, lhs, rhs, result_id);
    }

    void emit_store_v512(uint32_t addr_id, uint32_t val_id,
                         uint32_t node_id = ir::INVALID_SLOT) noexcept {
        if (active_stream_ == nullptr || active_stream_->is_null()) return;
        const uint32_t nid = (node_id == ir::INVALID_SLOT) ? allocate_temp_id() : node_id;
        active_stream_->push_back(
            ir::gen::make_generated_node(ir::OpCode::STORE_V512, nid, addr_id, val_id));
        try_annotate_debug_and_type_(*active_stream_, active_stream_->size() - 1);
    }

    // ---- Memory -------------------------------------------------------------

    /// Emit a load from @p addr_id.  Returns the result slot ID.
    [[nodiscard]] uint32_t emit_load(uint32_t addr_id,
                                     uint32_t result_id = ir::INVALID_SLOT) noexcept {
        if (active_stream_ == nullptr || active_stream_->is_null()) {
            return ir::INVALID_SLOT;
        }
        const uint32_t rid = (result_id == ir::INVALID_SLOT)
                                 ? allocate_temp_id()
                                 : result_id;
        active_stream_->push_back(
            ir::gen::make_generated_node(ir::OpCode::LOAD, rid, addr_id));
        annotate_result_type_(*active_stream_, ir::OpCode::LOAD, rid);
        try_annotate_debug_and_type_(*active_stream_, active_stream_->size() - 1);
        return rid;
    }

    /// Emit a store of @p val_id to @p addr_id.
    void emit_store(uint32_t addr_id, uint32_t val_id,
                    uint32_t node_id = ir::INVALID_SLOT) noexcept {
        if (active_stream_ == nullptr || active_stream_->is_null()) return;
        const uint32_t nid = (node_id == ir::INVALID_SLOT)
                                 ? allocate_temp_id()
                                 : node_id;
        active_stream_->push_back(
            ir::gen::make_generated_node(ir::OpCode::STORE, nid, addr_id, val_id));
        try_annotate_debug_and_type_(*active_stream_, active_stream_->size() - 1);
    }

    [[nodiscard]] uint32_t emit_atomic_load(uint32_t addr_id,
                                            uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_unary_op_(ir::OpCode::ATOMIC_LOAD, addr_id, result_id);
    }

    void emit_atomic_store(uint32_t addr_id,
                           uint32_t value_id,
                           uint32_t node_id = ir::INVALID_SLOT) noexcept {
        if (!has_insertion_point()) return;
        const uint32_t nid = (node_id == ir::INVALID_SLOT) ? allocate_temp_id() : node_id;
        active_stream_->push_back(ir::gen::make_generated_node(ir::OpCode::ATOMIC_STORE,
                                                                nid,
                                                                addr_id,
                                                                value_id,
                                                                ir::INVALID_SLOT,
                                                                static_cast<uint16_t>(ir::Flags::HAS_SIDE_EFFECTS | ir::Flags::IS_VOLATILE)));
        try_annotate_debug_and_type_(*active_stream_, active_stream_->size() - 1);
    }

    void emit_atomic_fence() noexcept {
        emit_side_effect_op0_(ir::OpCode::ATOMIC_FENCE);
    }

    void emit_acquire_fence() noexcept {
        emit_side_effect_op0_(ir::OpCode::ACQUIRE_FENCE);
    }

    void emit_release_fence() noexcept {
        emit_side_effect_op0_(ir::OpCode::RELEASE_FENCE);
    }

    void emit_memory_fence() noexcept {
        emit_side_effect_op0_(ir::OpCode::MEMORY_FENCE);
    }

    // ---- Comparisons (i32) --------------------------------------------------

    [[nodiscard]] uint32_t emit_compare_eq_i32(uint32_t lhs, uint32_t rhs,
                                               uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_binary_op_i32_(ir::OpCode::CMP_EQ_I32, lhs, rhs, result_id);
    }
    [[nodiscard]] uint32_t emit_compare_ne_i32(uint32_t lhs, uint32_t rhs,
                                               uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_binary_op_i32_(ir::OpCode::CMP_NE_I32, lhs, rhs, result_id);
    }
    [[nodiscard]] uint32_t emit_compare_lt_i32(uint32_t lhs, uint32_t rhs,
                                               uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_binary_op_i32_(ir::OpCode::CMP_LT_I32, lhs, rhs, result_id);
    }
    [[nodiscard]] uint32_t emit_compare_le_i32(uint32_t lhs, uint32_t rhs,
                                               uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_binary_op_i32_(ir::OpCode::CMP_LE_I32, lhs, rhs, result_id);
    }
    [[nodiscard]] uint32_t emit_compare_gt_i32(uint32_t lhs, uint32_t rhs,
                                               uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_binary_op_i32_(ir::OpCode::CMP_GT_I32, lhs, rhs, result_id);
    }
    [[nodiscard]] uint32_t emit_compare_ge_i32(uint32_t lhs, uint32_t rhs,
                                               uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_binary_op_i32_(ir::OpCode::CMP_GE_I32, lhs, rhs, result_id);
    }

    // ---- New IR surface synced with .dgtl_def/.dgtl_isel -------------------

    [[nodiscard]] uint32_t emit_select(uint32_t cond_id,
                                       uint32_t true_id,
                                       uint32_t false_id,
                                       uint32_t result_id = ir::INVALID_SLOT) noexcept {
        if (active_stream_ == nullptr || active_stream_->is_null()) {
            return ir::INVALID_SLOT;
        }
        const uint32_t rid = (result_id == ir::INVALID_SLOT) ? allocate_temp_id() : result_id;
        active_stream_->push_back(ir::gen::make_generated_node(ir::OpCode::SELECT, rid, cond_id, true_id, false_id));
        try_annotate_debug_and_type_(*active_stream_, active_stream_->size() - 1);
        return rid;
    }

    [[nodiscard]] uint32_t emit_gep(uint32_t base_ptr_id,
                                    uint32_t offset_id,
                                    uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_binary_op_i64_(ir::OpCode::GEP, base_ptr_id, offset_id, result_id);
    }

    [[nodiscard]] uint32_t emit_ptr_to_int(uint32_t ptr_id,
                                           uint32_t result_id = ir::INVALID_SLOT) noexcept {
        if (active_stream_ == nullptr || active_stream_->is_null()) {
            return ir::INVALID_SLOT;
        }
        const uint32_t rid = (result_id == ir::INVALID_SLOT) ? allocate_temp_id() : result_id;
        active_stream_->push_back(ir::gen::make_generated_node(ir::OpCode::PTR_TO_INT, rid, ptr_id));
        try_annotate_debug_and_type_(*active_stream_, active_stream_->size() - 1);
        return rid;
    }

    [[nodiscard]] uint32_t emit_ptr_cast(uint32_t ptr_id,
                                         uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_unary_op_(ir::OpCode::PTR_CAST, ptr_id, result_id);
    }

    [[nodiscard]] uint32_t emit_int_to_ptr(uint32_t value_id,
                                           uint32_t result_id = ir::INVALID_SLOT) noexcept {
        if (active_stream_ == nullptr || active_stream_->is_null()) {
            return ir::INVALID_SLOT;
        }
        const uint32_t rid = (result_id == ir::INVALID_SLOT) ? allocate_temp_id() : result_id;
        active_stream_->push_back(ir::gen::make_generated_node(ir::OpCode::INT_TO_PTR, rid, value_id));
        try_annotate_debug_and_type_(*active_stream_, active_stream_->size() - 1);
        return rid;
    }

    [[nodiscard]] uint32_t emit_bool_select(uint32_t cond_id,
                                            uint32_t true_id,
                                            uint32_t false_id,
                                            uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_ternary_op_(ir::OpCode::BOOL_SELECT, cond_id, true_id, false_id, result_id);
    }

    [[nodiscard]] uint32_t emit_runtime_intrinsic3(ir::OpCode opcode,
                                                   uint32_t a,
                                                   uint32_t b,
                                                   uint32_t c,
                                                   uint32_t result_id = ir::INVALID_SLOT,
                                                   uint16_t flags = ir::Flags::NONE) noexcept {
        if (!has_insertion_point()) {
            return ir::INVALID_SLOT;
        }
        const uint32_t rid = (result_id == ir::INVALID_SLOT) ? allocate_temp_id() : result_id;
        active_stream_->push_back(ir::gen::make_generated_node(opcode, rid, a, b, c, flags));
        annotate_result_type_(*active_stream_, opcode, rid);
        try_annotate_debug_and_type_(*active_stream_, active_stream_->size() - 1);
        return rid;
    }

    [[nodiscard]] uint32_t emit_runtime_intrinsic2(ir::OpCode opcode,
                                                   uint32_t a,
                                                   uint32_t b,
                                                   uint32_t result_id = ir::INVALID_SLOT,
                                                   uint16_t flags = ir::Flags::NONE) noexcept {
        return emit_runtime_intrinsic3(opcode, a, b, ir::INVALID_SLOT, result_id, flags);
    }

    [[nodiscard]] uint32_t emit_runtime_intrinsic1(ir::OpCode opcode,
                                                   uint32_t a,
                                                   uint32_t result_id = ir::INVALID_SLOT,
                                                   uint16_t flags = ir::Flags::NONE) noexcept {
        return emit_runtime_intrinsic3(opcode, a, ir::INVALID_SLOT, ir::INVALID_SLOT, result_id, flags);
    }

    [[nodiscard]] uint32_t emit_runtime_intrinsic0(ir::OpCode opcode,
                                                   uint32_t result_id = ir::INVALID_SLOT,
                                                   uint16_t flags = ir::Flags::NONE) noexcept {
        return emit_runtime_intrinsic3(opcode, ir::INVALID_SLOT, ir::INVALID_SLOT, ir::INVALID_SLOT, result_id, flags);
    }

    [[nodiscard]] uint32_t emit_read_tsc(uint32_t result_id = ir::INVALID_SLOT) noexcept {
        return emit_runtime_intrinsic0(ir::OpCode::READ_TSC, result_id);
    }

    void emit_prefetch_t0(uint32_t addr_id) noexcept {
        (void)emit_runtime_intrinsic1(ir::OpCode::PREFETCH_T0,
                                      addr_id,
                                      allocate_temp_id(),
                                      static_cast<uint16_t>(ir::Flags::HAS_SIDE_EFFECTS));
    }

    void emit_clflush(uint32_t addr_id) noexcept {
        (void)emit_runtime_intrinsic1(ir::OpCode::CLFLUSH,
                                      addr_id,
                                      allocate_temp_id(),
                                      static_cast<uint16_t>(ir::Flags::HAS_SIDE_EFFECTS | ir::Flags::IS_VOLATILE));
    }

    void emit_branch_likely(uint32_t cond_id,
                            uint32_t then_label_id,
                            uint32_t else_label_id = ir::INVALID_SLOT) noexcept {
        if (!has_insertion_point()) return;
        active_stream_->push_back(ir::gen::make_generated_node(ir::OpCode::BRANCH_LIKELY,
                                                                allocate_temp_id(),
                                                                cond_id,
                                                                then_label_id,
                                                                else_label_id,
                                                                static_cast<uint16_t>(ir::Flags::HAS_SIDE_EFFECTS | ir::Flags::IS_BRANCH)));
        try_annotate_debug_and_type_(*active_stream_, active_stream_->size() - 1);
    }

    void emit_branch_unlikely(uint32_t cond_id,
                              uint32_t then_label_id,
                              uint32_t else_label_id = ir::INVALID_SLOT) noexcept {
        if (!has_insertion_point()) return;
        active_stream_->push_back(ir::gen::make_generated_node(ir::OpCode::BRANCH_UNLIKELY,
                                                                allocate_temp_id(),
                                                                cond_id,
                                                                then_label_id,
                                                                else_label_id,
                                                                static_cast<uint16_t>(ir::Flags::HAS_SIDE_EFFECTS | ir::Flags::IS_BRANCH)));
        try_annotate_debug_and_type_(*active_stream_, active_stream_->size() - 1);
    }

    // ---- Debug & Type Metadata Annotations ----
    void set_debug_store(ir::debug::DGTLDebugStore* store) noexcept {
        active_debug_store_ = store;
    }

    [[nodiscard]] ir::debug::DGTLDebugStore* debug_store() const noexcept {
        return active_debug_store_;
    }

    void set_current_debug_location(uint32_t line, uint16_t column, uint16_t file_id) noexcept {
        current_line_ = line;
        current_column_ = column;
        current_file_id_ = file_id;
        has_debug_location_ = true;
    }

    void clear_current_debug_location() noexcept {
        has_debug_location_ = false;
    }

    void set_current_type_info(uint16_t type_tag, uint16_t ownership_flags, uint16_t nullability_flags, uint16_t aux_flags = 0) noexcept {
        current_type_tag_ = type_tag;
        current_ownership_flags_ = ownership_flags;
        current_nullability_flags_ = nullability_flags;
        current_aux_flags_ = aux_flags;
        has_type_info_ = true;
    }

    void clear_current_type_info() noexcept {
        has_type_info_ = false;
    }

    // ---- Linear Ownership APIs ----
    [[nodiscard]] uint32_t emit_ownership_acquire(uint32_t ptr_slot,
                                                  uint32_t result_id = ir::INVALID_SLOT) noexcept {
        const uint32_t rid = emit_unary_op_(ir::OpCode::OWNERSHIP_ACQUIRE, ptr_slot, result_id);
        if (has_insertion_point() && rid != ir::INVALID_SLOT) {
            active_stream_->track_ownership(ptr_slot, ir::OwnershipModel::LINEAR, rid);
        }
        return rid;
    }

    [[nodiscard]] uint32_t emit_ownership_release(uint32_t ptr_slot,
                                                  uint32_t result_id = ir::INVALID_SLOT) noexcept {
        const uint32_t rid = emit_unary_op_(ir::OpCode::OWNERSHIP_RELEASE, ptr_slot, result_id);
        if (has_insertion_point() && rid != ir::INVALID_SLOT) {
            active_stream_->release_ownership(ptr_slot, rid);
        }
        return rid;
    }

    [[nodiscard]] uint32_t emit_ownership_transfer(uint32_t ptr_slot,
                                                   uint32_t recipient_slot,
                                                   uint32_t result_id = ir::INVALID_SLOT) noexcept {
        const uint32_t rid = emit_binary_op_i64_(ir::OpCode::OWNERSHIP_TRANSFER, ptr_slot, recipient_slot, result_id);
        if (has_insertion_point() && rid != ir::INVALID_SLOT) {
            active_stream_->record_ownership_transfer(ptr_slot, rid);
            active_stream_->track_ownership(recipient_slot, ir::OwnershipModel::LINEAR, rid);
        }
        return rid;
    }

    // ---- Concurrency & Async APIs ----
    [[nodiscard]] uint32_t emit_async_launch(uint32_t task_func_slot,
                                             uint32_t arg_slot,
                                             uint32_t result_id = ir::INVALID_SLOT) noexcept {
        const uint32_t rid = emit_binary_op_i64_(ir::OpCode::ASYNC_LAUNCH, task_func_slot, arg_slot, result_id);
        if (has_insertion_point() && rid != ir::INVALID_SLOT) {
            active_stream_->mark_concurrent(rid, ir::ConcurrencyClass::ASYNC);
        }
        return rid;
    }

    [[nodiscard]] uint32_t emit_async_spawn(uint32_t task_func_slot,
                                            uint32_t arg_slot,
                                            uint32_t result_id = ir::INVALID_SLOT) noexcept {
        const uint32_t rid = emit_binary_op_i64_(ir::OpCode::ASYNC_SPAWN, task_func_slot, arg_slot, result_id);
        if (has_insertion_point() && rid != ir::INVALID_SLOT) {
            active_stream_->mark_concurrent(rid, ir::ConcurrencyClass::SPAWN);
        }
        return rid;
    }

    [[nodiscard]] uint32_t emit_async_await(uint32_t task_handle_slot,
                                            uint32_t result_id = ir::INVALID_SLOT) noexcept {
        const uint32_t rid = emit_unary_op_(ir::OpCode::ASYNC_AWAIT, task_handle_slot, result_id);
        if (has_insertion_point() && rid != ir::INVALID_SLOT) {
            active_stream_->mark_concurrent(rid, ir::ConcurrencyClass::AWAIT_READY);
        }
        return rid;
    }

    [[nodiscard]] uint32_t emit_chan_send(uint32_t chan_slot,
                                          uint32_t val_slot,
                                          uint32_t result_id = ir::INVALID_SLOT) noexcept {
        const uint32_t rid = emit_binary_op_i64_(ir::OpCode::CHAN_SEND, chan_slot, val_slot, result_id);
        if (has_insertion_point() && rid != ir::INVALID_SLOT) {
            active_stream_->mark_concurrent(rid, ir::ConcurrencyClass::SEND_OP);
        }
        return rid;
    }

    [[nodiscard]] uint32_t emit_chan_recv(uint32_t chan_slot,
                                          uint32_t result_id = ir::INVALID_SLOT) noexcept {
        const uint32_t rid = emit_unary_op_(ir::OpCode::CHAN_RECV, chan_slot, result_id);
        if (has_insertion_point() && rid != ir::INVALID_SLOT) {
            active_stream_->mark_concurrent(rid, ir::ConcurrencyClass::RECV_OP);
        }
        return rid;
    }

    [[nodiscard]] uint32_t emit_barrier_sync(uint32_t barrier_slot,
                                             uint32_t result_id = ir::INVALID_SLOT) noexcept {
        const uint32_t rid = emit_unary_op_(ir::OpCode::BARRIER_SYNC, barrier_slot, result_id);
        if (has_insertion_point() && rid != ir::INVALID_SLOT) {
            active_stream_->mark_concurrent(rid, ir::ConcurrencyClass::BARRIER);
        }
        return rid;
    }

    void emit_execution_boundary(uint32_t mask_register_id) noexcept {
        if (active_stream_ == nullptr || active_stream_->is_null()) return;
        active_stream_->push_back(
            ir::gen::make_generated_node(ir::OpCode::BARRIER_SYNC,
                                         allocate_temp_id(),
                                         mask_register_id,
                                         ir::INVALID_SLOT,
                                         ir::INVALID_SLOT,
                                         static_cast<uint16_t>(ir::Flags::HAS_SIDE_EFFECTS)));
    }

    // ---- On-the-Fly SMT Verification API ----
    [[nodiscard]] static dgtl::formal::VerificationResult verify_with_baseline(
        const ir::IRStream& original,
        const ir::IRStream& optimized,
        std::string_view pass_name,
        std::string_view solver = "z3"
    ) noexcept {
        dgtl::formal::DGTLFormalVerificationSMT verifier;
        const std::string script = verifier.export_to_smtlib2(original, optimized, pass_name);
        return verifier.verify_with_external_solver(script, solver);
    }

private:
    Context& context_;
    ir::IRStream* active_stream_ = nullptr;
    uint32_t next_temp_id_ = 1;
    bool numa_locality_enabled_ = true;
    uint32_t preferred_numa_node_ = 0;
    std::unordered_map<int32_t, uint32_t> constant_i32_cache_;
    std::unordered_map<std::string, uint32_t> string_handle_cache_;
    std::unordered_map<uint32_t, uint32_t> string_constant_slot_cache_;

    // Active Debug Store and metadata status
    ir::debug::DGTLDebugStore* active_debug_store_ = nullptr;
    uint32_t current_line_ = 0;
    uint16_t current_column_ = 0;
    uint16_t current_file_id_ = 0;
    bool has_debug_location_ = false;

    uint16_t current_type_tag_ = 0;
    uint16_t current_ownership_flags_ = 0;
    uint16_t current_nullability_flags_ = 0;
    uint16_t current_aux_flags_ = 0;
    bool has_type_info_ = false;

    void try_annotate_debug_and_type_(ir::IRStream& stream, size_t node_idx) noexcept {
        if (&stream == active_stream_ && active_debug_store_) {
            bool is_shadow = stream.is_active_transaction();
            if (has_debug_location_) {
                active_debug_store_->record_loc(node_idx, current_line_, current_column_, current_file_id_, is_shadow);
            }
            if (has_type_info_) {
                active_debug_store_->record_type(node_idx, current_type_tag_, current_ownership_flags_, current_nullability_flags_, current_aux_flags_, is_shadow);
            }
        }
    }

    static void annotate_result_type_(ir::IRStream& stream,
                                      ir::OpCode opcode,
                                      uint32_t result_id) noexcept {
        if (result_id == 0u || result_id == ir::INVALID_SLOT) {
            return;
        }
        const auto expected = ir::interfaces::expected_result_type_code(opcode);
        if (!expected.has_value() || *expected == ir::TypeCode::VOID_T) {
            stream.clear_result_type(result_id);
            return;
        }
        stream.set_result_type(result_id, *expected);
    }

    void pre_emit_numa_() const noexcept {
        if (!numa_locality_enabled_) {
            return;
        }
        if (dgtl::runtime::numa::get_num_nodes() <= 1u) {
            return;
        }
        (void)dgtl::runtime::numa::pin_current_thread_to_node(preferred_numa_node_);
    }

    // Private helpers backing the named-method facade ---
    [[nodiscard]] uint32_t emit_binary_op_i64_(ir::OpCode op,
                                               uint32_t lhs, uint32_t rhs,
                                               uint32_t result_id) noexcept {
        pre_emit_numa_();
        if (active_stream_ == nullptr || active_stream_->is_null()) {
            return ir::INVALID_SLOT;
        }
        const uint32_t rid = (result_id == ir::INVALID_SLOT)
                                 ? allocate_temp_id()
                                 : result_id;
        active_stream_->push_back(ir::gen::make_generated_node(op, rid, lhs, rhs));
        annotate_result_type_(*active_stream_, op, rid);
        try_annotate_debug_and_type_(*active_stream_, active_stream_->size() - 1);
        return rid;
    }

    [[nodiscard]] uint32_t emit_binary_op_i32_(ir::OpCode op,
                                               uint32_t lhs, uint32_t rhs,
                                               uint32_t result_id) noexcept {
        pre_emit_numa_();
        if (active_stream_ == nullptr || active_stream_->is_null()) {
            return ir::INVALID_SLOT;
        }
        const uint32_t rid = (result_id == ir::INVALID_SLOT)
                                 ? allocate_temp_id()
                                 : result_id;
        active_stream_->push_back(ir::gen::make_generated_node(op, rid, lhs, rhs));
        annotate_result_type_(*active_stream_, op, rid);
        try_annotate_debug_and_type_(*active_stream_, active_stream_->size() - 1);
        return rid;
    }

    [[nodiscard]] uint32_t emit_unary_op_(ir::OpCode op,
                                          uint32_t value,
                                          uint32_t result_id) noexcept {
        pre_emit_numa_();
        if (!has_insertion_point()) {
            return ir::INVALID_SLOT;
        }
        const uint32_t rid = (result_id == ir::INVALID_SLOT) ? allocate_temp_id() : result_id;
        active_stream_->push_back(ir::gen::make_generated_node(op, rid, value));
        annotate_result_type_(*active_stream_, op, rid);
        try_annotate_debug_and_type_(*active_stream_, active_stream_->size() - 1);
        return rid;
    }

    [[nodiscard]] uint32_t emit_ternary_op_(ir::OpCode op,
                                            uint32_t a,
                                            uint32_t b,
                                            uint32_t c,
                                            uint32_t result_id) noexcept {
        pre_emit_numa_();
        if (!has_insertion_point()) {
            return ir::INVALID_SLOT;
        }
        const uint32_t rid = (result_id == ir::INVALID_SLOT) ? allocate_temp_id() : result_id;
        active_stream_->push_back(ir::gen::make_generated_node(op, rid, a, b, c));
        annotate_result_type_(*active_stream_, op, rid);
        try_annotate_debug_and_type_(*active_stream_, active_stream_->size() - 1);
        return rid;
    }

    void emit_side_effect_op0_(ir::OpCode op) noexcept {
        pre_emit_numa_();
        if (!has_insertion_point()) return;
        active_stream_->push_back(ir::gen::make_generated_node(op,
                                                               allocate_temp_id(),
                                                               ir::INVALID_SLOT,
                                                               ir::INVALID_SLOT,
                                                               ir::INVALID_SLOT,
                                                               static_cast<uint16_t>(ir::Flags::HAS_SIDE_EFFECTS)));
        try_annotate_debug_and_type_(*active_stream_, active_stream_->size() - 1);
    }

    [[nodiscard]] static uint32_t next_string_handle_candidate(uint32_t base,
                                                               uint32_t salt) noexcept {
        uint32_t handle = base + (0x9E3779B9u * salt);
        if (handle == 0u || handle == ir::INVALID_SLOT) {
            handle ^= 0xA5A5A5A5u;
        }
        if (handle == 0u || handle == ir::INVALID_SLOT) {
            handle = 1u + salt;
        }
        return handle;
    }

    [[nodiscard]] uint32_t intern_string_handle(std::string_view literal) {
        const std::string key{literal};
        auto it = string_handle_cache_.find(key);
        if (it != string_handle_cache_.end()) {
            return it->second;
        }

        const uint32_t base_handle =
            dgtl::utils::hashing::Hashing::fnv1a_32(literal.data(), literal.size());

        uint32_t salt = 0;
        uint32_t handle = next_string_handle_candidate(base_handle, salt);
        while (true) {
            const std::string_view existing = dgtl::runtime::try_lookup_string(static_cast<uint64_t>(handle));
            if (existing.empty() || existing == literal) {
                break;
            }
            ++salt;
            handle = next_string_handle_candidate(base_handle, salt);
        }

        dgtl::runtime::register_string_handle(handle,
                                              literal.data(),
                                              static_cast<uint32_t>(literal.size()));
        string_handle_cache_.emplace(key, handle);
        return handle;
    }

    [[nodiscard]] uint32_t allocate_temp_id() noexcept {
        return next_temp_id_++;
    }

    [[nodiscard]] static ir::OpCode map_i32_binary_op_(BinaryI32Op op) noexcept {
        switch (op) {
            case BinaryI32Op::Add: return ir::OpCode::ADD_I32;
            case BinaryI32Op::Sub: return ir::OpCode::SUB_I32;
            case BinaryI32Op::Mul: return ir::OpCode::MUL_I32;
            case BinaryI32Op::Div: return ir::OpCode::DIV_I32;
            case BinaryI32Op::Mod: return ir::OpCode::MOD_I32;
            case BinaryI32Op::And: return ir::OpCode::AND_I32;
            case BinaryI32Op::Or: return ir::OpCode::OR_I32;
            case BinaryI32Op::Xor: return ir::OpCode::XOR_I32;
            case BinaryI32Op::Shl: return ir::OpCode::SHL_I32;
            case BinaryI32Op::Shr: return ir::OpCode::SHR_I32;
            default: return ir::OpCode::ADD_I32;
        }
    }

    [[nodiscard]] static ir::OpCode map_i32_compare_op_(CompareI32Op op) noexcept {
        switch (op) {
            case CompareI32Op::Eq: return ir::OpCode::CMP_EQ_I32;
            case CompareI32Op::Ne: return ir::OpCode::CMP_NE_I32;
            case CompareI32Op::Lt: return ir::OpCode::CMP_LT_I32;
            case CompareI32Op::Le: return ir::OpCode::CMP_LE_I32;
            case CompareI32Op::Gt: return ir::OpCode::CMP_GT_I32;
            case CompareI32Op::Ge: return ir::OpCode::CMP_GE_I32;
            default: return ir::OpCode::CMP_EQ_I32;
        }
    }

    [[nodiscard]] static ir::OpCode map_runtime_intrinsic_(RuntimeIntrinsic intrinsic) noexcept {
        switch (intrinsic) {
            case RuntimeIntrinsic::StringLength: return ir::OpCode::STRING_LENGTH;
            case RuntimeIntrinsic::StringCompare: return ir::OpCode::STRING_COMPARE;
            case RuntimeIntrinsic::StringConcat: return ir::OpCode::STRING_CONCAT;
            case RuntimeIntrinsic::StringSubstring: return ir::OpCode::STRING_SUBSTRING;
            case RuntimeIntrinsic::ReadTsc: return ir::OpCode::READ_TSC;
            case RuntimeIntrinsic::PrefetchT0: return ir::OpCode::PREFETCH_T0;
            case RuntimeIntrinsic::Clflush: return ir::OpCode::CLFLUSH;
            case RuntimeIntrinsic::SpinAcquire: return ir::OpCode::SPIN_ACQUIRE;
            case RuntimeIntrinsic::RingPushFree: return ir::OpCode::RING_PUSH_FREE;
            case RuntimeIntrinsic::WasmInvoke: return ir::OpCode::WASM_INVOKE;
            case RuntimeIntrinsic::FiberYieldLoop: return ir::OpCode::FIBER_YIELD_LOOP;
            case RuntimeIntrinsic::SuspendCtx: return ir::OpCode::SUSPEND_CTX;
            case RuntimeIntrinsic::ResumeCtx: return ir::OpCode::RESUME_CTX;
            case RuntimeIntrinsic::NeonVaddQ: return ir::OpCode::NEON_VADD_Q;
            case RuntimeIntrinsic::MatmulBf16: return ir::OpCode::MATMUL_BF16;
            case RuntimeIntrinsic::MatmulFp8: return ir::OpCode::MATMUL_FP8;
            case RuntimeIntrinsic::SdpaF16: return ir::OpCode::SDPA_F16;
            case RuntimeIntrinsic::SoftmaxFast: return ir::OpCode::SOFTMAX_FAST;
            case RuntimeIntrinsic::LayerNorm: return ir::OpCode::LAYER_NORM;
            case RuntimeIntrinsic::RopeEmbed: return ir::OpCode::ROPE_EMBED;
            case RuntimeIntrinsic::FastInvSqrt: return ir::OpCode::FAST_INV_SQRT;
            case RuntimeIntrinsic::VecAddF32512: return ir::OpCode::VEC_ADD_F32_512;
            case RuntimeIntrinsic::VecMulF32512: return ir::OpCode::VEC_MUL_F32_512;
            case RuntimeIntrinsic::StoreV512: return ir::OpCode::STORE_V512;
            default: return ir::OpCode::READ_TSC;
        }
    }

    [[nodiscard]] static const ir::gen::OpDefMetadata* lookup_generated_op_def_(std::string_view op_name) noexcept {
        const auto it = std::find_if(std::begin(ir::gen::kGeneratedOpDefs), std::end(ir::gen::kGeneratedOpDefs),
                                     [op_name](const auto& meta) { return meta.name == op_name; });
        return it != std::end(ir::gen::kGeneratedOpDefs) ? &(*it) : nullptr;
    }

    [[nodiscard]] static const ir::gen::OpDefMetadata* lookup_generated_op_def_(ir::OpCode opcode) noexcept {
        const auto it = std::find_if(std::begin(ir::gen::kGeneratedOpDefs), std::end(ir::gen::kGeneratedOpDefs),
                                     [opcode](const auto& meta) { return meta.opcode == opcode; });
        return it != std::end(ir::gen::kGeneratedOpDefs) ? &(*it) : nullptr;
    }
};

} // namespace api
} // namespace dgtl