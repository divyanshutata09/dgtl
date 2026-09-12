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

export module dgtl_all;

// Top-level public umbrella module for SDK and external integration entry points.
export import dgtl.ir_stream;
export import dgtl.backend.isa.win64_frame_lowering;
export import dgtl.backend.linker.pe;