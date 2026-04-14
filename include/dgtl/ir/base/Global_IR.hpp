/* * DGTL-MLIR-X v1.0 (DGTL Edition)
 * THE UNIVERSAL IR LINKER (UIL)
 * -------------------------------------------------------------------------
 * This is the singular entry point for the BitPacked IR infrastructure.
 * ORDER OF INCLUSION IS CRITICAL FOR C++26 TEMPLATE RESOLUTION.
 */

#pragma once

// PART 1: FUNDAMENTAL TYPES (The Opcodes and Bitmasks)
#include "IR_Types.hpp"       // Enum OpCode, struct Flags, struct IRTraits
#include "IR_Nodes.hpp"       // Unified IRNode architecture

// PART 2: UNIFIED NODE ARCHITECTURE (The Physical Layout)
// ✅ Added: Provides IRAtom, IRCore, and IRNode (the 24-byte unified view)

// PART 3: META-PROGRAMMING TRAITS (The Compiler's Logic)
// Depends on IR_Types and Global_IR for concept satisfaction
#include "IR_Traits.hpp"      // SFINAE/Concepts for IR validity

// PART 4: THE DIALECT ABSTRACTION (The Domain Bridge)
#include "../dialect/IR_Dialect_Interface.hpp" // Rules for AI, HFT, and Custom Dialects

// PART 5: MEMORY ARCHITECTURE (The Physical Backing)
// Depends on Global_IR for IRNode-specific arena allocation
#include "IR_Storage.hpp"     // Arena-backed storage logic

// PART 6: THE ORCHESTRATOR (The Public Interface)
#include "IR_Stream.hpp"      // The IRStream class and utilities

/**
 * @namespace dgtl::ir
 * @brief High-performance, bit-packed Intermediate Representation.
 * * DESIGN ARCHITECTURE (The UIL Chain):
 * 1. Types (Opcodes) -> 2. Global (Nodes) -> 3. Traits (Logic) -> 
 * 4. Dialect (Extension) -> 5. Storage (Memory) -> 6. Stream (Flow)
 */
namespace dgtl::ir {
    // This namespace is now fully populated via the headers above.
}