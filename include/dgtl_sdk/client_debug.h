/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 * Component:   Public C-ABI SDK - Entire-DGTL Polyglot Conformance & Diagnostic Oracle
 *
 * Standards:   C17 / C++20 / C++26 Compatible
 */

#ifndef DGTL_SDK_CLIENT_DEBUG_H
#define DGTL_SDK_CLIENT_DEBUG_H

#include "types.h"
#include "target.h"
#include "object.h"
#include "module.h"
#include "linker.h"
#include "jit.h"
#include "mir.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * SECTION 1 — ENUMERATIONS & FLAGS
 * ========================================================================== */

/**
 * @brief Supported Source Language Frontends
 */
typedef enum dgtl_source_lang {
    DGTL_LANG_MYZON   = 0,  /**< Native MYZON source (.my) */
    DGTL_LANG_C       = 1,  /**< Standard ISO C source (.c) */
    DGTL_LANG_CPP     = 2,  /**< Standard ISO C++ source (.cpp) */
    DGTL_LANG_RUST    = 3,  /**< Standard Rust source (.rs) */
    DGTL_LANG_PYTHON  = 4,  /**< Python numeric / kernel script (.py) */
    DGTL_LANG_IR_TEXT = 5,  /**< Direct Textual DGTL IR (.ir) */
    DGTL_LANG_IR_BIR  = 6   /**< Direct Binary DGTL Bytecode (.bir) */
} dgtl_source_lang_t;

/**
 * @brief Status Verdict for each Architectural Subsystem in DGTL
 */
typedef enum dgtl_layer_status {
    DGTL_LAYER_UNTESTED = 0,
    DGTL_LAYER_PASS     = 1,  /**< Subsystem executed cleanly with verified invariants */
    DGTL_LAYER_FAIL     = 2,  /**< Subsystem failed, crashed, or corrupted invariants */
    DGTL_LAYER_SKIPPED  = 3,  /**< Subsystem intentionally bypassed */
    DGTL_LAYER_UNSAT    = 4   /**< SMT Formal Verification Equivalence Violation */
} dgtl_layer_status_t;

/**
 * @brief Comprehensive Verdict on DGTL Health
 */
typedef enum dgtl_debug_verdict {
    DGTL_DEBUG_OK                 = 0,  /**< DGTL is 100% healthy: outputs match perfectly */
    DGTL_DEBUG_MISCOMPILATION     = 1,  /**< DGTL output differs from reference oracle */
    DGTL_DEBUG_FRONTEND_ERROR     = 2,  /**< Layer 1: Frontend ingestion or syntax failure */
    DGTL_DEBUG_STREAM_ERROR       = 3,  /**< Layer 2: Transactional IRStream / shadow buffer fault */
    DGTL_DEBUG_SMT_UNSAT          = 4,  /**< Layer 3: Formal SMT invariant violation */
    DGTL_DEBUG_PASS_ERROR         = 5,  /**< Layer 4: Optimization pass crashed or invalidated IR */
    DGTL_DEBUG_POLYHEDRAL_ERROR   = 6,  /**< Layer 5: Polyhedral loop scheduler fault */
    DGTL_DEBUG_MIR_ERROR          = 7,  /**< Layer 6: Machine IR lowering / ISel pattern failure */
    DGTL_DEBUG_REGALLOC_ERROR     = 8,  /**< Layer 7: Universal Register Allocator failure */
    DGTL_DEBUG_MACHINE_PIPE_ERROR = 9,  /**< Layer 8: Machine pipeline / branch / peephole fault */
    DGTL_DEBUG_CODEGEN_ERROR      = 10, /**< Layer 9: Target ISA machine code emitter fault */
    DGTL_DEBUG_OBJECT_ERROR       = 11, /**< Layer 10: Binary object writer/reader failure */
    DGTL_DEBUG_LINKER_ERROR       = 12, /**< Layer 11: Sovereign Linker failure */
    DGTL_DEBUG_JIT_RUNTIME_ERROR  = 13, /**< Layer 12: In-Memory JIT loader / execution fault */
    DGTL_DEBUG_TIMEOUT            = 14  /**< Execution timed out */
} dgtl_debug_verdict_t;

/* ============================================================================
 * SECTION 2 — AUDIT STRUCTURES
 * ========================================================================== */

/**
 * @brief Granular Diagnostic Record for a Single DGTL Subsystem Layer
 */
typedef struct dgtl_layer_audit {
    dgtl_layer_status_t status;
    uint64_t            latency_us;           /**< Execution duration in microseconds */
    const char*         layer_name;           /**< e.g. "Layer 4: IR Transformation Passes" */
    const char*         subcomponent_failed;  /**< e.g. "GlobalValueNumbering", "PEObjectWriter" */
    const char*         error_message;        /**< Detailed failure or verification diagnostics */
    const char*         pre_transform_dump;   /**< Snapshot prior to transformation */
    const char*         post_transform_dump;  /**< Snapshot after transformation */
} dgtl_layer_audit_t;

/**
 * @brief Complete End-to-End Audit Report for the ENTIRE DGTL Subsystem
 */
typedef struct dgtl_entire_audit_report {
    bool                    dgtl_is_ok;           /**< TRUE if entire DGTL is 100% sound */
    dgtl_debug_verdict_t    verdict;              /**< High-level categorized verdict */

    /* Execution Results */
    int64_t                 dgtl_exit_code;
    const char*             dgtl_stdout;
    int64_t                 reference_exit_code;
    const char*             reference_stdout;

    /* Telemetry & Performance */
    uint64_t                total_compilation_us;
    uint64_t                execution_cycles;
    size_t                  peak_arena_bytes;
    uint32_t                ir_node_count;
    uint64_t                logic_hash;

    /* Subsystem Audits for All 12 Architectural Pillars of Entire DGTL */
    dgtl_layer_audit_t      layer_1_frontend;         /**< Polyglot Ingestion Layer */
    dgtl_layer_audit_t      layer_2_ir_stream;        /**< Transactional SoA Stream & Arena */
    dgtl_layer_audit_t      layer_3_smt_formal;       /**< SMT Formal Verification & Sentinels */
    dgtl_layer_audit_t      layer_4_ir_passes;        /**< All 26 Production Optimization Passes */
    dgtl_layer_audit_t      layer_5_polyhedral;       /**< Polyhedral SCoP Tiling & AI Cost Model */
    dgtl_layer_audit_t      layer_6_mir_isel;         /**< Machine IR & Instruction Selection */
    dgtl_layer_audit_t      layer_7_regalloc;         /**< Universal Graph Coloring RegAlloc */
    dgtl_layer_audit_t      layer_8_mir_pipeline;     /**< Machine Pipeline & Frame Lowering */
    dgtl_layer_audit_t      layer_9_isa_codegen;      /**< Multi-Target ISA Emitters (9 Backends) */
    dgtl_layer_audit_t      layer_10_object_file;     /**< Object File Readers & Writers */
    dgtl_layer_audit_t      layer_11_sovereign_linker;/**< Sovereign Standalone Linkers (PE/ELF/Mach-O) */
    dgtl_layer_audit_t      layer_12_jit_execution;   /**< In-Memory JIT Execution Engine */

    /* Root Cause Isolation */
    int32_t                 first_failing_layer_idx;  /**< 1-12 or -1 if all passed */
    const char*             root_cause_explanation;   /**< Human-readable root cause diagnosis */
    const char*             suggested_fix;            /**< Suggested engineering fix */
} dgtl_entire_audit_report_t;

/**
 * @brief Client Debug Verification Options
 */
typedef struct dgtl_debug_options {
    dgtl_source_lang_t      language;             /**< Input language */
    dgtl_target_arch_t      target_arch;          /**< Target architecture (default: host x64) */
    int32_t                 opt_level;            /**< 0=O0, 1=O1, 2=O2, 3=O3, 4=Ofast, 5=Os, 6=Oz */
    bool                    run_all_26_passes;    /**< Exercise all 26 passes */
    bool                    run_smt_verifier;     /**< Execute SMT formal property checking */
    bool                    run_sovereign_link;   /**< Link standalone native executable */
    bool                    auto_bisect_pass;     /**< Automatically isolate failing pass on mismatch */
    uint32_t                timeout_ms;           /**< Timeout per test run (default: 5000ms) */
    const char*             entry_symbol;         /**< Entry function name (default: "main") */
} dgtl_debug_options_t;

/* ============================================================================
 * SECTION 3 — PUBLIC C-ABI CLIENT DEBUG APIS
 * ========================================================================== */

/**
 * @brief Initialize default options for client debugging.
 */
DGTL_API void DGTL_CALL dgtl_debug_default_options(
    dgtl_debug_options_t* opts,
    dgtl_source_lang_t lang) DGTL_NOEXCEPT;

/**
 * @brief Cross-Language Differential Verification Oracle.
 * Compiles and runs the code through reference oracle AND Entire DGTL (Layers 1-12).
 * Verifies exact output parity and audits all 12 DGTL layers.
 */
DGTL_NODISCARD DGTL_API dgtl_entire_audit_report_t DGTL_CALL dgtl_debug_verify_entire_stack(
    const char* source_code,
    const dgtl_debug_options_t* options) DGTL_NOEXCEPT;

/**
 * @brief Golden Output Verification Oracle.
 * Compiles and runs the code through Entire DGTL, comparing stdout and exit code
 * against user-supplied expected values.
 */
DGTL_NODISCARD DGTL_API dgtl_entire_audit_report_t DGTL_CALL dgtl_debug_verify_against_golden(
    const char* source_code,
    const char* expected_stdout,
    int64_t     expected_exit_code,
    const dgtl_debug_options_t* options) DGTL_NOEXCEPT;

/**
 * @brief Multi-ISA Cross-Backend Conformance.
 * Verifies compilation of the code across ALL 9 target architectures:
 * x64, ARM64, WASM, RISC-V 64, ARM32, CUDA PTX, ROCm AMDGCN, NASM, and Mach-O.
 */
DGTL_NODISCARD DGTL_API bool DGTL_CALL dgtl_debug_verify_all_9_isas(
    const char* source_code,
    const dgtl_debug_options_t* options,
    dgtl_entire_audit_report_t out_reports[9]) DGTL_NOEXCEPT;

/**
 * @brief Automatic Pass Bisector.
 * When a semantic mismatch or corruption is detected, systematically bisects all
 * passes to isolate the exact pass responsible for the deviation.
 */
DGTL_NODISCARD DGTL_API const char* DGTL_CALL dgtl_debug_bisect_failing_pass(
    const char* source_code,
    const dgtl_debug_options_t* options) DGTL_NOEXCEPT;

/**
 * @brief Release memory allocated for diagnostic strings in the audit report.
 */
DGTL_API void DGTL_CALL dgtl_debug_free_report(
    dgtl_entire_audit_report_t* report) DGTL_NOEXCEPT;

#ifdef __cplusplus
}

/* ============================================================================
 * SECTION 4 — C++20 ERGONOMIC ZERO-OVERHEAD RAII WRAPPERS
 * ========================================================================== */

namespace dgtl::debug {

class AuditReportWrapper {
private:
    dgtl_entire_audit_report_t report_{};

public:
    AuditReportWrapper() noexcept = default;
    explicit AuditReportWrapper(dgtl_entire_audit_report_t r) noexcept : report_(r) {}
    ~AuditReportWrapper() noexcept {
        dgtl_debug_free_report(&report_);
    }

    AuditReportWrapper(const AuditReportWrapper&) = delete;
    AuditReportWrapper& operator=(const AuditReportWrapper&) = delete;

    AuditReportWrapper(AuditReportWrapper&& other) noexcept : report_(other.report_) {
        other.report_ = dgtl_entire_audit_report_t{};
    }

    AuditReportWrapper& operator=(AuditReportWrapper&& other) noexcept {
        if (this != &other) {
            dgtl_debug_free_report(&report_);
            report_ = other.report_;
            other.report_ = dgtl_entire_audit_report_t{};
        }
        return *this;
    }

    [[nodiscard]] const dgtl_entire_audit_report_t& get() const noexcept { return report_; }
    [[nodiscard]] const dgtl_entire_audit_report_t* operator->() const noexcept { return &report_; }
    [[nodiscard]] bool is_ok() const noexcept { return report_.dgtl_is_ok; }
    [[nodiscard]] dgtl_debug_verdict_t verdict() const noexcept { return report_.verdict; }
    [[nodiscard]] const char* stdout_str() const noexcept { return report_.dgtl_stdout ? report_.dgtl_stdout : ""; }
    [[nodiscard]] int64_t exit_code() const noexcept { return report_.dgtl_exit_code; }
    [[nodiscard]] int32_t failing_layer() const noexcept { return report_.first_failing_layer_idx; }
    [[nodiscard]] const char* root_cause() const noexcept { return report_.root_cause_explanation ? report_.root_cause_explanation : ""; }
};

inline AuditReportWrapper verify_entire_stack(
    const char* source_code,
    dgtl_source_lang_t lang = DGTL_LANG_MYZON,
    int32_t opt_level = 2) {
    dgtl_debug_options_t opts{};
    dgtl_debug_default_options(&opts, lang);
    opts.opt_level = opt_level;
    return AuditReportWrapper(dgtl_debug_verify_entire_stack(source_code, &opts));
}

inline AuditReportWrapper verify_against_golden(
    const char* source_code,
    const char* expected_stdout,
    int64_t expected_exit_code,
    dgtl_source_lang_t lang = DGTL_LANG_MYZON,
    int32_t opt_level = 2) {
    dgtl_debug_options_t opts{};
    dgtl_debug_default_options(&opts, lang);
    opts.opt_level = opt_level;
    return AuditReportWrapper(dgtl_debug_verify_against_golden(source_code, expected_stdout, expected_exit_code, &opts));
}

} // namespace dgtl::debug

#endif /* __cplusplus */

#endif /* DGTL_SDK_CLIENT_DEBUG_H */
