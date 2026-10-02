/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Object Symbol Definition & Models
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdlib>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

export module dgtl.object.symbol;

export namespace dgtl::object {

enum class SymbolBinding : std::uint8_t {
    Local,
    Global,
    Weak,
    Hidden
};

enum class SymbolType : std::uint8_t {
    NoType,
    Object,
    Function,
    Section,
    File,
    Common,
    TLS
};

enum class SymbolVisibility : std::uint8_t {
    Default,
    Internal,
    Hidden,
    Protected
};

struct Symbol {
    std::string name;
    std::uint64_t value = 0u;
    std::uint64_t size = 0u;
    std::uint32_t section_index = 0u;
    std::uint32_t binding_raw = 0u;
    std::uint32_t type_raw = 0u;
    SymbolBinding binding = SymbolBinding::Global;
    SymbolType type = SymbolType::Function;
    SymbolVisibility visibility = SymbolVisibility::Default;
    bool external = false;

    Symbol() = default;
    Symbol(std::string n, std::uint64_t v, std::uint64_t s, std::uint32_t b = 0, std::uint32_t t = 0, bool ext = false)
        : name(std::move(n)), value(v), size(s), binding_raw(b), type_raw(t),
          binding(decode_binding(b)),
          type(decode_type(t)),
          external(ext) {}

    [[nodiscard]] static constexpr SymbolBinding decode_binding(std::uint32_t raw) noexcept {
        switch (raw) {
            case 0: return SymbolBinding::Local;
            case 2: return SymbolBinding::Weak;
            case 3: return SymbolBinding::Hidden;
            default: return SymbolBinding::Global;
        }
    }

    [[nodiscard]] static constexpr SymbolType decode_type(std::uint32_t raw) noexcept {
        switch (raw) {
            case 1: return SymbolType::Object;
            case 2: return SymbolType::Function;
            case 3: return SymbolType::Section;
            case 4: return SymbolType::File;
            case 5: return SymbolType::Common;
            case 6: return SymbolType::TLS;
            default: return SymbolType::NoType;
        }
    }

    static Symbol make_function(std::string n, std::uint64_t offset, std::uint64_t sz, bool is_global = true) {
        Symbol s;
        s.name = std::move(n);
        s.value = offset;
        s.size = sz;
        s.binding = is_global ? SymbolBinding::Global : SymbolBinding::Local;
        s.binding_raw = is_global ? 1u : 0u;
        s.type = SymbolType::Function;
        s.type_raw = 2u;
        s.external = false;
        return s;
    }

    static Symbol make_external(std::string n,
                                SymbolType t = SymbolType::NoType,
                                std::uint32_t binding_raw_value = 1u) {
        Symbol s;
        s.name = std::move(n);
        s.value = 0u;
        s.size = 0u;
        s.binding_raw = binding_raw_value;
        s.type = t;
        s.type_raw = static_cast<std::uint32_t>(t == SymbolType::Function ? 2u :
                                                 t == SymbolType::Object ? 1u : 0u);
        s.binding = decode_binding(binding_raw_value);
        s.external = true;
        return s;
    }

    [[nodiscard]] bool is_defined() const noexcept { return !external; }
    [[nodiscard]] bool is_external() const noexcept { return external; }
    [[nodiscard]] bool is_function() const noexcept { return type == SymbolType::Function; }
    [[nodiscard]] bool is_object() const noexcept { return type == SymbolType::Object; }
    [[nodiscard]] bool is_global() const noexcept { return binding == SymbolBinding::Global; }
    [[nodiscard]] bool is_local() const noexcept { return binding == SymbolBinding::Local; }
    [[nodiscard]] bool is_weak() const noexcept { return binding == SymbolBinding::Weak; }
};

} // namespace dgtl::object



