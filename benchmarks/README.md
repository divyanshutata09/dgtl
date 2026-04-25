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

# DGTL Benchmark Proof-Of-Life Artifacts

This directory stores publishable, reproducible output logs for backend runs under the proprietary release model.

## Included Logs

- `logs/release_arm64_int.log`
- `logs/release_x64_int.log`
- `logs/release_nasm_int.log`
- `logs/release_wasm_int.log`

These logs capture:

- backend identity
- IR mode and polyhedral mode
- logic hash markers
- emitted output excerpt (backend dependent)

## Object & Library Emission Artifacts

Native object and library outputs are generated under `build/out/` and are gitignored (not committed). Verify with:

```powershell
.\build\dgtl_release.exe --backend elfobj --emit-output build\out\kernel.o
.\build\dgtl_release.exe --backend staticlib --emit-output build\out\kernel.a
.\build\dgtl_release.exe --backend coffobj --emit-output build\out\kernel.obj
.\build\dgtl_release.exe --backend cofflib --emit-output build\out\kernel.lib
.\build\dgtl_release.exe --backend macho --emit-output build\out\kernel.macho
```

## JIT Execution Proof

```powershell
$env:DGTL_STRIKE_EXECUTE="1"
.\build\dgtl_release.exe --backend x64 --jit-execute
$env:DGTL_STRIKE_EXECUTE=""
```

Expected: `[DGTL-JIT] Strike OK status=0x0 executed_instructions=17 code_size=17`

## Regenerate

Run from repository root:

```powershell
$backends = @('arm64','x64','nasm','wasm')
foreach ($backend in $backends) {
  $path = "benchmarks/logs/release_${backend}_int.log"
  $txt = (& .\build\dgtl_release.exe --backend $backend --ir-mode int 2>&1 | Out-String)
  Set-Content -Path $path -Value $txt -Encoding utf8
}
```

For object/library artifacts:

```powershell
$targets = @(
  @{backend='elfobj'; out='build/out/kernel.o'},
  @{backend='staticlib'; out='build/out/kernel.a'},
  @{backend='coffobj'; out='build/out/kernel.obj'},
  @{backend='cofflib'; out='build/out/kernel.lib'},
  @{backend='macho'; out='build/out/kernel.macho'}
)
foreach ($t in $targets) {
  & .\build\dgtl_release.exe --backend $t.backend --emit-output $t.out
}
```

## Notes

- Build artifacts remain ignored; only logs/docs are intended for publication.
- For release posts, pair these logs with benchmark summaries and methodology notes.
