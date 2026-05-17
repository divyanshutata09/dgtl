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
 * @file Pass_Manager.hpp
 * @brief Master header aggregating all pass manager components.
 */

#pragma once

#include "dgtl/pipeline/Pass_Manager_Config.hpp"
#include "dgtl/pipeline/Pass_Manager_Analysis.hpp"
#include "dgtl/pipeline/Pass_Manager_Orchestrator.hpp"

namespace dgtl {

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

using AOT_Pipeline              = pipeline::AOT_Pipeline;
using JIT_Pipeline              = pipeline::JIT_Pipeline;
using Hybrid_Pipeline           = pipeline::Hybrid_Pipeline;

} // namespace dgtl
