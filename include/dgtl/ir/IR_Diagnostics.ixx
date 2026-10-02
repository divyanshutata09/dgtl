
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

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

export module dgtl.ir_diagnostics;

export namespace dgtl::ir::diagnostics {

enum class Severity : std::uint8_t { Note, Warning, Error, Fatal };

enum class Domain : std::uint8_t { Schema, Structural, Semantic, SSA, Analysis, Pass, Legalization, Backend, Serialization };

struct Diagnostic {
    Severity severity{Severity::Error}; Domain domain{Domain::Structural};
    std::uint64_t node{~0ULL}; std::uint64_t stable_id{0};
    std::string message{}; std::string operation{};
    [[nodiscard]] constexpr bool is_error() const noexcept { return severity >= Severity::Error; }
};
class DiagnosticEngine {
    std::vector<Diagnostic> diagnostics_;
public:
    void clear() noexcept { diagnostics_.clear(); }
    void emit(Diagnostic d) { diagnostics_.push_back(std::move(d)); }
    void note(Domain d, std::string m) { emit({Severity::Note,d,~0ULL,0,std::move(m),{}}); }
    void warning(Domain d, std::string m) { emit({Severity::Warning,d,~0ULL,0,std::move(m),{}}); }
    void error(Domain d, std::string m) { emit({Severity::Error,d,~0ULL,0,std::move(m),{}}); }
    [[nodiscard]] bool has_errors() const noexcept { for (const auto& d: diagnostics_) if (d.is_error()) return true; return false; }
    [[nodiscard]] const std::vector<Diagnostic>& all() const noexcept { return diagnostics_; }
    [[nodiscard]] std::size_t size() const noexcept { return diagnostics_.size(); }
};
}


