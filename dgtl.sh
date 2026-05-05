#!/bin/bash
###############################################################################
# DGTL Command Runner - Cross-Platform Build & Execution
# Bash equivalent of dgtl.ps1 for Linux/macOS
###############################################################################

set -o pipefail

# ============================================================================
# DETECT PLATFORM & SET CONDITIONALS
# ============================================================================

case "$OSTYPE" in
  linux*)
    OS="Linux"
    ;;
  darwin*)
    OS="macOS"
    ;;
  msys|cygwin|win32)
    OS="Windows"
    ;;
  *)
    OS="Unknown"
    ;;
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
  Build release binary            dgtl build-release
  Build smoke/conformance binary  dgtl build-smoke
  Build strict binary             dgtl build-strict
  Run full conformance            dgtl run-all
  Run one backend quickly         dgtl run-backend x64
  Run backend benchmark           dgtl bench-backend x64 fusion
  Build and run one source        dgtl brun tests/performance/foo.cpp
  Clean outputs/logs              dgtl clean-all
  Clean + build + run             dgtl clean-build-and-run

Categories (recommended):

1) Build binaries
  dgtl build-release                      -> build/dgtl_release
  dgtl build-smoke                        -> build/dgtl_main
  dgtl build-strict                       -> build/dgtl_main_strict

2) Validate correctness
  dgtl run-all                            # full conformance
  dgtl run-backend <arm64|x64|nasm|wasm> # quick backend check
  dgtl test [full]                        # aggregate source-backed tests

3) Benchmark performance
  dgtl bench-backend <backend> [profile|iterations] [pressure-vars]
  dgtl brun <path-to-cpp> [program args...]

4) Clean and one-shot workflows
  dgtl clean-all
  dgtl clean-build-and-run

Legacy short names (still supported):
  help | build | smoke-build | strict | run | bench | brun | test | clean | crun

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
  local candidates=(
    "g++"
    "g++-12"
    "g++-11"
    "clang++"
    "c++"
  )
  
  for candidate in "${candidates[@]}"; do
    if command -v "$candidate" &>/dev/null; then
      # Exclude clang if we haven't found gcc yet
      if [[ "$candidate" != "clang"* ]]; then
        echo "$candidate"
        return 0
      fi
    fi
  done
  
  # Last resort: clang
  if command -v clang++ &>/dev/null; then
    echo "clang++"
    return 0
  fi
  
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
  
  # Find all .cpp files except the excluded ones
  local all_sources
  all_sources=$(find "$SRC_DIR" -name "*.cpp" -type f | sort)
  
  local sources=()
  while IFS= read -r src; do
    local skip=false
    for exclude in "${exclude_paths[@]}"; do
      if [[ "$src" == "$exclude" ]]; then
        skip=true
        break
      fi
    done
    [[ $skip == false ]] && sources+=("$src")
  done <<< "$all_sources"
  
  # Add entry point at end
  sources+=("$entry_point")
  
  if [[ ${#sources[@]} -eq 0 ]]; then
    echo "[dgtl] ERROR: No source files found under $SRC_DIR" >&2
    return 1
  fi
  
  printf '%s\n' "${sources[@]}"
}

build_dgtl_binary() {
  local output_path="$1"
  local entry_point="$2"
  local strict="${3:-false}"
  shift 3
  local exclude_paths=("$@")
  
  mkdir -p "$(dirname "$output_path")"
  
  local g_plusplus
  g_plusplus=$(find_g_plusplus) || {
    echo "[dgtl] ERROR: g++ not found in PATH" >&2
    return 1
  }
  
  echo "[dgtl] Using compiler: $g_plusplus"
  
  # Common flags
  local cflags=(
    "-std=c++23"
    "-O2"
    "-Wall"
    "-Wextra"
    "-I$INCLUDE_DIR"
  )
  
  if [[ "$strict" == "true" ]]; then
    cflags+=("-Wpedantic" "-Werror")
  fi
  
  # Get source files
  local sources
  sources=$(get_source_files "$entry_point" "${exclude_paths[@]}") || return 1
  
  echo "[dgtl] Compiling $(basename "$output_path")"
  
  # Convert source list to array
  local source_array=()
  while IFS= read -r src; do
    source_array+=("$src")
  done <<< "$sources"
  
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
  
  # Check if sources are newer than executable
  local exe_time
  exe_time=$(stat -c%Y "$output_path" 2>/dev/null || stat -f%m "$output_path" 2>/dev/null)
  
  local needs_rebuild=false
  while IFS= read -r src; do
    local src_time
    src_time=$(stat -c%Y "$src" 2>/dev/null || stat -f%m "$src" 2>/dev/null)
    if [[ $src_time -gt $exe_time ]]; then
      needs_rebuild=true
      break
    fi
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
  
  if [[ ! -f "$exe_path" ]]; then
    echo "[dgtl] ERROR: Missing executable: $exe_path. Run 'dgtl build' first." >&2
    return 1
  fi
  
  local valid_backends=(arm64 x64 nasm wasm)
  local valid=false
  for b in "${valid_backends[@]}"; do
    if [[ "$backend" == "$b" ]]; then
      valid=true
      break
    fi
  done
  
  if [[ $valid == false ]]; then
    echo "[dgtl] ERROR: Unsupported backend '$backend'. Expected one of: ${valid_backends[*]}" >&2
    return 1
  fi
  
  "$exe_path" --backend "$backend" "${extra_args[@]}" || {
    echo "[dgtl] ERROR: Execution failed for backend '$backend'" >&2
    return 1
  }
}

invoke_backend_benchmark() {
  local exe_path="$1"
  local backend="$2"
  shift 2
  local extra_args=("$@")
  
  if [[ ! -f "$exe_path" ]]; then
    echo "[dgtl] ERROR: Missing executable: $exe_path. Run 'dgtl strict' first." >&2
    return 1
  fi
  
  local valid_backends=(arm64 x64 nasm wasm)
  local valid=false
  for b in "${valid_backends[@]}"; do
    if [[ "$backend" == "$b" ]]; then
      valid=true
      break
    fi
  done
  
  if [[ $valid == false ]]; then
    echo "[dgtl] ERROR: Unsupported backend '$backend'" >&2
    return 1
  fi
  
  local bench_args=(--backend "$backend" --benchmark)
  bench_args+=("${extra_args[@]}")
  
  "$exe_path" "${bench_args[@]}" || {
    echo "[dgtl] ERROR: Benchmark failed for backend '$backend'" >&2
    return 1
  }
}

invoke_brun() {
  local source_path="${1:-}"
  shift || true
  local program_args=("$@")
  
  if [[ -z "$source_path" ]]; then
    echo "[dgtl] Using default performance sources"
    local default_sources=(
      "tests/performance/benchmark_hardest_e2e_gate.cpp"
      "tests/performance/benchmark_production.cpp"
    )
    
    for src in "${default_sources[@]}"; do
      if [[ -f "$ROOT/$src" ]]; then
        invoke_brun "$ROOT/$src" || return 1
      fi
    done
    return 0
  fi
  
  # Resolve source path
  local resolved_source
  if [[ "$source_path" = /* ]]; then
    resolved_source="$source_path"
  else
    resolved_source="$ROOT/$source_path"
  fi
  
  if [[ ! -f "$resolved_source" ]]; then
    echo "[dgtl] ERROR: Missing source file: $resolved_source" >&2
    return 1
  fi
  
  # Determine output path
  local output_dir="$BUILD_DIR/brun"
  mkdir -p "$output_dir"
  
  local output_path="$output_dir/$(basename "${resolved_source%.cpp}")"
  
  # Compile with optimizations
  local g_plusplus
  g_plusplus=$(find_g_plusplus) || {
    echo "[dgtl] ERROR: g++ not found" >&2
    return 1
  }
  
  local cflags=(
    "-std=c++23"
    "-O3"
    "-march=native"
    "-I$INCLUDE_DIR"
  )
  
  local needs_compile=true
  if [[ -f "$output_path" ]]; then
    local exe_time
    exe_time=$(stat -c%Y "$output_path" 2>/dev/null || stat -f%m "$output_path" 2>/dev/null)
    local src_time
    src_time=$(stat -c%Y "$resolved_source" 2>/dev/null || stat -f%m "$resolved_source" 2>/dev/null)
    
    if [[ $src_time -le $exe_time ]]; then
      needs_compile=false
    fi
  fi
  
  if [[ $needs_compile == true ]]; then
    echo "[dgtl] Compiling $(basename "$resolved_source")"
    "$g_plusplus" "${cflags[@]}" "$resolved_source" -o "$output_path" || {
      echo "[dgtl] ERROR: Compilation failed for $resolved_source" >&2
      return 1
    }
  else
    echo "[dgtl] Reusing up-to-date $(basename "$output_path")"
  fi
  
  echo "[dgtl] Running $(basename "$output_path")"
  "$output_path" "${program_args[@]}" || {
    echo "[dgtl] ERROR: Execution failed" >&2
    return 1
  }
}

clean_dgtl_artifacts() {
  echo "[dgtl] Cleaning artifacts"
  
  local paths=(
    "$BUILD_DIR/dgtl_release"
    "$BUILD_DIR/dgtl_main"
    "$BUILD_DIR/dgtl_main_strict"
    "$BUILD_DIR/dgtl_tests"
  )
  
  for path in "${paths[@]}"; do
    rm -f "$path"
  done
  
  local dirs=(
    "$BUILD_DIR/sdk_obj"
    "$BUILD_DIR/DGTL_SDK"
    "$BUILD_DIR/obj"
  )
  
  for dir in "${dirs[@]}"; do
    rm -rf "$dir"
  done
  
  echo "[dgtl] Removed build outputs"
}

# ============================================================================
# COMMAND DISPATCH
# ============================================================================

command="${1:-help}"
shift || true

# Handle command aliases
case "$command" in
  build-release)
    command="build"
    ;;
  build-smoke)
    command="smoke-build"
    ;;
  build-strict)
    command="strict"
    ;;
  run-all)
    command="run"
    ;;
  run-backend)
    command="run"
    ;;
  bench-backend)
    command="bench"
    ;;
  run-tests)
    command="test"
    ;;
  update-goldens|update-golden)
    command="update-golden"
    ;;
  run-strict-conformance)
    command="srun"
    ;;
  clean-all)
    command="clean"
    ;;
  clean-build-and-run)
    command="crun"
    ;;
esac

case "$command" in
  help)
    show_usage
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
    if [[ -z "$backend" ]]; then
      echo "[dgtl] ERROR: Missing backend for bench" >&2
      exit 1
    fi
    ensure_dgtl_binary "$SMOKE_STRICT_EXE" "$SMOKE_MAIN" "true" "$PROD_MAIN" || exit 1
    invoke_backend_benchmark "$SMOKE_STRICT_EXE" "$backend" "$@" || exit 1
    ;;
  brun)
    source_path="${1:-}"
    shift || true
    invoke_brun "$source_path" "$@" || exit 1
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
