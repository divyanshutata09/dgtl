/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 * Component:   Public C-ABI SDK - Machine IR Introspection & Codegen
 *
 * Standards:   C17 / C++20 / C++26 Compatible
 */

#ifndef DGTL_SDK_MIR_H
#define DGTL_SDK_MIR_H

#include "types.h"
#include "target.h"
#include "module.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum dgtl_regalloc_strategy {
    DGTL_REGALLOC_GRAPH_COLORING = 0,
    DGTL_REGALLOC_LINEAR_SCAN    = 1
} dgtl_regalloc_strategy_t;

DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_mir_disassemble_stream(
    dgtl_stream_t* s,
    dgtl_target_machine_t tm,
    dgtl_text_t* out_text) DGTL_NOEXCEPT;

DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_mir_dump_machine_ir(
    dgtl_stream_t* s,
    dgtl_target_machine_t tm,
    dgtl_text_t* out_text) DGTL_NOEXCEPT;

DGTL_NODISCARD DGTL_API dgtl_err_t DGTL_CALL dgtl_mir_disassemble_module(
    dgtl_module_t mod,
    dgtl_target_machine_t tm,
    dgtl_text_t* out_text) DGTL_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif /* DGTL_SDK_MIR_H */