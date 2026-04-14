# DGTL Dependency Version Control

## Purpose
This file is the single source of truth for externally required tool and platform versions used to build and run DGTL.

## Locked Baseline
- Project: DGTL
- Lock Date: 2026-04-14
- Status: Active

## Toolchain
- C++ Language Standard: C++26
- C Compiler Standard: C17
- Compiler Family: GCC (UCRT64)
- GCC Version: 15.2.0
- Compiler Path: C:/msys64/ucrt64/bin/g++.exe
- IntelliSense Mode: windows-gcc-x64

## Build and Host Platform
- OS: Windows
- Shell: PowerShell 5.1+
- Runtime ABI: UCRT64

## Build Flags Baseline
- Standard Build: -std=c++26 -O2
- Include Paths:
  - ./include
  - ./src

## Project Include Surface Lock
- Umbrella Header: include/dgtl/all.hpp
- Entry Point: src/main.cpp

## Backend Support Lock
- arm64
- x64
- nasm
- wasm

## Version Update Policy
1. Any change to compiler version, standard version, ABI, or backend support must update this file in the same change.
2. Include the date of change and reason below.
3. Rebuild and smoke-test all backends after updates.

## Change Log
- 2026-04-14: Initial dependency version-control file created.
