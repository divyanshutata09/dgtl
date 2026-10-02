/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Semantic Type System & Hardware Layout Solver
 * Standards:   C++26 / UCRT64 / HFT Zero-Allocation Type Registry
 */

module;

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <string>
#include <vector>
#include <span>
#include <optional>
#include <algorithm>
#include <bit>

export module dgtl.hir.type_system;

import dgtl.arena;
import dgtl.ir_enums;
import dgtl.ir_core.abi;
import dgtl.hashing;

export namespace dgtl::hir::semantic {

/**
 * @enum TypeClass
 * @brief High-level mathematical classification of a semantic type.
 */
enum class TypeClass : uint8_t {
    Primitive = 0,
    Pointer,
    Vector,
    Tensor,
    Struct,
    Tuple,
    Function,
    Opaque
};

/**
 * @struct StructField
 * @brief Metadata for a struct member field.
 */
struct StructField {
    std::string_view name;
    uint32_t type_id = 0;
    uint32_t offset_bytes = 0;
    uint32_t size_bytes = 0;
    uint32_t alignment = 0;
};

/**
 * @struct alignas(32) TypeDescriptor
 * @brief Immutable descriptor for concrete, compound, and hardware-vector types.
 */
struct alignas(32) TypeDescriptor {
    uint32_t id = 0;
    std::string_view name;
    TypeClass type_class = TypeClass::Primitive;
    dgtl::ir::TypeCode canonical_code = dgtl::ir::TypeCode::VOID_T;
    dgtl::core::DGTLABIValueClass abi_class = dgtl::core::DGTLABIValueClass::none;

    uint32_t size_bytes = 0;
    uint32_t alignment = 1;

    // Pointer / Vector / Tensor metadata
    uint32_t element_type_id = 0;
    uint32_t element_count = 0;      // Vector lanes or Tuple field count
    uint32_t address_space = 0;       // 0=Generic, 1=Global, 2=Shared, etc.

    // Compound fields
    std::span<const StructField> fields{};

    [[nodiscard]] constexpr bool is_integer() const noexcept {
        return canonical_code >= dgtl::ir::TypeCode::I4 &&
               canonical_code <= dgtl::ir::TypeCode::I512;
    }

    [[nodiscard]] constexpr bool is_floating_point() const noexcept {
        return dgtl::ir::is_floating_type_code(canonical_code);
    }

    [[nodiscard]] constexpr bool is_pointer() const noexcept {
        return type_class == TypeClass::Pointer ||
               canonical_code == dgtl::ir::TypeCode::PTR ||
               dgtl::ir::is_pointer_type_code(canonical_code);
    }

    [[nodiscard]] constexpr bool is_vector() const noexcept {
        return type_class == TypeClass::Vector;
    }

    [[nodiscard]] constexpr bool is_tensor() const noexcept {
        return type_class == TypeClass::Tensor;
    }

    [[nodiscard]] constexpr bool is_aggregate() const noexcept {
        return type_class == TypeClass::Struct ||
               type_class == TypeClass::Tuple;
    }
};

/**
 * @class TypeSystem
 * @brief Arena-backed, zero-heap type interning pool and ABI layout solver.
 */
class alignas(64) TypeSystem {
public:
    explicit TypeSystem(dgtl::core::DGTL_Arena& arena) noexcept
        : arena_(arena) {
        types_.reserve(128);
        register_builtin_types();
    }

    ~TypeSystem() = default;

    TypeSystem(const TypeSystem&) = delete;
    TypeSystem& operator=(const TypeSystem&) = delete;
    TypeSystem(TypeSystem&&) noexcept = default;
    TypeSystem& operator=(TypeSystem&&) noexcept = default;

    // =========================================================================
    // BUILTIN TYPES
    // =========================================================================

    [[nodiscard]] const TypeDescriptor* get_primitive(dgtl::ir::TypeCode code) const noexcept {
        const size_t idx = static_cast<size_t>(code);
        return (idx < builtin_table_.size()) ? builtin_table_[idx] : nullptr;
    }

    [[nodiscard]] const TypeDescriptor* get_type(uint32_t type_id) const noexcept {
        if (type_id == 0 || type_id >= types_.size()) return nullptr;
        return types_[type_id];
    }

    [[nodiscard]] const TypeDescriptor* lookup_by_name(std::string_view name) const noexcept {
        for (const auto* t : types_) {
            if (t && t->name == name) return t;
        }
        return nullptr;
    }

    // =========================================================================
    // COMPOUND TYPE FACTORIES
    // =========================================================================

    const TypeDescriptor* get_pointer_type(uint32_t pointee_id, uint32_t addr_space = 0) {
        for (const auto* t : types_) {
            if (t && t->type_class == TypeClass::Pointer &&
                t->element_type_id == pointee_id &&
                t->address_space == addr_space) {
                return t;
            }
        }

        auto* desc = allocate_type();
        desc->id = static_cast<uint32_t>(types_.size() - 1);
        desc->type_class = TypeClass::Pointer;
        desc->canonical_code = dgtl::ir::TypeCode::PTR;
        desc->abi_class = dgtl::core::DGTLABIValueClass::pointer;
        desc->size_bytes = 8;
        desc->alignment = 8;
        desc->element_type_id = pointee_id;
        desc->address_space = addr_space;
        desc->name = intern_string("ptr");
        return desc;
    }

    const TypeDescriptor* get_vector_type(uint32_t element_id, uint32_t lane_count) {
        for (const auto* t : types_) {
            if (t && t->type_class == TypeClass::Vector &&
                t->element_type_id == element_id &&
                t->element_count == lane_count) {
                return t;
            }
        }

        const auto* elem = get_type(element_id);
        if (!elem) return nullptr;

        auto* desc = allocate_type();
        desc->id = static_cast<uint32_t>(types_.size() - 1);
        desc->type_class = TypeClass::Vector;
        desc->canonical_code = dgtl::ir::TypeCode::VECTOR_F32X16;
        desc->abi_class = dgtl::core::DGTLABIValueClass::custom;
        desc->size_bytes = elem->size_bytes * lane_count;
        desc->alignment = std::min(64u, std::bit_ceil(desc->size_bytes));
        desc->element_type_id = element_id;
        desc->element_count = lane_count;
        desc->name = intern_string("vec");
        return desc;
    }

    const TypeDescriptor* get_tuple_type(std::span<const uint32_t> element_type_ids) {
        auto* desc = allocate_type();
        desc->id = static_cast<uint32_t>(types_.size() - 1);
        desc->type_class = TypeClass::Tuple;
        desc->canonical_code = dgtl::ir::TypeCode::VOID_T;
        desc->abi_class = dgtl::core::DGTLABIValueClass::aggregate;

        uint32_t offset = 0;
        uint32_t max_align = 1;

        auto* fields = arena_.allocate<StructField>(element_type_ids.size());

        for (size_t i = 0; i < element_type_ids.size(); ++i) {
            const auto* elem = get_type(element_type_ids[i]);
            const uint32_t align = elem ? elem->alignment : 8;
            const uint32_t size = elem ? elem->size_bytes : 8;
            offset = align_up(offset, align);

            fields[i] = StructField{
                .name = "",
                .type_id = element_type_ids[i],
                .offset_bytes = offset,
                .size_bytes = size,
                .alignment = align
            };
            offset += size;
            max_align = std::max(max_align, align);
        }

        desc->fields = std::span<const StructField>(fields, element_type_ids.size());
        desc->alignment = max_align;
        desc->size_bytes = align_up(offset, max_align);
        desc->name = intern_string("tuple");
        return desc;
    }

    const TypeDescriptor* create_struct_type(std::string_view name, std::span<const StructField> input_fields) {
        auto* desc = allocate_type();
        desc->id = static_cast<uint32_t>(types_.size() - 1);
        desc->name = intern_string(name);
        desc->type_class = TypeClass::Struct;
        desc->canonical_code = dgtl::ir::TypeCode::VOID_T;
        desc->abi_class = dgtl::core::DGTLABIValueClass::aggregate;

        uint32_t offset = 0;
        uint32_t max_align = 1;

        auto* fields = arena_.allocate<StructField>(input_fields.size());

        for (size_t i = 0; i < input_fields.size(); ++i) {
            const auto* f_type = get_type(input_fields[i].type_id);
            const uint32_t align = f_type ? f_type->alignment : 8;
            const uint32_t size = f_type ? f_type->size_bytes : 8;
            offset = align_up(offset, align);

            fields[i] = StructField{
                .name = intern_string(input_fields[i].name),
                .type_id = input_fields[i].type_id,
                .offset_bytes = offset,
                .size_bytes = size,
                .alignment = align
            };
            offset += size;
            max_align = std::max(max_align, align);
        }

        desc->fields = std::span<const StructField>(fields, input_fields.size());
        desc->alignment = max_align;
        desc->size_bytes = align_up(offset, max_align);
        return desc;
    }

    [[nodiscard]] bool can_losslessly_coerce(const TypeDescriptor& from, const TypeDescriptor& to) const noexcept {
        if (from.id == to.id) return true;

        if (from.is_integer() && to.is_integer()) {
            return to.size_bytes >= from.size_bytes;
        }

        if (from.is_floating_point() && to.is_floating_point()) {
            return to.size_bytes >= from.size_bytes;
        }

        if (from.is_pointer() && to.is_pointer()) {
            return from.address_space == to.address_space;
        }

        return false;
    }

    [[nodiscard]] dgtl::core::DGTL_Arena& arena() noexcept { return arena_; }

private:
    dgtl::core::DGTL_Arena& arena_;
    std::vector<TypeDescriptor*> types_;
    std::array<const TypeDescriptor*, 32> builtin_table_{};

    [[nodiscard]] static constexpr uint32_t align_up(uint32_t val, uint32_t align) noexcept {
        if (align <= 1) return val;
        return (val + align - 1) & ~(align - 1);
    }

    TypeDescriptor* allocate_type() noexcept {
        auto* desc = arena_.allocate<TypeDescriptor>(1);
        std::memset(desc, 0, sizeof(TypeDescriptor));
        types_.push_back(desc);
        return desc;
    }

    [[nodiscard]] std::string_view intern_string(std::string_view src) noexcept {
        if (src.empty()) return "";
        char* mem = arena_.allocate<char>(src.size() + 1);
        if (!mem) return src;
        std::memcpy(mem, src.data(), src.size());
        mem[src.size()] = '\0';
        return std::string_view(mem, src.size());
    }

    void register_builtin(dgtl::ir::TypeCode code, std::string_view name,
                          dgtl::core::DGTLABIValueClass abi_cls, uint32_t size, uint32_t align) noexcept {
        auto* desc = allocate_type();
        desc->id = static_cast<uint32_t>(types_.size() - 1);
        desc->name = intern_string(name);
        desc->type_class = TypeClass::Primitive;
        desc->canonical_code = code;
        desc->abi_class = abi_cls;
        desc->size_bytes = size;
        desc->alignment = align;

        const size_t idx = static_cast<size_t>(code);
        if (idx < builtin_table_.size()) {
            builtin_table_[idx] = desc;
        }
    }

    void register_builtin_types() noexcept {
        using TC = dgtl::ir::TypeCode;
        using AC = dgtl::core::DGTLABIValueClass;

        register_builtin(TC::VOID_T, "void", AC::none, 0, 1);
        register_builtin(TC::BOOL, "Bool", AC::i32, 1, 1);
        register_builtin(TC::PREDICATE, "Predicate", AC::i32, 1, 1);
        register_builtin(TC::ADDRESS_SPACE, "AddressSpace", AC::i32, 4, 4);
        register_builtin(TC::PTR, "Ptr", AC::pointer, 8, 8);
        register_builtin(TC::STRING_HANDLE, "StringHandle", AC::i64, 8, 8);

        register_builtin(TC::I4, "I4", AC::i32, 1, 1);
        register_builtin(TC::I8, "I8", AC::i32, 1, 1);
        register_builtin(TC::I16, "I16", AC::i32, 2, 2);
        register_builtin(TC::I32, "I32", AC::i32, 4, 4);
        register_builtin(TC::I64, "I64", AC::i64, 8, 8);
        register_builtin(TC::I128, "I128", AC::i64, 16, 16);
        register_builtin(TC::I256, "I256", AC::i64, 32, 32);
        register_builtin(TC::I512, "I512", AC::i64, 64, 64);

        register_builtin(TC::F8, "F8", AC::f32, 1, 1);
        register_builtin(TC::BF16, "BF16", AC::f32, 2, 2);
        register_builtin(TC::F16, "F16", AC::f32, 2, 2);
        register_builtin(TC::F32, "F32", AC::f32, 4, 4);
        register_builtin(TC::F64, "F64", AC::f64, 8, 8);
        register_builtin(TC::F128, "F128", AC::f64, 16, 16);

        register_builtin(TC::VEC3_F32, "Vec3xF32", AC::custom, 12, 16);
        register_builtin(TC::VEC4_F32, "Vec4xF32", AC::custom, 16, 16);
        register_builtin(TC::MAT4X4_F32, "Mat4x4F32", AC::custom, 64, 64);
        register_builtin(TC::QUAT_F32, "QuatF32", AC::custom, 16, 16);
        register_builtin(TC::VECTOR_F32X16, "VectorF32x16", AC::custom, 64, 64);
        register_builtin(TC::VECTOR_I32X16, "VectorI32x16", AC::custom, 64, 64);
        register_builtin(TC::VECTOR_F16X32, "VectorF16x32", AC::custom, 64, 64);

        register_builtin(TC::TENSOR_FP8, "TensorFP8", AC::custom, 16, 16);
        register_builtin(TC::TENSOR_BF16, "TensorBF16", AC::custom, 16, 16);
        register_builtin(TC::TENSOR_F32, "TensorF32", AC::custom, 16, 16);
        register_builtin(TC::TENSOR_I64, "TensorI64", AC::custom, 16, 16);
    }
};

} // namespace dgtl::hir::semantic

export namespace dgtl::semantic {
    using namespace dgtl::hir::semantic;
    using TypeClass = dgtl::hir::semantic::TypeClass;
    using StructField = dgtl::hir::semantic::StructField;
    using TypeDescriptor = dgtl::hir::semantic::TypeDescriptor;
    using TypeSystem = dgtl::hir::semantic::TypeSystem;
}

export namespace dgtl::hir {
    using namespace dgtl::hir::semantic;
    using TypeClass = dgtl::hir::semantic::TypeClass;
    using StructField = dgtl::hir::semantic::StructField;
    using TypeDescriptor = dgtl::hir::semantic::TypeDescriptor;
    using TypeSystem = dgtl::hir::semantic::TypeSystem;
}