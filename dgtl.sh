#!/bin/bash
###############################################################################
# DGTL Command Runner - Direct Memory & SDK Architecture Edition
# Optimized for MSYS2 UCRT64 / Linux / macOS
###############################################################################

set -e # Exit on error
set -o pipefail

# ============================================================================
# DETECT PLATFORM & SET CONDITIONALS
# ============================================================================

case "$OSTYPE" in
  linux*)   OS="Linux" ;;
  darwin*)  OS="macOS" ;;
  msys*|cygwin*|win32*) OS="Windows" ;;
  *) OS="Unknown" ;;
esac

echo "[dgtl] Detected platform: $OS"

# ============================================================================
# PATHS
# ============================================================================

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$ROOT/src"
BUILD_DIR="$ROOT/build"
INCLUDE_DIR="$ROOT/include"
BENCHMARKS_LOG_DIR="$ROOT/benchmarks/logs"

# SDK & Tools Paths
SDK_DIR="$BUILD_DIR/DGTL_SDK"
SDK_LIB="$SDK_DIR/lib/x64/dgtl.lib"
GEN_TOOL_SRC="$ROOT/tools/cpp/dgtl_gen_main.cpp"
GEN_TOOL_EXE="$BUILD_DIR/tools/dgtl-gen"
DEF_FILE="$INCLUDE_DIR/dgtl/ir/gen/defs/phase1_core_ops.dgtl_def"
GEN_HEADER="$INCLUDE_DIR/dgtl/ir/gen/DGTL_Gen_Ops.hpp"

# Main entry points
PROD_MAIN="$SRC_DIR/main.cpp"
SMOKE_MAIN="$SRC_DIR/apps/smoke_main.cpp"

# Compiled binaries
PROD_EXE="$BUILD_DIR/dgtl_release"
SMOKE_EXE="$BUILD_DIR/dgtl_main"
SMOKE_STRICT_EXE="$BUILD_DIR/dgtl_main_strict"
TEST_EXE="$BUILD_DIR/dgtl_tests"

# ============================================================================
# USAGE
# ============================================================================

show_usage() {
  cat << 'EOF'
DGTL command runner

Pick by intent:
  Build DGTL SDK for Myzon        dgtl build-sdk
  Generate IR Operations (DSL)    dgtl gen
  Build release binary            dgtl build-release
  Build smoke/conformance binary  dgtl build-smoke
  Build strict binary             dgtl build-strict
  Run full conformance            dgtl run-all
  Run one backend quickly         dgtl run-backend x64
  Run backend benchmark           dgtl bench-backend x64 fusion
  Build and run benchmark suite   dgtl brun
  Clean outputs/logs              dgtl clean-all
  Clean + build + run             dgtl clean-build-and-run

Categories (recommended):

1) SDK & Infrastructure
  dgtl build-sdk                          -> build/DGTL_SDK/ (For Myzon Linking)
  dgtl gen                                -> Generates DGTL_Gen_Ops.hpp

2) Build binaries
  dgtl build-release                      -> build/dgtl_release
  dgtl build-smoke                        -> build/dgtl_main
  dgtl build-strict                       -> build/dgtl_main_strict

3) Validate correctness
  dgtl run-all                            # full conformance
  dgtl run-backend <arm64|x64|nasm|wasm>  # quick backend check
  dgtl test [full]                        # aggregate source-backed tests

4) Benchmark performance
  dgtl bench-backend <backend> [profile|iterations] [pressure-vars]
  dgtl brun [program args...]

5) Clean and one-shot workflows
  dgtl clean-all
  dgtl clean-build-and-run

Alias mapping:
  build-release -> build
  build-smoke -> smoke-build
  build-strict -> strict
  run-all -> run
  run-backend -> run <backend>
  bench-backend -> bench <backend>
  run-tests -> test
  clean-all -> clean
  clean-build-and-run -> crun
EOF
}

# ============================================================================
# HELPERS
# ============================================================================

find_tool() {
  local tool=$1
  command -v "$tool" 2>/dev/null || echo "$tool"
}

find_g_plusplus() {
  local candidates=("g++" "g++-12" "g++-11" "clang++" "c++")
  for candidate in "${candidates[@]}"; do
    if command -v "$candidate" &>/dev/null; then
      if [[ "$candidate" != "clang"* ]]; then
        echo "$candidate"
        return 0
      fi
    fi
  done
  if command -v clang++ &>/dev/null; then echo "clang++"; return 0; fi
  echo "g++"
  return 1
}

get_source_files() {
  local entry_point="$1"
  local exclude_paths=("${@:2}")
  
  if [[ ! -f "$entry_point" ]]; then
    echo "[dgtl] ERROR: Missing entrypoint source: $entry_point" >&2
    return 1
  fi
  
  local all_sources
  all_sources=$(find "$SRC_DIR" -name "*.cpp" -type f | sort)
  
  local sources=()
  while IFS= read -r src; do
    local skip=false
    for exclude in "${exclude_paths[@]}"; do
      if [[ "$src" == "$exclude" ]]; then skip=true; break; fi
    done
    [[ $skip == false ]] && sources+=("$src")
  done <<< "$all_sources"
  
  sources+=("$entry_point")
  
  if [[ ${#sources[@]} -eq 0 ]]; then
    echo "[dgtl] ERROR: No source files found under $SRC_DIR" >&2
    return 1
  fi
  printf '%s\n' "${sources[@]}"
}

# ============================================================================
# NEW ARCHITECTURE: DSL GEN & SDK BUILDER
# ============================================================================

run_gen() {
  local g_plusplus
  g_plusplus=$(find_g_plusplus) || { echo "[dgtl] ERROR: C++ compiler not found" >&2; return 1; }

  mkdir -p "$(dirname "$GEN_TOOL_EXE")"
  
  # 1. Build the generator if needed
  if [[ ! -f "$GEN_TOOL_EXE" || "$GEN_TOOL_SRC" -nt "$GEN_TOOL_EXE" ]]; then
      echo "[dgtl] Compiling dgtl-gen tool..."
      "$g_plusplus" -std=c++26 -O2 "$GEN_TOOL_SRC" -o "$GEN_TOOL_EXE"
  fi

  # 2. Run the generator
  echo "[dgtl] Generating IR Ops from DSL..."
  "$GEN_TOOL_EXE" --root "$ROOT"
}

build_sdk() {
  run_gen # Always generate ops first

  echo "[dgtl] Building DGTL Static Library (SDK)..."
  mkdir -p "$SDK_DIR/lib/x64"
  mkdir -p "$SDK_DIR/include"
  local OBJ_DIR="$BUILD_DIR/obj_sdk"
  mkdir -p "$OBJ_DIR"

  local g_plusplus
  g_plusplus=$(find_g_plusplus)

  local cflags=("-std=c++26" "-O3" "-march=native" "-Wall" "-Wextra" "-I$INCLUDE_DIR")
  local sources=$(find "$SRC_DIR" -name "*.cpp" ! -path "*/apps/*" ! -name "main.cpp" ! -path "*/tools/*")

  local objects=()
  for src in $sources; do
      local rel_path="${src#$SRC_DIR/}"
      local obj="$OBJ_DIR/${rel_path//\//_}.o"
      
      if [[ "$src" -nt "$obj" ]]; then
          echo "[dgtl] Compiling $rel_path"
          "$g_plusplus" "${cflags[@]}" -c "$src" -o "$obj"
      fi
      objects+=("$obj")
  done

  echo "[dgtl] Archiving to $SDK_LIB"
  ar rcs "$SDK_LIB" "${objects[@]}"

  echo "[dgtl] Packaging Public Headers..."
  cp -r "$INCLUDE_DIR/dgtl" "$SDK_DIR/include/"
  
  # RUTHLESS PURGE: Remove private/frontend logic from SDK
  rm -rf "$SDK_DIR/include/dgtl/frontend"
  
  echo "[dgtl] SDK successfully built at $SDK_DIR"
}

# ============================================================================
# COMPILATION CORE
# ============================================================================

build_dgtl_binary() {
  local output_path="$1"
  local entry_point="$2"
  local strict="${3:-false}"
  shift 3
  local exclude_paths=("$@")
  
  run_gen # Enforce auto-generation before any binary build

  mkdir -p "$(dirname "$output_path")"
  
  local g_plusplus
  g_plusplus=$(find_g_plusplus) || { echo "[dgtl] ERROR: g++ not found" >&2; return 1; }
  
  echo "[dgtl] Using compiler: $g_plusplus"
  
  local cflags=("-std=c++26" "-O2" "-Wall" "-Wextra" "-I$INCLUDE_DIR")
  if [[ "$strict" == "true" ]]; then
    cflags+=("-Wpedantic" "-Werror")
  fi
  
  local sources
  sources=$(get_source_files "$entry_point" "${exclude_paths[@]}") || return 1
  
  echo "[dgtl] Compiling $(basename "$output_path")"
  
  local source_array=()
  while IFS= read -r src; do source_array+=("$src"); done <<< "$sources"
  
  "$g_plusplus" "${cflags[@]}" "${source_array[@]}" -o "$output_path" || {
    echo "[dgtl] ERROR: Compilation failed for $output_path" >&2
    return 1
  }
  
  echo "[dgtl] Wrote $output_path"
}

ensure_dgtl_binary() {
  local output_path="$1"
  local entry_point="$2"
  local strict="${3:-false}"
  shift 3
  local exclude_paths=("$@")
  
  if [[ ! -f "$output_path" ]]; then
    build_dgtl_binary "$output_path" "$entry_point" "$strict" "${exclude_paths[@]}" || return 1
    return 0
  fi
  
  local exe_time
  exe_time=$(stat -c%Y "$output_path" 2>/dev/null || stat -f%m "$output_path" 2>/dev/null)
  
  local needs_rebuild=false
  while IFS= read -r src; do
    local src_time
    src_time=$(stat -c%Y "$src" 2>/dev/null || stat -f%m "$src" 2>/dev/null)
    if [[ $src_time -gt $exe_time ]]; then needs_rebuild=true; break; fi
  done < <(find "$SRC_DIR" -name "*.hpp" -o -name "*.h" -o -name "*.cpp")
  
  if [[ "$needs_rebuild" == "true" ]]; then
    echo "[dgtl] Source changed - rebuilding $(basename "$output_path")"
    build_dgtl_binary "$output_path" "$entry_point" "$strict" "${exclude_paths[@]}" || return 1
  else
    echo "[dgtl] Reusing up-to-date $(basename "$output_path")"
  fi
}

invoke_single_backend() {
  local exe_path="$1"
  local backend="$2"
  shift 2
  local extra_args=("$@")
  
  if [[ ! -f "$exe_path" ]]; then echo "[dgtl] ERROR: Missing executable: $exe_path" >&2; return 1; fi
  
  local valid_backends=(arm64 x64 nasm wasm)
  local valid=false
  for b in "${valid_backends[@]}"; do if [[ "$backend" == "$b" ]]; then valid=true; break; fi; done
  
  if [[ $valid == false ]]; then echo "[dgtl] ERROR: Unsupported backend '$backend'" >&2; return 1; fi
  
  "$exe_path" --backend "$backend" "${extra_args[@]}" || { echo "[dgtl] ERROR: Execution failed" >&2; return 1; }
}

invoke_backend_benchmark() {
  local exe_path="$1"
  local backend="$2"
  shift 2
  local extra_args=("$@")
  
  if [[ ! -f "$exe_path" ]]; then echo "[dgtl] ERROR: Missing executable: $exe_path" >&2; return 1; fi
  local bench_args=(--backend "$backend" --benchmark "${extra_args[@]}")
  "$exe_path" "${bench_args[@]}" || { echo "[dgtl] ERROR: Benchmark failed" >&2; return 1; }
}

invoke_brun() {
  local program_args=("$@")
  local default_sources=(
    "tests/performance/benchmarks/benchmark_full_production_stress.cpp"
    "tests/performance/benchmarks/benchmark_hardest_e2e_gate.cpp"
  )

  echo "[dgtl] Using retained benchmark sources"
  local output_dir="$BUILD_DIR/brun"
  mkdir -p "$output_dir"

  local g_plusplus
  g_plusplus=$(find_g_plusplus) || return 1
  local cflags=("-std=c++26" "-O3" "-march=native" "-I$INCLUDE_DIR")

  for src in "${default_sources[@]}"; do
    if [[ ! -f "$ROOT/$src" ]]; then echo "[dgtl] ERROR: Missing benchmark $ROOT/$src" >&2; return 1; fi

    local output_path="$output_dir/$(basename "${src%.cpp}")"
    local needs_compile=true
    if [[ -f "$output_path" ]]; then
      local exe_time=$(stat -c%Y "$output_path" 2>/dev/null || stat -f%m "$output_path" 2>/dev/null)
      local src_time=$(stat -c%Y "$ROOT/$src" 2>/dev/null || stat -f%m "$ROOT/$src" 2>/dev/null)
      if [[ $src_time -le $exe_time ]]; then needs_compile=false; fi
    fi

    if [[ $needs_compile == true ]]; then
      echo "[dgtl] Compiling $(basename "$src")"
      "$g_plusplus" "${cflags[@]}" "$ROOT/$src" -o "$output_path" || return 1
    else
      echo "[dgtl] Reusing up-to-date $(basename "$output_path")"
    fi

    echo "[dgtl] Running $(basename "$output_path")"
    "$output_path" "${program_args[@]}" || return 1
  done
}

clean_dgtl_artifacts() {
  echo "[dgtl] Cleaning artifacts"
  local paths=("$BUILD_DIR/dgtl_release" "$BUILD_DIR/dgtl_main" "$BUILD_DIR/dgtl_main_strict" "$BUILD_DIR/dgtl_tests")
  for path in "${paths[@]}"; do rm -f "$path"; done
  
  local dirs=("$BUILD_DIR/sdk_obj" "$BUILD_DIR/DGTL_SDK" "$BUILD_DIR/obj" "$BUILD_DIR/obj_sdk")
  for dir in "${dirs[@]}"; do rm -rf "$dir"; done
  echo "[dgtl] Removed build outputs"
}

# ============================================================================
# COMMAND DISPATCH
# ============================================================================

command="${1:-help}"
shift || true

# Handle command aliases
case "$command" in
  build-release) command="build" ;;
  build-smoke)   command="smoke-build" ;;
  build-strict)  command="strict" ;;
  run-all)       command="run" ;;
  run-backend)   command="run" ;;
  bench-backend) command="bench" ;;
  run-tests)     command="test" ;;
  update-goldens|update-golden) command="update-golden" ;;
  run-strict-conformance)       command="srun" ;;
  clean-all)           command="clean" ;;
  clean-build-and-run) command="crun" ;;
esac

case "$command" in
  help)
    show_usage
    ;;
  gen)
    run_gen
    ;;
  build-sdk)
    build_sdk
    ;;
  build)
    build_dgtl_binary "$PROD_EXE" "$PROD_MAIN" "false" "$SMOKE_MAIN" || exit 1
    ;;
  smoke-build)
    build_dgtl_binary "$SMOKE_EXE" "$SMOKE_MAIN" "false" "$PROD_MAIN" || exit 1
    ;;
  strict)
    build_dgtl_binary "$SMOKE_STRICT_EXE" "$SMOKE_MAIN" "true" "$PROD_MAIN" || exit 1
    ;;
  run)
    backend="${1:-}"
    shift || true
    ensure_dgtl_binary "$SMOKE_EXE" "$SMOKE_MAIN" "false" "$PROD_MAIN" || exit 1
    if [[ -n "$backend" ]]; then
      invoke_single_backend "$SMOKE_EXE" "$backend" "$@" || exit 1
    else
      echo "[dgtl] Running full conformance"
      "$SMOKE_EXE" || exit 1
    fi
    ;;
  bench)
    backend="${1:-}"
    shift || true
    if [[ -z "$backend" ]]; then echo "[dgtl] ERROR: Missing backend" >&2; exit 1; fi
    ensure_dgtl_binary "$SMOKE_STRICT_EXE" "$SMOKE_MAIN" "true" "$PROD_MAIN" || exit 1
    invoke_backend_benchmark "$SMOKE_STRICT_EXE" "$backend" "$@" || exit 1
    ;;
  brun)
    invoke_brun "$@" || exit 1
    ;;
  test)
    mode="${1:-}"
    echo "[dgtl] Building test suite"
    build_dgtl_binary "$TEST_EXE" "$SMOKE_MAIN" "false" "$PROD_MAIN" || exit 1
    echo "[dgtl] Running test suite"
    "$TEST_EXE" || exit 1
    ;;
  clean)
    clean_dgtl_artifacts
    ;;
  crun)
    clean_dgtl_artifacts
    build_dgtl_binary "$SMOKE_EXE" "$SMOKE_MAIN" "false" "$PROD_MAIN" || exit 1
    echo "[dgtl] Running conformance"
    "$SMOKE_EXE" || exit 1
    ;;
  *)
    echo "[dgtl] ERROR: Unknown command '$command'" >&2
    show_usage
    exit 1
    ;;
esac