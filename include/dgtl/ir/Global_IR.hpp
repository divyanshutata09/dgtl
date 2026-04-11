/* * DGTL-MLIR-X v1.0 (DGTL Edition)
 * THE UNIVERSAL IR LINKER (UIL)
 * -------------------------------------------------------------------------
 * This is the singular entry point for the BitPacked IR infrastructure.
 * ORDER OF INCLUSION IS CRITICAL FOR C++26 TEMPLATE RESOLUTION.
 */

#pragma once

// PART 1: FUNDAMENTAL TYPES (The Bit-Level Definitions)
#include "IR_Types.hpp"   // Opcodes, TypeEnums, Width-specifiers

// PART 2: META-PROGRAMMING TRAITS (The Compiler's Logic)
#include "IR_Traits.hpp"  // SFINAE/Concepts for IR validity

// PART 3: THE DIALECT ABSTRACTION (The Domain Bridge)
#include "IR_Dialect_interface.hpp" // Rules for AI, HFT, and Mobile App opcodes

// PART 4: THE ATOMIC UNIT (The Data Layout)
#include "IR_Node.hpp"    // The 128/256-bit BitPacked Node structure

// PART 5: MEMORY ARCHITECTURE (The Physical Backing)
#include "IR_Storage.hpp" // Arena-backed storage logic

// PART 6: THE ORCHESTRATOR (The Public Interface)
#include "IR_StreamCloneTransn.hpp" // The IRStream and Transactional API

/**
 * @namespace dgtl::ir
 * @brief High-performance, bit-packed Intermediate Representation.
 * * DESIGN ARCHITECTURE:
 * 1. Types -> 2. Traits -> 3. Dialect -> 4. Node -> 5. Storage -> 6. Stream
 */
