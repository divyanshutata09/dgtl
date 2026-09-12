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
#include <new>
#include <memory>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

export module dgtl.ir_core.function;

// FIXED CYCLIC LOOP: Import local leaf sibling dependencies natively via short-form shortcuts
import dgtl.ir_enums;
import dgtl.ir_node;
import dgtl.arena;
import dgtl.ir_core.stream;
import dgtl.ir_core.semantic;
export import dgtl.ir_core.debug;

export namespace dgtl::core {

using FrontendTypeHandle = uint64_t;

struct DGTLFunctionParameter {
    [[maybe_unused]] std::string name;
    uint32_t slot_id = dgtl::ir::INVALID_SLOT;
    FrontendTypeHandle type_handle = 0;
    bool is_variadic = false;
};

/**
 * @class DGTLFunction
 * @brief Represents a single unit of compilation within the DGTL framework.
 * [STAFF NOTE]: Version 1.1.0 optimization replaces unordered_map with 
 * Arena-backed contiguous vectors for O(1) type/hint resolution.
 */
class DGTLFunction {
public:
    DGTLFunction(DGTL_Arena& arena, std::string_view name, size_t reserved_nodes = 256)
        : arena_(arena),
          name_(name),
          stream_(arena, reserved_nodes),
          symbols_(arena),
          debug_store_(std::make_unique<dgtl::ir::debug::DGTLDebugStore>(arena, reserved_nodes)) {
        // Pre-reserve metadata vectors to minimize reallocations during lowering
        slot_type_handles_.reserve(reserved_nodes);
        temp_type_hints_.reserve(reserved_nodes);
    }

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] ir::IRStream& stream() noexcept { return stream_; }
    [[nodiscard]] const ir::IRStream& stream() const noexcept { return stream_; }
    [[nodiscard]] dgtl::semantic::SymbolTable& symbols() noexcept { return symbols_; }
    [[nodiscard]] const dgtl::semantic::SymbolTable& symbols() const noexcept { return symbols_; }
    [[nodiscard]] dgtl::ir::debug::DGTLDebugStore& debug_store() noexcept { return *debug_store_; }
    [[nodiscard]] const dgtl::ir::debug::DGTLDebugStore& debug_store() const noexcept { return *debug_store_; }
    [[nodiscard]] const std::vector<DGTLFunctionParameter>& parameters() const noexcept { return parameters_; }

    [[nodiscard]] uint32_t allocate_slot() noexcept {
        if (next_slot_id_ == dgtl::ir::INVALID_SLOT) [[unlikely]] {
            return dgtl::ir::INVALID_SLOT;
        }
        return next_slot_id_++;
    }

    [[nodiscard]] uint32_t add_parameter(std::string_view sym_name,
                                         FrontendTypeHandle type_handle = 0,
                                         dgtl::ir::OpCode type_hint = dgtl::ir::OpCode::NOP,
                                         bool is_variadic = false) {
        const uint32_t slot_id = allocate_slot();
        if (slot_id == dgtl::ir::INVALID_SLOT) [[unlikely]] {
            return dgtl::ir::INVALID_SLOT;
        }
        if (!symbols_.define(sym_name, slot_id, type_hint)) [[unlikely]] {
            return dgtl::ir::INVALID_SLOT;
        }
        parameters_.push_back(DGTLFunctionParameter{std::string(sym_name), slot_id, type_handle, is_variadic});
        
        if (type_handle != 0) {
            ensure_metadata_capacity(slot_id);
            slot_type_handles_[slot_id] = type_handle;
        }
        return slot_id;
    }

    [[nodiscard]] bool define_local(std::string_view sym_name,
                                    uint32_t slot_id,
                                    FrontendTypeHandle type_handle = 0,
                                    dgtl::ir::OpCode type_hint = dgtl::ir::OpCode::NOP) {
        if (slot_id == dgtl::ir::INVALID_SLOT) [[unlikely]] {
            return false;
        }
        if (!symbols_.define(sym_name, slot_id, type_hint)) [[unlikely]] {
            return false;
        }
        if (type_handle != 0) {
            ensure_metadata_capacity(slot_id);
            slot_type_handles_[slot_id] = type_handle;
        }
        return true;
    }

    [[nodiscard]] std::optional<uint32_t> resolve_slot(std::string_view sym_name) const noexcept {
        const auto entry = symbols_.resolve(sym_name);
        if (!entry.has_value()) {
            return std::nullopt;
        }
        return entry->slot_id;
    }

    [[nodiscard]] uint32_t define_or_get_local(std::string_view sym_name,
                                               FrontendTypeHandle type_handle = 0,
                                               dgtl::ir::OpCode type_hint = dgtl::ir::OpCode::NOP) {
        if (const auto existing = resolve_slot(sym_name); existing.has_value()) {
            if (*existing == dgtl::ir::INVALID_SLOT) [[unlikely]] {
                return dgtl::ir::INVALID_SLOT;
            }
            if (type_handle != 0) {
                ensure_metadata_capacity(*existing);
                slot_type_handles_[*existing] = type_handle;
            }
            return *existing;
        }

        const uint32_t slot_id = allocate_slot();
        const bool defined = define_local(sym_name, slot_id, type_handle, type_hint);
        if (!defined) [[unlikely]] {
            return dgtl::ir::INVALID_SLOT;
        }
        return slot_id;
    }

    [[nodiscard]] uint32_t define_temp(FrontendTypeHandle type_handle = 0,
                                       dgtl::ir::OpCode type_hint = dgtl::ir::OpCode::NOP) {
        const uint32_t slot_id = allocate_slot();
        if (slot_id == dgtl::ir::INVALID_SLOT) [[unlikely]] {
            return dgtl::ir::INVALID_SLOT;
        }
        ensure_metadata_capacity(slot_id);
        
        if (type_handle != 0) {
            slot_type_handles_[slot_id] = type_handle;
        }
        if (type_hint != dgtl::ir::OpCode::NOP) {
            temp_type_hints_[slot_id] = type_hint;
        }
        return slot_id;
    }

    void set_return_type(FrontendTypeHandle type_handle) noexcept {
        return_type_handle_ = type_handle;
    }

    [[nodiscard]] FrontendTypeHandle return_type() const noexcept {
        return return_type_handle_;
    }

    void set_slot_type(uint32_t slot_id, FrontendTypeHandle type_handle) noexcept {
        if (slot_id == dgtl::ir::INVALID_SLOT) [[unlikely]] {
            return;
        }
        ensure_metadata_capacity(slot_id);
        slot_type_handles_[slot_id] = type_handle;
    }

    [[nodiscard]] std::optional<FrontendTypeHandle> get_slot_type(uint32_t slot_id) const noexcept {
        if (slot_id == dgtl::ir::INVALID_SLOT) [[unlikely]] {
            return std::nullopt;
        }
        if (slot_id >= slot_type_handles_.size()) [[unlikely]] {
            return std::nullopt;
        }
        FrontendTypeHandle h = slot_type_handles_[slot_id];
        return (h != 0) ? std::make_optional(h) : std::nullopt;
    }

    void set_temp_type_hint(uint32_t slot_id,
                            dgtl::ir::OpCode hint) noexcept {
        if (slot_id == dgtl::ir::INVALID_SLOT) [[unlikely]] {
            return;
        }
        ensure_metadata_capacity(slot_id);
        temp_type_hints_[slot_id] = hint;
    }

    [[nodiscard]] std::optional<dgtl::ir::OpCode> get_temp_type_hint(uint32_t slot_id) const noexcept {
        if (slot_id == dgtl::ir::INVALID_SLOT) [[unlikely]] {
            return std::nullopt;
        }
        if (slot_id >= temp_type_hints_.size()) [[unlikely]] {
            return std::nullopt;
        }
        const dgtl::ir::OpCode hint = temp_type_hints_[slot_id];
        return (hint != dgtl::ir::OpCode::NOP) ? std::make_optional(hint) : std::nullopt;
    }

    void begin_scope() {
        symbols_.push_scope();
    }

    void end_scope() {
        symbols_.pop_scope();
    }

private:
    /**
     * @brief Ensures flat metadata vectors can accommodate the given slot index.
     * [STAFF NOTE]: Using vectors instead of maps eliminates hashing overhead
     * and utilizes CPU prefetching during linear IR lowering.
     */
    void ensure_metadata_capacity(uint32_t slot_id) noexcept {
        if (slot_id >= slot_type_handles_.size()) [[unlikely]] {
            const size_t new_size = static_cast<size_t>(slot_id) + 128;
            slot_type_handles_.resize(new_size, 0);
            temp_type_hints_.resize(new_size, dgtl::ir::OpCode::NOP);
        }
    }

    DGTL_Arena& arena_;
    std::string name_;
    ir::IRStream stream_;
    dgtl::semantic::SymbolTable symbols_;
    std::unique_ptr<dgtl::ir::debug::DGTLDebugStore> debug_store_;
    std::vector<DGTLFunctionParameter> parameters_;

    // HFT UPGRADE: Contiguous storage for metadata
    std::vector<FrontendTypeHandle> slot_type_handles_;
    std::vector<dgtl::ir::OpCode> temp_type_hints_;

    FrontendTypeHandle return_type_handle_ = 0;
    uint32_t next_slot_id_ = 1;
};

} // namespace dgtl::core