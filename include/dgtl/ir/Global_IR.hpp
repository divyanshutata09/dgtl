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

/* * DGTL (Deep General-purpose Transformation Layer) version 1.0.0 (DGTL Edition)
 * THE UNIVERSAL IR LINKER (UIL)
 * -------------------------------------------------------------------------
 * This is the singular entry point for the BitPacked IR infrastructure.
 * ORDER OF INCLUSION IS CRITICAL FOR C++26 TEMPLATE RESOLUTION.
 */

#pragma once

#include "base/IR_Node.hpp"
#include "base/IR_PrintSerialDumper.hpp"
#include "base/IR_SSA.hpp"
#include "base/IR_Stream.hpp"
#include "base/IR_Verifier.hpp"


#include "gen/DGTL_Gen_Ops.hpp"
#include "interfaces/Op_Traits.hpp"

#include "evolver/IR_Evolver_Mutation.hpp"

#include "runtime/TLS_Flush_Merge.hpp"

namespace dgtl::ir {
    // This namespace is now fully populated via the headers above.
}
