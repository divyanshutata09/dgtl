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

# DGTL Verification Guide

This guide was simplified after cleanup of `.ps1` scripts and generated `.exe` outputs.

## Goal
Verify that source, build configuration, and tests are still in a good state using your current toolchain.

## Manual Verification Checklist
1. Confirm source tree integrity (`include/`, `src/`, `tests/`, `build/`).
2. Configure/build using your preferred compiler workflow.
3. Run available tests from `tests/` and inspect failures.
4. Ensure documentation links in `docs/` and `README.md` are still valid.

## Scope
- This guide intentionally avoids references to removed script-based workflows.
- If automation scripts are reintroduced later, document them in a new commands guide.
