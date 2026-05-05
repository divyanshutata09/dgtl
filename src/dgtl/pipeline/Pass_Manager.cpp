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

#include "dgtl/pipeline/Pass_Manager.hpp"

// Query Optimization Passes (5)
#include "dgtl/pass/query/Query_Plan.hpp"
#include "dgtl/pass/query/Query_Cardinality_Estimator.hpp"
#include "dgtl/pass/query/Predicate_Pusher.hpp"
#include "dgtl/pass/query/Join_Order_Optimizer.hpp"
#include "dgtl/pass/query/Query_Optimizer.hpp"

// ML Optimization Passes (4)
#include "dgtl/pass/ml/Tensor_Fusion.hpp"
#include "dgtl/pass/ml/Tensor_Specializer.hpp"
#include "dgtl/pass/ml/ML_Backend_Dispatcher.hpp"
#include "dgtl/pass/ml/ML_Optimizer.hpp"

// SIMD Passes (4)
#include "dgtl/pass/simd/SIMD_Legalizer.hpp"
#include "dgtl/pass/simd/Advanced_SIMD_Recognizer.hpp"
#include "dgtl/pass/simd/Advanced_Vectorization.hpp"
#include "dgtl/pass/simd/SIMD_Gather_Scatter_Optimizer.hpp"

// Speculative Passes (4)
#include "dgtl/pass/speculative/Speculative_DCE.hpp"
#include "dgtl/pass/speculative/Speculative_Inliner.hpp"
#include "dgtl/pass/speculative/Speculative_Alias_Hoister.hpp"
#include "dgtl/pass/speculative/Temporal_Vector_Fusion.hpp"

// Transform Passes (7)
#include "dgtl/pass/transform/GVN.hpp"
#include "dgtl/pass/transform/Loop_Fusion.hpp"
#include "dgtl/pass/transform/Instruction_Fusion_Synth.hpp"
#include "dgtl/pass/transform/Poly_Transform.hpp"
#include "dgtl/pass/transform/Instruction_Folder.hpp"
#include "dgtl/pass/transform/DCE_Solver.hpp"
#include "dgtl/pass/transform/Instruction_Reordering.hpp"

// Tuning Passes (3)
#include "dgtl/pass/tuning/Auto_Tuner.hpp"
#include "dgtl/pass/tuning/Profile_Guided_Optimization.hpp"
#include "dgtl/pass/tuning/Microarchitecture_Tuning.hpp"

namespace dgtl::pipeline {

bool DGTLoptManager::register_all_passes() noexcept {
    if (order_frozen_) {
        return false;
    }

    passes.clear();

    try {
        // STAGE 1: PreInlining (3 passes)
        // Early optimization for basic patterns
        add_opt(std::make_unique<dgtl::pass::query::PredicatePusher>());
        add_opt(std::make_unique<dgtl::pass::DGTLAdvancedSIMDRecognizer>());
        add_opt(std::make_unique<dgtl::pass::DGTLSpeculativeDCE>());

        // STAGE 2: PostInlining (6 passes)
        // Main optimization pipeline with advanced patterns
        add_opt(std::make_unique<dgtl::pass::query::JoinOrderOptimizer>());
        add_opt(std::make_unique<dgtl::pass::query::QueryOptimizer>());
        add_opt(std::make_unique<dgtl::pass::DGTLAdvancedVectorization>());
        add_opt(std::make_unique<dgtl::pass::DGTLTemporalVectorFusion>());
        add_opt(std::make_unique<dgtl::pass::DGTLSpeculativeInliner>(nullptr, nullptr));
        add_opt(std::make_unique<dgtl::pass::DGTLLoopFusion>(nullptr));

        // STAGE 3: PostSpecialization (5 passes)
        // Specialized optimization for ML and complex transforms
        add_opt(std::make_unique<dgtl::pass::ml::TensorFusion>());
        add_opt(std::make_unique<dgtl::pass::ml::TensorSpecializer>());
        add_opt(std::make_unique<dgtl::pass::ml::MLBackendDispatcher>());
        add_opt(std::make_unique<dgtl::pass::ml::MLOptimizer>());
        add_opt(std::make_unique<dgtl::pass::DGTLMicroarchitectureTuning>());

        // STAGE 4: LateCleanup (1 pass)
        // Final optimization and dead code elimination
        add_opt(std::make_unique<dgtl::pass::DGTLProfileGuidedOptimization>(nullptr));

        return true;

    } catch (...) {
        // Clear partially registered passes on error
        passes.clear();
        return false;
    }
}

} // namespace dgtl::pipeline
