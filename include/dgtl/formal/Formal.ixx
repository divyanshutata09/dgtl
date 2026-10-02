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

export module dgtl.formal;

export import dgtl.formal.smt;
export import dgtl.ir_formal_verifier;

export namespace dgtl::formal {
    using VerificationEngine = dgtl::formal::DGTLFormalVerificationSMT;
}
