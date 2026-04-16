param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Args
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = Split-Path -Parent $MyInvocation.MyCommand.Path

function Show-Usage {
    @(
        'DGTL command runner',
        '',
        'Pick by intent:',
        '  Build release binary            dgtl build-release',
        '  Build smoke/conformance binary  dgtl build-smoke',
        '  Build strict binary             dgtl build-strict',
        '  Run full conformance            dgtl run-all',
        '  Run one backend quickly         dgtl run-backend x64',
        '  Run backend benchmark           dgtl bench-backend x64 fusion',
        '  Refresh golden baselines        dgtl update-goldens',
        '  Run strict conformance          dgtl run-strict-conformance',
        '  Run aggregate tests             dgtl test',
        '  Clean outputs/logs              dgtl clean-all',
        '  Clean + build + run             dgtl clean-build-and-run',
        '',
        'Production workflow (ship flow):',
        '  Use when: building and validating a release for deployment.',
        '  1. dgtl build-release             -> build\dgtl_release.exe (ONLY production binary)',
        '  2. dgtl run-all                   # full conformance, all 4 backends',
        '     dgtl run-strict-conformance    # strict executable gate (edge cases)',
        '     dgtl test                      # aggregate functional/robustness tests',
        '  3. dgtl bench-backend x64         # optional: timing profile before shipping',
        '  Or all-in-one: dgtl clean-build-and-run',
        '  Note: build-smoke and build-strict are NOT production builds.',
        '',
        'Polyhedral Parallelization (P0-001+):',
        '  P0 features require direct invocation with flags. Example:',
        '  .\build\dgtl_release.exe --polyhedral-mode safe --polyhedral-tile 32 --polyhedral-parallel auto',
        '  Flags:',
        '    --polyhedral-mode <off|safe|aggressive>     Control SCoP detection and transform policy',
        '    --polyhedral-tile <n>                       Tile size (0=auto)',
        '    --polyhedral-parallel <auto|off|on>         Parallel lowering policy',
        '    --backend <arm64|x64|nasm|wasm>             Target backend',
        '',
        'Categories (recommended names):',
        '',
        '1) Build binaries',
        '  Use when: you need an executable before running tests/benchmarks.',
        '  dgtl build-release                      -> build\dgtl_release.exe',
        '  dgtl build-smoke                        -> build\dgtl_main.exe',
        '  dgtl build-strict                       -> build\dgtl_main_strict.exe',
        '',
        '2) Validate correctness',
        '  Use when: you want to verify backend output against expected behavior.',
        '  dgtl run-all                            # full conformance',
        '  dgtl run-backend <arm64|x64|nasm|wasm> # quick backend check',
        '  dgtl run-strict-conformance             # strict executable gate',
        '  dgtl test [full]                       # aggregate source-backed tests',
        '',
        '3) Benchmark performance',
        '  Use when: you want opt/emit timings for one backend/profile.',
        '  dgtl bench-backend <backend> [profile|iterations] [pressure-vars] [dump]',
        '  Profiles: fold (default), fusion, simd, pressure, predication',
        '  Examples:',
        '    dgtl bench-backend x64',
        '    dgtl bench-backend x64 fusion',
        '    dgtl bench-backend x64 pressure 64',
        '    dgtl bench-backend x64 fusion dump',
        '',
        '4) Manage baselines',
        '  Use when: output changes are intentional and goldens must be updated.',
        '  dgtl update-goldens',
        '  dgtl update-goldens strict',
        '',
        '5) Clean and one-shot workflows',
        '  Use when: you want a clean state or a full clean-build-run cycle.',
        '  dgtl clean-all',
        '  dgtl clean-build-and-run',
        '',
        'Legacy short names (still supported):',
        '  help | build | smoke-build | strict | run | bench | update-golden | srun | test | clean | crun',
        '',
        'Alias mapping:',
        '  build-release -> build',
        '  build-smoke -> smoke-build',
        '  build-strict -> strict',
        '  run-all -> run',
        '  run-backend -> run <backend>',
        '  bench-backend -> bench <backend>',
        '  update-goldens -> update-golden',
        '  run-strict-conformance -> srun',
        '  run-tests -> test',
        '  clean-all -> clean',
        '  clean-build-and-run -> crun'
    ) -join [Environment]::NewLine
}

function Get-SourceFiles {
    param(
        [string]$EntryPointPath,
        [string[]]$ExcludePaths = @()
    )

    $srcDir = Join-Path $root 'src'
    $entryPointFull = [System.IO.Path]::GetFullPath($EntryPointPath)
    if (-not (Test-Path $entryPointFull)) {
        throw "Missing entrypoint source: $entryPointFull"
    }

    $excludeSet = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    [void]$excludeSet.Add($entryPointFull)
    foreach ($excludePath in $ExcludePaths) {
        [void]$excludeSet.Add([System.IO.Path]::GetFullPath($excludePath))
    }

    $files = Get-ChildItem -Path $srcDir -Recurse -File -Filter *.cpp |
        Sort-Object FullName |
        ForEach-Object { $_.FullName } |
        Where-Object { -not $excludeSet.Contains([System.IO.Path]::GetFullPath($_)) }

    if ($files.Count -eq 0) {
        throw "No source files found under $srcDir"
    }

    return @($files + $entryPointFull)
}

function Invoke-Gpp {
    param(
        [string[]]$Arguments,
        [string]$FailureMessage
    )

    $gpp = Get-Command g++ -ErrorAction SilentlyContinue
    if (-not $gpp) {
        throw 'g++ was not found in PATH.'
    }

    & $gpp.Source @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw $FailureMessage
    }
}

function Build-DgtlBinary {
    param(
        [string]$OutputPath,
        [string]$EntryPointPath,
        [string[]]$ExcludePaths = @(),
        [switch]$Strict
    )

    $buildDir = Split-Path -Parent $OutputPath
    New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

    $commonFlags = @(
        '-std=c++26',
        '-O2',
        '-I', (Join-Path $root 'include')
    )

    if ($Strict) {
        $commonFlags += @('-Wall', '-Wextra', '-Wpedantic', '-Werror')
    }

    $sourceFiles = Get-SourceFiles -EntryPointPath $EntryPointPath -ExcludePaths $ExcludePaths
    $arguments = @() + $commonFlags + $sourceFiles + @('-o', $OutputPath)

    Write-Host "[build] Compiling $(Split-Path -Leaf $OutputPath)"
    Invoke-Gpp -Arguments $arguments -FailureMessage "Compilation failed for $OutputPath"
    Write-Host "[build] Wrote $OutputPath"
}

function Ensure-DgtlBinary {
    param(
        [string]$OutputPath,
        [string]$EntryPointPath,
        [string[]]$ExcludePaths = @(),
        [switch]$Strict
    )

    if (-not (Test-Path $OutputPath)) {
        Build-DgtlBinary -OutputPath $OutputPath -EntryPointPath $EntryPointPath -ExcludePaths $ExcludePaths -Strict:$Strict
        return
    }

    $exeTime = (Get-Item $OutputPath).LastWriteTimeUtc

    $allSrc = Get-ChildItem -Path (Join-Path $root 'src') -Recurse -File | Where-Object {
        '.cpp','.hpp' -contains $_.Extension
    }
    $allInc = Get-ChildItem -Path (Join-Path $root 'include') -Recurse -File | Where-Object {
        $_.Extension -eq '.hpp'
    }

    foreach ($f in @($allSrc + $allInc)) {
        if ($f.LastWriteTimeUtc -gt $exeTime) {
            Write-Host "[build] Source changed ($($f.Name)) - rebuilding $(Split-Path -Leaf $OutputPath)"
            Build-DgtlBinary -OutputPath $OutputPath -EntryPointPath $EntryPointPath -ExcludePaths $ExcludePaths -Strict:$Strict
            return
        }
    }

    Write-Host "[build] Reusing up-to-date $(Split-Path -Leaf $OutputPath)"
}

function Build-DgtlTestBinary {
    param(
        [string]$OutputPath,
        [switch]$Full
    )

    $buildDir = Split-Path -Parent $OutputPath
    New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

    $commonFlags = @(
        '-std=c++26',
        '-O0',
        '-DDGTL_EMBED_TESTS',
        '-I', (Join-Path $root 'include')
    )

    $srcSources = Get-ChildItem -Path (Join-Path $root 'src') -Recurse -File -Filter *.cpp |
        Sort-Object FullName |
        ForEach-Object { $_.FullName } |
        Where-Object {
            $fullPath = [System.IO.Path]::GetFullPath($_)
            $fullPath -ne [System.IO.Path]::GetFullPath((Join-Path $root 'src\main.cpp')) -and
            $fullPath -ne [System.IO.Path]::GetFullPath((Join-Path $root 'src\apps\smoke_main.cpp'))
        }

    $testSources = @(
        (Join-Path $root 'tests\test_suite_main.cpp'),
        (Join-Path $root 'tests\functional\pipeline\test_pipeline1.cpp'),
        (Join-Path $root 'tests\functional\pipeline\production_arch_validator.cpp'),
        (Join-Path $root 'tests\functional\pipeline\test_pipeline_multi_target.cpp'),
        (Join-Path $root 'tests\functional\pipeline\test_release_ir_modes.cpp'),
        (Join-Path $root 'tests\functional\pipeline\test_arm64_fcvtl_widening.cpp'),
        (Join-Path $root 'tests\functional\pipeline\test_arm64_fcvtn_narrowing.cpp'),
        (Join-Path $root 'tests\functional\pipeline\test_temporal_fusion_predication.cpp'),
        (Join-Path $root 'tests\robustness\core\test_robustness_1.cpp'),
        (Join-Path $root 'tests\robustness\core\test_robustness_2.cpp'),
        (Join-Path $root 'tests\robustness\core\test_robustness_3.cpp'),
        (Join-Path $root 'tests\robustness\core\test_robustness_4.cpp'),
        (Join-Path $root 'tests\robustness\test_inliner_safety.cpp'),
        (Join-Path $root 'tests\robustness\inliner\test_speculative_inliner.cpp'),
        (Join-Path $root 'tests\performance\test_performance_mt_arena.cpp'),
        (Join-Path $root 'tests\performance\test_performance_mt_codegen.cpp'),
        (Join-Path $root 'tests\performance\stress\test_opt_strike.cpp')
    )

    foreach ($source in @($srcSources + $testSources)) {
        if (-not (Test-Path $source)) {
            throw "Missing test source: $source"
        }
    }

    $arguments = @() + $commonFlags + $srcSources + $testSources + @('-o', $OutputPath)

    Write-Host "[build] Compiling $(Split-Path -Leaf $OutputPath)"
    Invoke-Gpp -Arguments $arguments -FailureMessage "Compilation failed for $OutputPath"
    Write-Host "[build] Wrote $OutputPath"
}

function Ensure-DgtlTestBinary {
    param(
        [string]$OutputPath,
        [switch]$Full
    )

    if (-not (Test-Path $OutputPath)) {
        Build-DgtlTestBinary -OutputPath $OutputPath -Full:$Full
        return
    }

    $exeWriteTime = (Get-Item $OutputPath).LastWriteTimeUtc

    $testSourceCandidates = @(
        (Join-Path $root 'tests\test_suite_main.cpp'),
        (Join-Path $root 'tests\functional\pipeline\test_pipeline1.cpp'),
        (Join-Path $root 'tests\functional\pipeline\production_arch_validator.cpp'),
        (Join-Path $root 'tests\functional\pipeline\test_pipeline_multi_target.cpp'),
        (Join-Path $root 'tests\functional\pipeline\test_release_ir_modes.cpp'),
        (Join-Path $root 'tests\functional\pipeline\test_arm64_fcvtl_widening.cpp'),
        (Join-Path $root 'tests\functional\pipeline\test_arm64_fcvtn_narrowing.cpp'),
        (Join-Path $root 'tests\functional\pipeline\test_temporal_fusion_predication.cpp'),
        (Join-Path $root 'tests\robustness\core\test_robustness_1.cpp'),
        (Join-Path $root 'tests\robustness\core\test_robustness_2.cpp'),
        (Join-Path $root 'tests\robustness\core\test_robustness_3.cpp'),
        (Join-Path $root 'tests\robustness\core\test_robustness_4.cpp'),
        (Join-Path $root 'tests\robustness\test_inliner_safety.cpp'),
        (Join-Path $root 'tests\robustness\inliner\test_speculative_inliner.cpp'),
        (Join-Path $root 'tests\performance\test_performance_mt_arena.cpp'),
        (Join-Path $root 'tests\performance\test_performance_mt_codegen.cpp'),
        (Join-Path $root 'tests\performance\stress\test_opt_strike.cpp')
    )

    $srcSourceCandidates = Get-ChildItem -Path (Join-Path $root 'src') -Recurse -File -Filter *.cpp |
        Sort-Object FullName |
        ForEach-Object { $_.FullName } |
        Where-Object {
            $fullPath = [System.IO.Path]::GetFullPath($_)
            $fullPath -ne [System.IO.Path]::GetFullPath((Join-Path $root 'src\main.cpp')) -and
            $fullPath -ne [System.IO.Path]::GetFullPath((Join-Path $root 'src\apps\smoke_main.cpp'))
        }

    foreach ($source in @($srcSourceCandidates + $testSourceCandidates)) {
        if (-not (Test-Path $source)) {
            Build-DgtlTestBinary -OutputPath $OutputPath -Full:$Full
            return
        }

        if ((Get-Item $source).LastWriteTimeUtc -gt $exeWriteTime) {
            Build-DgtlTestBinary -OutputPath $OutputPath -Full:$Full
            return
        }
    }

    Write-Host "[build] Reusing up-to-date $(Split-Path -Leaf $OutputPath)"
}

function Invoke-DgtlTestSuite {
    param(
        [string]$ExecutablePath,
        [switch]$Full
    )

    if (-not (Test-Path $ExecutablePath)) {
        throw "Missing test executable: $ExecutablePath"
    }

    $testArgs = @()
    if ($Full) {
        $testArgs += '--full'
    }

    & $ExecutablePath @testArgs
    if ($LASTEXITCODE -ne 0) {
        throw 'DGTL test suite failed.'
    }
}

function Invoke-Conformance {
    param(
        [string]$ExecutablePath,
        [switch]$UpdateGolden
    )

    $scriptCandidates = @(
        (Join-Path $root 'docs\My_Docs\commands\backend_conformance_check.ps1'),
        (Join-Path $root 'My_Docs\commands\backend_conformance_check.ps1')
    )

    $scriptPath = $null
    foreach ($candidate in $scriptCandidates) {
        if (Test-Path $candidate) {
            $scriptPath = $candidate
            break
        }
    }

    if (-not $scriptPath) {
        throw "Missing conformance script: $($scriptCandidates[0])"
    }

    $scriptArgs = @('-ExecutionPolicy', 'Bypass', '-File', $scriptPath, '-Executable', $ExecutablePath)
    if ($UpdateGolden) {
        $scriptArgs += '-UpdateGolden'
    }

    & powershell @scriptArgs
    if ($LASTEXITCODE -ne 0) {
        if ($UpdateGolden) {
            throw 'Golden update failed.'
        }
        throw 'Backend conformance failed.'
    }
}

function Invoke-SingleBackend {
    param(
        [string]$ExecutablePath,
        [string]$Backend,
        [string[]]$ExtraArgs = @()
    )

    if (-not (Test-Path $ExecutablePath)) {
        throw "Missing executable: $ExecutablePath. Run 'dgtl build' first."
    }

    $validBackends = @('arm64', 'x64', 'nasm', 'wasm')
    if ($Backend -notin $validBackends) {
        throw "Unsupported backend '$Backend'. Expected one of: $($validBackends -join ', ')"
    }

    $cmdArgs = @('--backend', $Backend)
    if ($null -ne $ExtraArgs -and $ExtraArgs.Count -gt 0) {
        $cmdArgs += $ExtraArgs
    }

    & $ExecutablePath @cmdArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Execution failed for backend '$Backend'."
    }
}

function Invoke-BackendBenchmark {
    param(
        [string]$ExecutablePath,
        [string]$Backend,
        [string]$ProfileOrIterations,
        [string]$PressureVars,
        [switch]$DumpBenchmark,
        [string[]]$PassThroughArgs = @()
    )

    if (-not (Test-Path $ExecutablePath)) {
        throw "Missing executable: $ExecutablePath. Run 'dgtl build' or 'dgtl strict' first."
    }

    $validBackends = @('arm64', 'x64', 'nasm', 'wasm')
    if ($Backend -notin $validBackends) {
        throw "Unsupported backend '$Backend'. Expected one of: $($validBackends -join ', ')"
    }

    $cmdArgs = @('--backend', $Backend, '--benchmark')

    if ($null -ne $ProfileOrIterations -and $ProfileOrIterations -ne '') {
        $validProfiles = @('fold', 'fusion', 'simd', 'pressure', 'predication')
        if ($ProfileOrIterations -in $validProfiles) {
            $cmdArgs += @('--benchmark-profile', $ProfileOrIterations)
            if ($ProfileOrIterations -eq 'pressure' -and $null -ne $PressureVars -and $PressureVars -ne '') {
                [void][UInt64]::Parse($PressureVars)
                $cmdArgs += @('--pressure-vars', $PressureVars)
            }
        } else {
            [void][UInt64]::Parse($ProfileOrIterations)
            $cmdArgs += @('--benchmark-iterations', $ProfileOrIterations)
        }
    }

    if ($DumpBenchmark) {
        $cmdArgs += '--benchmark-dump'
    }

    if ($null -ne $PassThroughArgs -and $PassThroughArgs.Count -gt 0) {
        $cmdArgs += $PassThroughArgs
    }

    & $ExecutablePath @cmdArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Benchmark failed for backend '$Backend'."
    }
}

function Clean-DgtlArtifacts {
    $paths = @(
        (Join-Path $root 'build\dgtl_release.exe'),
        (Join-Path $root 'build\dgtl_main.exe'),
        (Join-Path $root 'build\dgtl_main_strict.exe'),
        (Join-Path $root 'build\dgtl_tests.exe'),
        (Join-Path $root 'build\main_release_smoke.exe'),
        (Join-Path $root 'build\production_arch_validator_embedded.o'),
        (Join-Path $root 'build\run_test_pipeline1_main.cpp'),
        (Join-Path $root 'build\run_test_pipeline1_main.o'),
        (Join-Path $root 'build\test_pipeline1.o'),
        (Join-Path $root 'dgtl_main.exe'),
        (Join-Path $root 'build\logs\conformance_arm64.log'),
        (Join-Path $root 'build\logs\conformance_x64.log'),
        (Join-Path $root 'build\logs\conformance_nasm.log'),
        (Join-Path $root 'build\logs\conformance_wasm.log'),
        (Join-Path $root 'build\logs\conformance_pass.cache'),
        (Join-Path $root 'build\logs\conformance_dgtl_main.cache'),
        (Join-Path $root 'build\logs\conformance_dgtl_main_strict.cache')
    )

    foreach ($path in $paths) {
        if (Test-Path $path) {
            Remove-Item -Force $path
        }
    }

    Write-Host '[clean] Removed DGTL build outputs and conformance logs'
}

[string[]]$allArgs = @()
if ($null -ne $Args) {
    if ($Args -is [System.Array]) {
        $allArgs = [string[]]$Args
    } else {
        $allArgs = @([string]$Args)
    }
}

$command = if ($allArgs.Count -gt 0 -and $null -ne $allArgs[0] -and $allArgs[0] -ne '') {
    $allArgs[0].ToLowerInvariant()
} else {
    'help'
}
$remainingArgs = @()
if ($allArgs.Count -gt 1) {
    $remainingArgs = @($allArgs[1..($allArgs.Count - 1)])
}

$modeArg = if ($remainingArgs.Count -gt 0 -and $null -ne $remainingArgs[0] -and $remainingArgs[0] -ne '') {
    [string]$remainingArgs[0]
} else {
    $null
}

$extraArgs = @()
if ($remainingArgs.Count -gt 1) {
    $extraArgs = @($remainingArgs[1..($remainingArgs.Count - 1)])
}

# Support descriptive aliases while keeping existing short commands stable.
switch ($command) {
    'build-release' { $command = 'build' }
    'build-smoke' { $command = 'smoke-build' }
    'build-strict' { $command = 'strict' }
    'run-all' { $command = 'run' }
    'run-tests' { $command = 'test' }
    'run-backend' {
        if ($null -eq $modeArg -or $modeArg -eq '') {
            throw "Missing backend for run-backend. Use 'dgtl run-backend arm64' or similar."
        }
        $command = 'run'
    }
    'bench-backend' {
        if ($null -eq $modeArg -or $modeArg -eq '') {
            throw "Missing backend for bench-backend. Use 'dgtl bench-backend x64' or similar."
        }
        $command = 'bench'
    }
    'update-goldens' { $command = 'update-golden' }
    'run-strict-conformance' { $command = 'srun' }
    'clean-all' { $command = 'clean' }
    'clean-build-and-run' { $command = 'crun' }
}

$prodMain = Join-Path $root 'src\main.cpp'
$smokeMain = Join-Path $root 'src\apps\smoke_main.cpp'

$prodExe = Join-Path $root 'build\dgtl_release.exe'
$smokeExe = Join-Path $root 'build\dgtl_main.exe'
$smokeStrictExe = Join-Path $root 'build\dgtl_main_strict.exe'
$testExe = Join-Path $root 'build\dgtl_tests.exe'

switch ($command) {
    'help' {
        Show-Usage
    }
    'build' {
        Build-DgtlBinary -OutputPath $prodExe -EntryPointPath $prodMain -ExcludePaths @($smokeMain)
    }
    'smoke-build' {
        Build-DgtlBinary -OutputPath $smokeExe -EntryPointPath $smokeMain -ExcludePaths @($prodMain)
    }
    'strict' {
        Build-DgtlBinary -OutputPath $smokeStrictExe -EntryPointPath $smokeMain -ExcludePaths @($prodMain) -Strict
    }
    'run' {
        Ensure-DgtlBinary -OutputPath $smokeExe -EntryPointPath $smokeMain -ExcludePaths @($prodMain)
        if ($null -ne $modeArg) {
            Invoke-SingleBackend -ExecutablePath $smokeExe -Backend $modeArg.ToLowerInvariant() -ExtraArgs $extraArgs
        } else {
            if ($extraArgs.Count -gt 0) {
                throw "Extra arguments are not supported for full conformance run. Use 'dgtl run-backend <backend> ...'."
            }
            Invoke-Conformance -ExecutablePath $smokeExe
        }
    }
    'bench' {
        if ($null -eq $modeArg) {
            throw "Missing backend for bench. Use 'dgtl bench x64' or similar."
        }
        Ensure-DgtlBinary -OutputPath $smokeStrictExe -EntryPointPath $smokeMain -ExcludePaths @($prodMain) -Strict

        $benchExtras = @($extraArgs)
        $dumpRequested = $false

        $positionalExtras = @()
        $passThroughArgs = @()
        for ($i = 0; $i -lt $benchExtras.Count; ++$i) {
            $arg = [string]$benchExtras[$i]
            $argLower = $arg.ToLowerInvariant()

            if ($argLower -eq 'dump') {
                $dumpRequested = $true
                continue
            }

            if ($argLower.StartsWith('--polyhedral-')) {
                $passThroughArgs += $arg
                if (($argLower -eq '--polyhedral-mode' -or
                     $argLower -eq '--polyhedral-tile' -or
                     $argLower -eq '--polyhedral-parallel') -and
                    $i + 1 -lt $benchExtras.Count) {
                    $next = [string]$benchExtras[$i + 1]
                    if (-not $next.StartsWith('--')) {
                        $passThroughArgs += $next
                        ++$i
                    }
                }
                continue
            }

            if ($arg.StartsWith('--')) {
                throw "Unsupported bench flag '$arg'."
            }

            $positionalExtras += $arg
        }

        if ($positionalExtras.Count -gt 2) {
            throw "Too many bench arguments. Use: dgtl bench <backend> [profile|iterations] [pressure-vars] [dump]"
        }

        $profileOrIterations = if ($positionalExtras.Count -ge 1) { [string]$positionalExtras[0] } else { $null }
        $pressureVars = if ($positionalExtras.Count -ge 2) { [string]$positionalExtras[1] } else { $null }

        Invoke-BackendBenchmark -ExecutablePath $smokeStrictExe `
            -Backend $modeArg.ToLowerInvariant() `
            -ProfileOrIterations $profileOrIterations `
            -PressureVars $pressureVars `
            -DumpBenchmark:$dumpRequested `
            -PassThroughArgs $passThroughArgs
    }
    'update-golden' {
        if ($null -ne $modeArg -and $modeArg.ToLowerInvariant() -eq 'strict') {
            Ensure-DgtlBinary -OutputPath $smokeStrictExe -EntryPointPath $smokeMain -ExcludePaths @($prodMain) -Strict
            Invoke-Conformance -ExecutablePath $smokeStrictExe -UpdateGolden
        } elseif ($null -ne $modeArg) {
            throw "Unsupported update-golden mode '$modeArg'. Use 'dgtl update-golden' or 'dgtl update-golden strict'."
        } else {
            Ensure-DgtlBinary -OutputPath $smokeExe -EntryPointPath $smokeMain -ExcludePaths @($prodMain)
            Invoke-Conformance -ExecutablePath $smokeExe -UpdateGolden
        }
    }
    'srun' {
        Ensure-DgtlBinary -OutputPath $smokeStrictExe -EntryPointPath $smokeMain -ExcludePaths @($prodMain) -Strict
        Invoke-Conformance -ExecutablePath $smokeStrictExe
    }
    'test' {
        if ($null -ne $modeArg -and $modeArg.ToLowerInvariant() -ne 'full') {
            throw "Unsupported test mode '$modeArg'. Use 'dgtl test' or 'dgtl test full'."
        }

        $runFullSuite = $null -ne $modeArg -and $modeArg.ToLowerInvariant() -eq 'full'
        Ensure-DgtlTestBinary -OutputPath $testExe -Full:$runFullSuite
        Invoke-DgtlTestSuite -ExecutablePath $testExe -Full:$runFullSuite
    }
    'clean' {
        Clean-DgtlArtifacts
    }
    'crun' {
        Clean-DgtlArtifacts
        Build-DgtlBinary -OutputPath $smokeExe -EntryPointPath $smokeMain -ExcludePaths @($prodMain)
        Invoke-Conformance -ExecutablePath $smokeExe
    }
    default {
        throw "Unknown command '$command'. Run 'dgtl help' for usage."
    }
}