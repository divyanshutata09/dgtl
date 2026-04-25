param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Args
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$script:ResolvedGppPath = $null
$script:LatestWriteCache = @{}

function Get-LatestWriteTimeUtc {
    param(
        [string[]]$Paths,
        [string[]]$Extensions = @(),
        [string[]]$ExplicitFiles = @()
    )

    $latest = [DateTime]::MinValue

    foreach ($file in $ExplicitFiles) {
        if (-not (Test-Path $file)) {
            continue
        }
        $t = (Get-Item $file).LastWriteTimeUtc
        if ($t -gt $latest) {
            $latest = $t
        }
    }

    foreach ($path in $Paths) {
        if (-not (Test-Path $path)) {
            continue
        }

        $items = Get-ChildItem -Path $path -Recurse -File
        foreach ($item in $items) {
            if ($Extensions.Count -gt 0 -and $Extensions -notcontains $item.Extension) {
                continue
            }
            if ($item.LastWriteTimeUtc -gt $latest) {
                $latest = $item.LastWriteTimeUtc
            }
        }
    }

    return $latest
}

function Get-LatestWriteTimeUtcCached {
    param(
        [string]$CacheKey,
        [string[]]$Paths,
        [string[]]$Extensions = @(),
        [string[]]$ExplicitFiles = @()
    )

    if ($script:LatestWriteCache.ContainsKey($CacheKey)) {
        return [DateTime]$script:LatestWriteCache[$CacheKey]
    }

    $latest = Get-LatestWriteTimeUtc -Paths $Paths -Extensions $Extensions -ExplicitFiles $ExplicitFiles
    $script:LatestWriteCache[$CacheKey] = $latest
    return $latest
}

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
        '  Build and run one source        dgtl brun tests/performance/foo.cpp',
        '  Refresh golden baselines        dgtl update-goldens',
        '  Run strict conformance          dgtl run-strict-conformance',
        '  Run aggregate tests             dgtl test',
        '  Verify proprietary headers      dgtl header-check',
        '  Package SDK distribution        dgtl package',
        '  Clean outputs/logs              dgtl clean-all',
        '  Clean + build + run             dgtl clean-build-and-run',
        '',
        'Production workflow (ship flow):',
        '  Use when: building and validating a release for deployment.',
        '  1. dgtl build-release             -> build\dgtl_release.exe (ONLY production binary)',
        '  2. dgtl test full                 # aggregate functional/robustness tests',
        '  3. dgtl package                   # build static lib + SDK zip',
        '  Or all-in-one: dgtl package       (runs test full first, then archives SDK)',
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
        '  dgtl test [full]                        # aggregate source-backed tests',
        '  dgtl header-check                       # proprietary header compliance gate',
        '',
        '3) Benchmark performance',
        '  Use when: you want opt/emit timings for one backend/profile.',
        '  dgtl bench-backend <backend> [profile|iterations] [pressure-vars] [dump]',
        '  dgtl brun <path-to-cpp> [program args...]',
        '  dgtl brun                                # runs default performance benchmark suite',
        '  dgtl brun tests/performance [--mode elite|universal] [program args...]',
        '    Runs every .cpp under tests/performance and saves logs to benchmarks/logs/brun/',
        '  dgtl brun <path-to-cpp> [--mode elite|universal] [program args...]',
            '    --mode elite       Enforce 0.10% CPN drift gate, large pages + core affinity mandatory',
            '    --mode universal   5.00% CPN drift gate, standard environment (default)',
        '  Profiles: fold (default), fusion, simd, pressure, predication',
        '  ARM64 binary A/B: append --arm64-binary-stream',
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
        '  help | build | smoke-build | strict | run | bench | brun | update-golden | srun | test | header-check | clean | crun',
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
        '  check-headers -> header-check',
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

    $gppPath = Resolve-GppPath
    $tmpBase = Join-Path $env:TEMP ("dgtl-gpp-" + [guid]::NewGuid().ToString('N'))
    $stdoutPath = $tmpBase + '.stdout.log'
    $stderrPath = $tmpBase + '.stderr.log'

    try {
        $proc = Start-Process -FilePath $gppPath `
            -ArgumentList $Arguments `
            -NoNewWindow `
            -Wait `
            -PassThru `
            -RedirectStandardOutput $stdoutPath `
            -RedirectStandardError $stderrPath

        if (Test-Path $stdoutPath) {
            Get-Content $stdoutPath | ForEach-Object { Write-Host $_ }
        }
        if (Test-Path $stderrPath) {
            Get-Content $stderrPath | ForEach-Object { Write-Host $_ }
        }

        $exitCode = $proc.ExitCode
    } finally {
        if (Test-Path $stdoutPath) { Remove-Item $stdoutPath -Force -ErrorAction SilentlyContinue }
        if (Test-Path $stderrPath) { Remove-Item $stderrPath -Force -ErrorAction SilentlyContinue }
    }

    if ($exitCode -ne 0) {
        throw $FailureMessage
    }
}

function Resolve-GppPath {
    $resolvedVar = Get-Variable -Name ResolvedGppPath -Scope Script -ErrorAction SilentlyContinue
    if ($null -ne $resolvedVar -and $null -ne $resolvedVar.Value -and $resolvedVar.Value -ne '') {
        return [string]$resolvedVar.Value
    }

    $candidates = @()

    # Prefer workspace toolchain config first.
    $vscodeConfig = Join-Path $root '.vscode\c_cpp_properties.json'
    if (Test-Path $vscodeConfig) {
        try {
            $json = Get-Content -Path $vscodeConfig -Raw | ConvertFrom-Json
            foreach ($cfg in @($json.configurations)) {
                $compilerPath = $cfg.compilerPath
                if ($null -ne $compilerPath -and $compilerPath -ne '') {
                    $resolved = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath([string]$compilerPath)
                    $candidates += $resolved
                }
            }
        } catch {
            Write-Host "[build] Warning: failed to parse .vscode/c_cpp_properties.json ($($_.Exception.Message))"
        }
    }

    # Known preferred GCC path for UCRT64 setups on Windows.
    $candidates += @(
        'C:\msys64\ucrt64\bin\g++.exe',
        'C:\msys64\mingw64\bin\g++.exe'
    )

    # Fallback to PATH resolution.
    $gpp = Get-Command g++ -ErrorAction SilentlyContinue
    if ($gpp) {
        $candidates += $gpp.Source
    }

    foreach ($candidate in $candidates | Select-Object -Unique) {
        if (-not (Test-Path $candidate)) {
            continue
        }

        $normalized = [System.IO.Path]::GetFullPath($candidate)
        if ($normalized -match 'clang|llvm') {
            continue
        }

        $script:ResolvedGppPath = $normalized
        Write-Host "[build] Using compiler: $script:ResolvedGppPath"
        return $script:ResolvedGppPath
    }

    throw 'g++ was not found. Ensure a GCC g++ toolchain is installed and available (for example C:\msys64\ucrt64\bin\g++.exe).'
}

function Resolve-ToolchainBinary {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ToolName
    )

    $gppPath = Resolve-GppPath
    $gppDir = Split-Path -Parent $gppPath
    $toolPath = Join-Path $gppDir "${ToolName}.exe"
    if (Test-Path $toolPath) {
        return $toolPath
    }

    $toolCmd = Get-Command $ToolName -ErrorAction SilentlyContinue
    if ($toolCmd) {
        return $toolCmd.Source
    }

    return $null
}

function Invoke-HeaderCompliance {
    $headerScript = Join-Path $root 'tools\scripts\verify_proprietary_headers.ps1'
    if (-not (Test-Path $headerScript)) {
        throw "Missing proprietary header verification script: $headerScript"
    }

    & powershell -ExecutionPolicy Bypass -File $headerScript -SummaryOnly -RelativePaths
    if ($LASTEXITCODE -ne 0) {
        throw 'Proprietary header compliance check failed.'
    }

    Write-Host '[compliance] Proprietary header check passed'
}

function Invoke-ApiPolicyGates {
    $policyScript = Join-Path $root 'tools\scripts\verify_public_api_policy.ps1'
    if (-not (Test-Path $policyScript)) {
        throw "Missing public API policy verification script: $policyScript"
    }

    & powershell -ExecutionPolicy Bypass -File $policyScript -SummaryOnly
    if ($LASTEXITCODE -ne 0) {
        throw 'Public API policy gates failed.'
    }

    Write-Host '[api-policy] Public API policy gates passed'
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

function Invoke-Brun {
    param(
        [string]$SourcePath,
        [string[]]$ProgramArgs = @(),
        [string]$Mode = ''
    )

    if ($null -eq $SourcePath -or $SourcePath -eq '') {
        throw "Missing source file for brun. Use 'dgtl brun tests/performance/foo.cpp'."
    }

    $resolvedSource = if ([System.IO.Path]::IsPathRooted($SourcePath)) {
        $SourcePath
    } else {
        Join-Path $root $SourcePath
    }
    $resolvedSource = [System.IO.Path]::GetFullPath($resolvedSource)

    if (-not (Test-Path $resolvedSource)) {
        throw "Missing source file or directory: $resolvedSource"
    }

    if (Test-Path $resolvedSource -PathType Container) {
        $cppSources = Get-ChildItem -Path $resolvedSource -Recurse -File -Filter *.cpp |
            Sort-Object FullName |
            ForEach-Object { $_.FullName }

        if ($cppSources.Count -eq 0) {
            throw "No .cpp files found under $resolvedSource"
        }

        Write-Host "[brun] Found $($cppSources.Count) source files under $resolvedSource"
        $failedRuns = @()
        foreach ($cppSource in $cppSources) {
            $exitCode = Invoke-Brun -SourcePath $cppSource -ProgramArgs $ProgramArgs -Mode $Mode
            if ($null -eq $exitCode) {
                $exitCode = 0
            }
            if ($exitCode -ne 0) {
                $failedRuns += "$(Split-Path -Leaf $cppSource) (exit=$exitCode)"
            }
        }

        if ($failedRuns.Count -gt 0) {
            Write-Host "[brun] Failed runs:"
            foreach ($failedRun in $failedRuns) {
                Write-Host "  - $failedRun"
            }
            return 1
        }

        Write-Host "[brun] All runs completed successfully"
        return 0
    }

    if ([System.IO.Path]::GetExtension($resolvedSource) -ne '.cpp') {
        throw "brun only supports .cpp sources or directories: $resolvedSource"
    }

    $outputDir = Join-Path $root 'build\brun'
    New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
    $benchmarkLogDir = Join-Path $root 'benchmarks\logs\brun'
    New-Item -ItemType Directory -Force -Path $benchmarkLogDir | Out-Null

    $outputPath = Join-Path $outputDir (([System.IO.Path]::GetFileNameWithoutExtension($resolvedSource)) + '.exe')
    $normalizedRoot = [System.IO.Path]::GetFullPath($root)
    if (-not $normalizedRoot.EndsWith([System.IO.Path]::DirectorySeparatorChar)) {
        $normalizedRoot += [System.IO.Path]::DirectorySeparatorChar
    }
    $isPerformanceSource = $resolvedSource.StartsWith($normalizedRoot + 'tests\performance', [System.StringComparison]::OrdinalIgnoreCase) -or
        $resolvedSource.StartsWith((Join-Path $root 'tests\performance'), [System.StringComparison]::OrdinalIgnoreCase)
    $arguments = @(
        '-std=c++26',
        '-O3',
        '-I', (Join-Path $root 'include'),
        $resolvedSource
    )

    if ($isPerformanceSource) {
        $arguments += @(
            '-march=native',
            '-mtune=native',
            '-flto=auto',
            '-flto-partition=none'
        )
    }

    $arguments += @('-o', $outputPath)

    $needsCompile = $true
    if (Test-Path $outputPath) {
        $exeWriteTime = (Get-Item $outputPath).LastWriteTimeUtc
        $sourceWriteTime = (Get-Item $resolvedSource).LastWriteTimeUtc
        $includeWriteTime = Get-LatestWriteTimeUtcCached `
            -CacheKey 'brun:include:headers' `
            -Paths @((Join-Path $root 'include')) `
            -Extensions @('.hpp', '.h', '.hh', '.hxx', '.inl')

        if ($sourceWriteTime -le $exeWriteTime -and $includeWriteTime -le $exeWriteTime) {
            $needsCompile = $false
        }
    }

    if ($needsCompile) {
        Write-Host "[brun] Compiling $(Split-Path -Leaf $resolvedSource)"
        Invoke-Gpp -Arguments $arguments -FailureMessage "Compilation failed for $resolvedSource"
    } else {
        Write-Host "[brun] Reusing up-to-date $(Split-Path -Leaf $outputPath)"
    }
    Write-Host "[brun] Running $(Split-Path -Leaf $outputPath)"

    $runArgs = @()
    if ($null -ne $Mode -and $Mode -ne '') {
        $runArgs += @('--mode', $Mode)
    }
    $runArgs += $ProgramArgs
    $runArgs = @($runArgs | Where-Object { $null -ne $_ -and $_ -ne '' })

    $modeSuffix = ''
    if ($null -ne $Mode -and $Mode -ne '') {
        $modeSuffix = "." + $Mode
    }
    $logBaseName = ([System.IO.Path]::GetFileNameWithoutExtension($resolvedSource)) + $modeSuffix
    $stdoutPath = Join-Path $outputDir ($logBaseName + '.stdout.log')
    $stderrPath = Join-Path $outputDir ($logBaseName + '.stderr.log')
    $benchmarkStdoutPath = Join-Path $benchmarkLogDir ($logBaseName + '.stdout.log')
    $benchmarkStderrPath = Join-Path $benchmarkLogDir ($logBaseName + '.stderr.log')
    if (Test-Path $stdoutPath) {
        Remove-Item $stdoutPath -Force
    }
    if (Test-Path $stderrPath) {
        Remove-Item $stderrPath -Force
    }

    if ($runArgs.Count -gt 0) {
        $process = Start-Process -FilePath $outputPath `
            -ArgumentList $runArgs `
            -NoNewWindow `
            -Wait `
            -PassThru `
            -RedirectStandardOutput $stdoutPath `
            -RedirectStandardError $stderrPath
    } else {
        $process = Start-Process -FilePath $outputPath `
            -NoNewWindow `
            -Wait `
            -PassThru `
            -RedirectStandardOutput $stdoutPath `
            -RedirectStandardError $stderrPath
    }

    if (Test-Path $stdoutPath) {
        try {
            Get-Content $stdoutPath | ForEach-Object { Write-Host $_ }
        } catch {
            $msg = $_.Exception.Message
            if ($msg -notmatch 'pipeline has been stopped|pipe has been ended|The pipe is being closed') {
                throw
            }
        }
    }
    if (Test-Path $stderrPath) {
        try {
            Get-Content $stderrPath | ForEach-Object { Write-Host $_ }
        } catch {
            $msg = $_.Exception.Message
            if ($msg -notmatch 'pipeline has been stopped|pipe has been ended|The pipe is being closed') {
                throw
            }
        }
    }

    Copy-Item -Path $stdoutPath -Destination $benchmarkStdoutPath -Force
    Copy-Item -Path $stderrPath -Destination $benchmarkStderrPath -Force
    Write-Host "[brun] Saved logs to $benchmarkLogDir"

    $programExitCode = $process.ExitCode
    return $programExitCode
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

    $latestSrcOrHdr = Get-LatestWriteTimeUtcCached `
        -CacheKey 'ensure:binary:src-include' `
        -Paths @((Join-Path $root 'src'), (Join-Path $root 'include')) `
        -Extensions @('.cpp', '.hpp', '.h', '.hh', '.hxx', '.inl')

    if ($latestSrcOrHdr -gt $exeTime) {
        Write-Host "[build] Source changed - rebuilding $(Split-Path -Leaf $OutputPath)"
        Build-DgtlBinary -OutputPath $OutputPath -EntryPointPath $EntryPointPath -ExcludePaths $ExcludePaths -Strict:$Strict
        return
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
        (Join-Path $root 'tests\functional\pipeline\test_frontend_bridge_smoke.cpp'),
        (Join-Path $root 'tests\functional\pipeline\test_frontend_link_api_emit.cpp'),
        (Join-Path $root 'tests\functional\pipeline\test_frontend_pipeline_lowerer.cpp'),
        (Join-Path $root 'tests\functional\pipeline\test_frontend_payload_io.cpp'),
        (Join-Path $root 'tests\functional\pipeline\test_poly_matmul_scop_tiling.cpp'),
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
        (Join-Path $root 'tests\functional\pipeline\test_frontend_bridge_smoke.cpp'),
        (Join-Path $root 'tests\functional\pipeline\test_frontend_link_api_emit.cpp'),
        (Join-Path $root 'tests\functional\pipeline\test_frontend_pipeline_lowerer.cpp'),
        (Join-Path $root 'tests\functional\pipeline\test_frontend_payload_io.cpp'),
        (Join-Path $root 'tests\functional\pipeline\test_poly_matmul_scop_tiling.cpp'),
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

    foreach ($source in $testSourceCandidates) {
        if (-not (Test-Path $source)) {
            Build-DgtlTestBinary -OutputPath $OutputPath -Full:$Full
            return
        }
    }

    $latestTestInputs = Get-LatestWriteTimeUtcCached `
        -CacheKey 'ensure:test:inputs' `
        -Paths @((Join-Path $root 'src'), (Join-Path $root 'include')) `
        -Extensions @('.cpp', '.hpp', '.h', '.hh', '.hxx', '.inl') `
        -ExplicitFiles $testSourceCandidates

    if ($latestTestInputs -gt $exeWriteTime) {
        Build-DgtlTestBinary -OutputPath $OutputPath -Full:$Full
        return
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

function Invoke-SdkPackage {
    $sdkVersion = '1.0.0'
    $sdkDir     = Join-Path $root "build\DGTL_SDK"
    $sdkInclude = Join-Path $sdkDir 'include'
    $sdkLibX64  = Join-Path $sdkDir 'lib\x64'
    $sdkBin     = Join-Path $sdkDir 'bin'
    $objDir     = Join-Path $root   'build\sdk_obj'
    $zipName    = "DGTL_v${sdkVersion}_TataG_Systems.zip"
    $zipPath    = Join-Path $root   "build\$zipName"

    # 1. Compile library object files (all src/ except main.cpp and smoke_main.cpp)
    Write-Host '[sdk] Compiling library object files'
    New-Item -ItemType Directory -Force -Path $objDir | Out-Null

    $srcDir = Join-Path $root 'src'
    $sdkImplPath = [System.IO.Path]::GetFullPath((Join-Path $root 'src\sdk\dgtl_sdk_impl.cpp'))
    $libSources = Get-ChildItem -Path $srcDir -Recurse -File -Filter *.cpp |
        Sort-Object FullName |
        ForEach-Object { $_.FullName } |
        Where-Object {
            $fp = [System.IO.Path]::GetFullPath($_)
            $fp -ne [System.IO.Path]::GetFullPath((Join-Path $root 'src\main.cpp')) -and
            $fp -ne [System.IO.Path]::GetFullPath((Join-Path $root 'src\apps\smoke_main.cpp'))
        }

    # Add the SDK bridge implementation only if not already found by recursive scan
    if ($sdkImplPath -and (Test-Path $sdkImplPath)) {
        $alreadyIncluded = $libSources | Where-Object {
            [System.IO.Path]::GetFullPath($_) -eq $sdkImplPath
        }
        if (-not $alreadyIncluded) {
            $libSources = @($libSources) + @($sdkImplPath)
        }
    }

    $commonFlags = @(
        '-std=c++26',
        '-O1',
        '-c',
        '-I', (Join-Path $root 'include')
    )

    # Gather header timestamps for incremental check
    $incDir = Join-Path $root 'include'
    $newestHeaderTime = (Get-ChildItem -Path $incDir -Recurse -File | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1).LastWriteTimeUtc

    $objFiles = @()
    $compiled = 0
    $skipped  = 0
    foreach ($src in $libSources) {
        $baseName = [System.IO.Path]::GetFileNameWithoutExtension($src)
        # Prevent name collisions by prefixing directory
        $relPath  = $src.Substring($srcDir.Length).TrimStart('\', '/')
        $safeBase = ($relPath -replace '[\\/]', '_') -replace '\.cpp$', ''
        $objFile  = Join-Path $objDir "$safeBase.o"

        # Incremental: skip if .o is newer than both source and all headers
        $needsCompile = $true
        if (Test-Path $objFile) {
            $objTime = (Get-Item $objFile).LastWriteTimeUtc
            $srcTime = (Get-Item $src).LastWriteTimeUtc
            if ($objTime -gt $srcTime -and $objTime -gt $newestHeaderTime) {
                $needsCompile = $false
            }
        }

        if ($needsCompile) {
            $args = @() + $commonFlags + @($src, '-o', $objFile)
            Invoke-Gpp -Arguments $args -FailureMessage "Failed to compile $src"
            $compiled++
        } else {
            $skipped++
        }
        $objFiles += $objFile
    }
    Write-Host "[sdk] Object files: $compiled compiled, $skipped up-to-date ($($objFiles.Count) total)"

    # 2. Archive into static library using ar
    $arPath = Resolve-ToolchainBinary -ToolName 'ar'
    if (-not $arPath) {
        throw 'ar was not found in PATH. Required for static library creation.'
    }

    New-Item -ItemType Directory -Force -Path $sdkLibX64 | Out-Null
    $libFile = Join-Path $sdkLibX64 'dgtl.lib'

    # Remove old lib if it exists
    if (Test-Path $libFile) { Remove-Item -Force $libFile }

    Write-Host '[sdk] Archiving dgtl.lib'
    $arArgs = @('rcs', $libFile) + $objFiles
    & $arPath @arArgs
    if ($LASTEXITCODE -ne 0) {
        throw 'Failed to create static library dgtl.lib'
    }

    $libSize = (Get-Item $libFile).Length
    Write-Host "[sdk] Created dgtl.lib ($libSize bytes, $($objFiles.Count) objects)"

    # 3. Assemble SDK directory structure
    Write-Host '[sdk] Assembling SDK directory'

    # include/ — public SDK headers only
    New-Item -ItemType Directory -Force -Path $sdkInclude | Out-Null
    $sdkHeaders = Get-ChildItem -Path (Join-Path $root 'include\dgtl_sdk') -Filter *.h -File
    foreach ($h in $sdkHeaders) {
        Copy-Item -Path $h.FullName -Destination $sdkInclude -Force
    }

    # bin/ — production binary
    New-Item -ItemType Directory -Force -Path $sdkBin | Out-Null
    $prodExePath = Join-Path $root 'build\dgtl_release.exe'
    if (Test-Path $prodExePath) {
        Copy-Item -Path $prodExePath -Destination (Join-Path $sdkBin 'dgtl.exe') -Force
    }

    # LICENSE
    $licFile = Join-Path $root 'LICENSE'
    if (Test-Path $licFile) {
        Copy-Item -Path $licFile -Destination $sdkDir -Force
    }

    # 3b. Strip symbols from SDK artifacts to reduce distribution size
    $stripPath = Resolve-ToolchainBinary -ToolName 'strip'
    if ($stripPath) {
        $sdkExe = Join-Path $sdkBin 'dgtl.exe'
        if (Test-Path $sdkExe) {
            & $stripPath --strip-all $sdkExe
            $strippedExeSize = [math]::Round((Get-Item $sdkExe).Length / 1KB, 1)
            Write-Host "[sdk] Stripped dgtl.exe -> ${strippedExeSize} KB"
        }
        if (Test-Path $libFile) {
            & $stripPath --strip-unneeded $libFile
            $strippedLibSize = [math]::Round((Get-Item $libFile).Length / 1KB, 1)
            Write-Host "[sdk] Stripped dgtl.lib -> ${strippedLibSize} KB"
        }
    }

    # 4. Smoke link test
    Write-Host '[sdk] Running SDK smoke link test'
    $smokeTestSrc = Join-Path $root 'tests\sdk\sdk_smoke_test.cpp'
    $smokeTestExe = Join-Path $root 'build\sdk_smoke_test.exe'

    $smokeArgs = @(
        '-std=c++26', '-O2',
        '-I', $sdkInclude,
        '-I', (Join-Path $root 'include'),
        $smokeTestSrc,
        '-L', $sdkLibX64,
        '-l:dgtl.lib',
        '-o', $smokeTestExe
    )
    Invoke-Gpp -Arguments $smokeArgs -FailureMessage 'SDK smoke link test failed to compile'

    & $smokeTestExe
    if ($LASTEXITCODE -ne 0) {
        throw 'SDK smoke link test FAILED at runtime'
    }
    Write-Host '[sdk] Smoke link test PASSED'

    # 5. Create zip archive
    Write-Host "[sdk] Packaging $zipName"
    if (Test-Path $zipPath) { Remove-Item -Force $zipPath }
    Compress-Archive -Path $sdkDir -DestinationPath $zipPath -CompressionLevel Fastest
    $zipSize = (Get-Item $zipPath).Length
    Write-Host "[sdk] Created $zipPath ($zipSize bytes)"

    # Summary
    Write-Host ''
    Write-Host '=== DGTL SDK Package Summary ==='
    Write-Host "  Version:     $sdkVersion"
    Write-Host "  SDK dir:     $sdkDir"
    Write-Host "  Static lib:  $libFile ($libSize bytes)"
    Write-Host "  Headers:     $($sdkHeaders.Count) public headers"
    Write-Host "  ZIP:         $zipPath"
    Write-Host "  Smoke test:  PASSED"
    Write-Host '================================='
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
        (Join-Path $root 'build\libdgtl.a'),
        (Join-Path $root 'build\scop_test.o'),
        (Join-Path $root 'build\test_compile.o'),
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

    # SDK build intermediates, package output, and stale orphan directories
    $sdkDirs = @(
        (Join-Path $root 'build\sdk_obj'),
        (Join-Path $root 'build\DGTL_SDK'),
        (Join-Path $root 'build\sdk'),
        (Join-Path $root 'build\batch'),
        (Join-Path $root 'build\build'),
        (Join-Path $root 'build\tests_with_main')
    )
    foreach ($dir in $sdkDirs) {
        if (Test-Path $dir) {
            Remove-Item -Recurse -Force $dir
        }
    }
    $sdkFiles = @(
        (Join-Path $root 'build\sdk_smoke_test.exe')
    )
    foreach ($f in $sdkFiles) {
        if (Test-Path $f) {
            Remove-Item -Force $f
        }
    }

    Write-Host '[clean] Removed DGTL build outputs, conformance logs, and SDK artifacts'
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
    'check-headers' { $command = 'header-check' }
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
        Invoke-ApiPolicyGates
        Build-DgtlBinary -OutputPath $prodExe -EntryPointPath $prodMain -ExcludePaths @($smokeMain)
    }
    'smoke-build' {
        Build-DgtlBinary -OutputPath $smokeExe -EntryPointPath $smokeMain -ExcludePaths @($prodMain)
    }
    'strict' {
        Build-DgtlBinary -OutputPath $smokeStrictExe -EntryPointPath $smokeMain -ExcludePaths @($prodMain) -Strict
    }
    'run' {
        Invoke-HeaderCompliance
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

            if ($argLower -eq '--arm64-binary-stream') {
                $passThroughArgs += $arg
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
    'brun' {
        # Build a normalized brun argument list that supports:
        # - dgtl brun <source> [--mode <m>] [program args...]
        # - dgtl brun --mode <m> [program args...]
        $rawBrunArgs = @()
        if (-not [string]::IsNullOrWhiteSpace($modeArg)) {
            $rawBrunArgs += [string]$modeArg
        }
        $rawBrunArgs += @($extraArgs)

        # Extract --mode and source path (first non-flag token) from args
        $brunMode = ''
        $brunSourcePath = ''
        $brunProgramArgs = @()
        $i = 0
        while ($i -lt $rawBrunArgs.Count) {
            $arg = [string]$rawBrunArgs[$i]
            if ($arg -eq '--mode' -and ($i + 1) -lt $rawBrunArgs.Count) {
                $brunMode = [string]$rawBrunArgs[$i + 1]
                $i += 2
            } else {
                if ($brunSourcePath -eq '' -and -not $arg.StartsWith('--')) {
                    $brunSourcePath = $arg
                } else {
                    $brunProgramArgs += $arg
                }
                $i++
            }
        }
        # Validate mode
        if ($brunMode -ne '' -and $brunMode -ne 'elite' -and $brunMode -ne 'universal') {
            throw "Unsupported --mode '$brunMode'. Valid values: elite, universal."
        }

        if ([string]::IsNullOrWhiteSpace($brunSourcePath)) {
            $defaultBrunSources = @(
                'tests/performance/benchmark_hardest_e2e_gate.cpp',
                'tests/performance/benchmark_production.cpp',
                'tests/performance/compiler_audit_suite.cpp',
                'tests/performance/dgtl_benchmark.cpp'
            )

            Write-Host "[brun] Running default performance suite ($($defaultBrunSources.Count) files)"
            $suiteExitCode = 0
            foreach ($source in $defaultBrunSources) {
                $brunExitCode = Invoke-Brun -SourcePath $source -ProgramArgs $brunProgramArgs -Mode $brunMode
                if ($null -eq $brunExitCode) {
                    $brunExitCode = 0
                }
                if ($brunExitCode -ne 0) {
                    $suiteExitCode = $brunExitCode
                }
            }

            if ($suiteExitCode -ne 0) {
                exit $suiteExitCode
            }
        } else {
            $brunExitCode = Invoke-Brun -SourcePath $brunSourcePath -ProgramArgs $brunProgramArgs -Mode $brunMode
            if ($null -eq $brunExitCode) {
                $brunExitCode = 0
            }
            if ($brunExitCode -ne 0) {
                exit $brunExitCode
            }
        }
    }
    'update-golden' {
        Invoke-HeaderCompliance
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
        Invoke-HeaderCompliance
        Ensure-DgtlBinary -OutputPath $smokeStrictExe -EntryPointPath $smokeMain -ExcludePaths @($prodMain) -Strict
        Invoke-Conformance -ExecutablePath $smokeStrictExe
    }
    'test' {
        Invoke-ApiPolicyGates
        Invoke-HeaderCompliance
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
        Invoke-HeaderCompliance
        Clean-DgtlArtifacts
        Build-DgtlBinary -OutputPath $smokeExe -EntryPointPath $smokeMain -ExcludePaths @($prodMain)
        Invoke-Conformance -ExecutablePath $smokeExe
    }
    'header-check' {
        Invoke-HeaderCompliance
    }
    'package' {
        # Full validation gate: tests must pass before packaging
        Invoke-ApiPolicyGates
        Invoke-HeaderCompliance
        Ensure-DgtlTestBinary -OutputPath $testExe -Full
        Invoke-DgtlTestSuite -ExecutablePath $testExe -Full
        # Ensure production binary (reuse if up-to-date)
        Ensure-DgtlBinary -OutputPath $prodExe -EntryPointPath $prodMain -ExcludePaths @($smokeMain)
        # Package SDK
        Invoke-SdkPackage
    }
    default {
        throw "Unknown command '$command'. Run 'dgtl help' for usage."
    }
}