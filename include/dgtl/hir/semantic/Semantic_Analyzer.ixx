/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Unified Semantic Driver & Pipeline Coordinator
 * Standards:   C++26 / UCRT64 / Production Compiler Verification Driver
 */

module;

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <span>

export module dgtl.hir.semantic_analyzer;

import dgtl.arena;
import dgtl.ir_enums;
import dgtl.ir_node;
import dgtl.ir.stream;
import dgtl.hir.semantic;
import dgtl.hir.type_system;
import dgtl.hir.type_checker;
import dgtl.hir.borrow_checker;
import dgtl.hir.name_resolver;
import dgtl.hir.constant_evaluator;
import dgtl.ir_core.abi;

export namespace dgtl::hir::semantic {

enum class DiagnosticLevel : uint8_t {
    Info = 0,
    Warning,
    Error,
    Fatal
};

struct SemanticDiagnostic {
    DiagnosticLevel level = DiagnosticLevel::Error;
    size_t node_index = 0;
    uint32_t slot_id = dgtl::ir::INVALID_SLOT;
    std::string message;
};

/**
 * @class SemanticAnalyzer
 * @brief Master semantic pass orchestrator driving SymbolTable, TypeSystem,
 * TypeChecker, BorrowChecker, NameResolver, and ConstantEvaluator in lock-step.
 */
class SemanticAnalyzer {
public:
    SemanticAnalyzer(dgtl::core::DGTL_Arena& arena,
                     SymbolTable& sym_table,
                     TypeSystem& type_sys) noexcept
        : arena_(arena),
          symbol_table_(sym_table),
          type_system_(type_sys),
          type_checker_(type_sys, sym_table),
          borrow_checker_(sym_table),
          name_resolver_(arena, sym_table, type_sys),
          constant_evaluator_(sym_table) {
        diagnostics_.reserve(32);
    }

    ~SemanticAnalyzer() = default;

    // Sub-component accessors for frontend driver bindings
    [[nodiscard]] SymbolTable& symbols() noexcept { return symbol_table_; }
    [[nodiscard]] TypeSystem& types() noexcept { return type_system_; }
    [[nodiscard]] NameResolver& names() noexcept { return name_resolver_; }
    [[nodiscard]] ConstantEvaluator& constants() noexcept { return constant_evaluator_; }

    /**
     * @brief Executes the complete semantic verification pipeline over an IRStream.
     * @return true if stream passed all semantic checks without errors.
     */
    [[nodiscard]] bool analyze(dgtl::ir::IRStream& stream) {
        diagnostics_.clear();
        if (stream.is_null() || stream.size() == 0) return true;

        // Stage 1: Compile-Time Constant Propagation & Folding
        constant_evaluator_.propagate_stream_constants(stream);

        // Stage 2: Type Checking & Hardware Signature Matching
        std::vector<TypeError> type_errors;
        if (!type_checker_.verify_stream(stream, type_errors)) {
            for (auto& err : type_errors) {
                diagnostics_.push_back(SemanticDiagnostic{
                    .level = DiagnosticLevel::Error,
                    .node_index = err.node_index,
                    .slot_id = err.slot_id,
                    .message = std::move(err.message)
                });
            }
        }

        // Stage 3: Linear Ownership & Lifetime Borrow Checking
        std::vector<OwnershipError> ownership_errors;
        if (!borrow_checker_.verify(stream, ownership_errors)) {
            for (auto& err : ownership_errors) {
                diagnostics_.push_back(SemanticDiagnostic{
                    .level = DiagnosticLevel::Error,
                    .node_index = err.node_index,
                    .slot_id = err.handle_slot,
                    .message = std::move(err.message)
                });
            }
        }

        return !has_errors();
    }

    [[nodiscard]] bool has_errors() const noexcept {
        for (const auto& d : diagnostics_) {
            if (d.level == DiagnosticLevel::Error || d.level == DiagnosticLevel::Fatal) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::span<const SemanticDiagnostic> diagnostics() const noexcept {
        return diagnostics_;
    }

private:
    dgtl::core::DGTL_Arena& arena_;
    SymbolTable& symbol_table_;
    TypeSystem& type_system_;
    TypeChecker type_checker_;
    BorrowChecker borrow_checker_;
    NameResolver name_resolver_;
    ConstantEvaluator constant_evaluator_;

    std::vector<SemanticDiagnostic> diagnostics_;
};

} // namespace dgtl::hir::semantic

export namespace dgtl::semantic {
    using namespace dgtl::hir::semantic;
    using DiagnosticLevel = dgtl::hir::semantic::DiagnosticLevel;
    using SemanticDiagnostic = dgtl::hir::semantic::SemanticDiagnostic;
    using SemanticAnalyzer = dgtl::hir::semantic::SemanticAnalyzer;
}

export namespace dgtl::hir {
    using namespace dgtl::hir::semantic;
    using DiagnosticLevel = dgtl::hir::semantic::DiagnosticLevel;
    using SemanticDiagnostic = dgtl::hir::semantic::SemanticDiagnostic;
    using SemanticAnalyzer = dgtl::hir::semantic::SemanticAnalyzer;
}