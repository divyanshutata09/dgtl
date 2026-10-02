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

/**
 * @file Pass_Manager.ixx
 * @brief Master header aggregating all pass manager components as module partitions.
 */

// 🚀 FIXED: Bind directly to the core intermediate macro layer space
export module dgtl.ir.pipeline.pass_manager;

// Re-export the sub-components seamlessly under the core module namespace
export import dgtl.ir.pipeline.pass_manager_config;
export import dgtl.ir.pipeline.pass_manager_analysis;
export import dgtl.ir.pipeline.pass_manager_orchestrator;

export namespace dgtl {

using TargetPrecision           = pipeline::TargetPrecision;
using PassStage                 = pipeline::PassStage;
using PassAffinity              = pipeline::PassAffinity;
using PassExecutionPolicy       = pipeline::PassExecutionPolicy;
using PassExecutionTrace        = pipeline::PassExecutionTrace;
using PipelineTelemetry         = pipeline::PipelineTelemetry;
using DGTLLanguageRootMarker    = pipeline::DGTLLanguageRootMarker;
using DGTLOptimizationPass      = pipeline::DGTLOptimizationPass;

using PreservedAnalysisKind     = pipeline::PreservedAnalysisKind;
using PreservedAnalysesInfo     = pipeline::PreservedAnalysesInfo;
using PassDependencyGraph       = pipeline::PassDependencyGraph;
using AnalysisID                = pipeline::AnalysisID;
using DGTLAnalysis              = pipeline::DGTLAnalysis;
using DGTLAnalysisManager       = pipeline::DGTLAnalysisManager;

using PassDecisionEngine        = pipeline::PassDecisionEngine;
using DGTLoptManager            = pipeline::DGTLoptManager;
using FunctionLevelPassExecutor = pipeline::FunctionLevelPassExecutor;
using Pass_Composer             = pipeline::Pass_Composer;

using AOT_Pipeline             = pipeline::AOT_Pipeline;
using JIT_Pipeline             = pipeline::JIT_Pipeline;
using Hybrid_Pipeline          = pipeline::Hybrid_Pipeline;

} // namespace dgtl

export namespace dgtl::pipeline {
    using namespace dgtl;
}

export namespace dgtl::ir::pipeline {
    using namespace dgtl::pipeline;
}

