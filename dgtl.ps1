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
                'Usage:',
                '  dgtl help',
                '  dgtl build',
                '  dgtl strict',
                '  dgtl run [arm64|x64|nasm|wasm]',
        '  dgtl bench [arm64|x64|nasm|wasm] [profile|iterations] [pressure-vars] [dump]',
                '  dgtl update-golden [strict]',
                '  dgtl srun',
                '  dgtl clean',
                '  dgtl crun',
                '',
                'Notes:',
                '  build          Builds build\dgtl_main.exe',
                '  strict         Builds build\dgtl_main_strict.exe with strict warnings',
                '  run            Runs backend conformance, or a single backend if specified',
                '  bench          Runs split optimization/emission benchmark for one backend
                 Profiles: fold (default), fusion, simd, pressure
                 Examples: dgtl bench x64 / dgtl bench x64 fusion / dgtl bench x64 pressure 64 / dgtl bench x64 fusion dump',
                '  update-golden  Refreshes conformance baselines from the default or strict executable',
                '  srun           Runs backend conformance using the strict executable',
                '  clean          Removes built executables and conformance logs',
                '  crun           clean + build + run'
        ) -join [Environment]::NewLine
}

function Get-SourceFiles {
    $srcDir = Join-Path $root 'src'
    $files = Get-ChildItem -Path $srcDir -Recurse -File -Filter *.cpp |
        Sort-Object FullName |
        ForEach-Object { $_.FullName }

    if ($files.Count -eq 0) {
        throw "No source files found under $srcDir"
    }

    return $files
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

    $sourceFiles = Get-SourceFiles
    $arguments = @() + $commonFlags + $sourceFiles + @('-o', $OutputPath)

    Write-Host "[build] Compiling $(Split-Path -Leaf $OutputPath)"
    Invoke-Gpp -Arguments $arguments -FailureMessage "Compilation failed for $OutputPath"
    Write-Host "[build] Wrote $OutputPath"
}

function Ensure-DgtlBinary {
    param(
        [string]$OutputPath,
        [switch]$Strict
    )

    if (-not (Test-Path $OutputPath)) {
        Build-DgtlBinary -OutputPath $OutputPath -Strict:$Strict
    }
}

function Invoke-Conformance {
    param(
        [string]$ExecutablePath,
        [switch]$UpdateGolden
    )

    $scriptPath = Join-Path $root 'My_Docs\commands\backend_conformance_check.ps1'
    if (-not (Test-Path $scriptPath)) {
        throw "Missing conformance script: $scriptPath"
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
        [string]$Backend
    )

    if (-not (Test-Path $ExecutablePath)) {
        throw "Missing executable: $ExecutablePath. Run 'dgtl build' first."
    }

    $validBackends = @('arm64', 'x64', 'nasm', 'wasm')
    if ($Backend -notin $validBackends) {
        throw "Unsupported backend '$Backend'. Expected one of: $($validBackends -join ', ')"
    }

    & $ExecutablePath '--backend' $Backend
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
        [switch]$DumpBenchmark
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
        $validProfiles = @('fold', 'fusion', 'simd', 'pressure')
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

    & $ExecutablePath @cmdArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Benchmark failed for backend '$Backend'."
    }
}

function Clean-DgtlArtifacts {
    $paths = @(
        (Join-Path $root 'build\dgtl_main.exe'),
        (Join-Path $root 'build\dgtl_main_strict.exe'),
        (Join-Path $root 'dgtl_main.exe'),
        (Join-Path $root 'build\logs\conformance_arm64.log'),
        (Join-Path $root 'build\logs\conformance_x64.log'),
        (Join-Path $root 'build\logs\conformance_nasm.log'),
        (Join-Path $root 'build\logs\conformance_wasm.log')
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
$modeArg = if ($allArgs.Count -gt 1 -and $null -ne $allArgs[1] -and $allArgs[1] -ne '') {
    [string]$allArgs[1]
} else {
    $null
}

$extraArg = if ($allArgs.Count -gt 2 -and $null -ne $allArgs[2] -and $allArgs[2] -ne '') {
    [string]$allArgs[2]
} else {
    $null
}

$extraArg2 = if ($allArgs.Count -gt 3 -and $null -ne $allArgs[3] -and $allArgs[3] -ne '') {
    [string]$allArgs[3]
} else {
    $null
}

$extraArg3 = if ($allArgs.Count -gt 4 -and $null -ne $allArgs[4] -and $allArgs[4] -ne '') {
    [string]$allArgs[4]
} else {
    $null
}

$defaultExe = Join-Path $root 'build\dgtl_main.exe'
$strictExe = Join-Path $root 'build\dgtl_main_strict.exe'

switch ($command) {
    'help' {
        Show-Usage
    }
    'build' {
        Build-DgtlBinary -OutputPath $defaultExe
    }
    'strict' {
        Build-DgtlBinary -OutputPath $strictExe -Strict
    }
    'run' {
        if ($null -ne $modeArg) {
            Invoke-SingleBackend -ExecutablePath $defaultExe -Backend $modeArg.ToLowerInvariant()
        } else {
            Invoke-Conformance -ExecutablePath $defaultExe
        }
    }
    'bench' {
        if ($null -eq $modeArg) {
            throw "Missing backend for bench. Use 'dgtl bench x64' or similar."
        }
        Ensure-DgtlBinary -OutputPath $strictExe -Strict

        $benchExtras = @()
        if ($null -ne $extraArg) { $benchExtras += $extraArg }
        if ($null -ne $extraArg2) { $benchExtras += $extraArg2 }
        if ($null -ne $extraArg3) { $benchExtras += $extraArg3 }

        $dumpRequested = $false
        $filteredExtras = @()
        foreach ($arg in $benchExtras) {
            if ($arg.ToLowerInvariant() -eq 'dump') {
                $dumpRequested = $true
            } else {
                $filteredExtras += $arg
            }
        }

        if ($filteredExtras.Count -gt 2) {
            throw "Too many bench arguments. Use: dgtl bench <backend> [profile|iterations] [pressure-vars] [dump]"
        }

        $profileOrIterations = if ($filteredExtras.Count -ge 1) { [string]$filteredExtras[0] } else { $null }
        $pressureVars = if ($filteredExtras.Count -ge 2) { [string]$filteredExtras[1] } else { $null }

        Invoke-BackendBenchmark -ExecutablePath $strictExe `
            -Backend $modeArg.ToLowerInvariant() `
            -ProfileOrIterations $profileOrIterations `
            -PressureVars $pressureVars `
            -DumpBenchmark:$dumpRequested
    }
    'update-golden' {
        if ($null -ne $modeArg -and $modeArg.ToLowerInvariant() -eq 'strict') {
            Ensure-DgtlBinary -OutputPath $strictExe -Strict
            Invoke-Conformance -ExecutablePath $strictExe -UpdateGolden
        } elseif ($null -ne $modeArg) {
            throw "Unsupported update-golden mode '$modeArg'. Use 'dgtl update-golden' or 'dgtl update-golden strict'."
        } else {
            Ensure-DgtlBinary -OutputPath $defaultExe
            Invoke-Conformance -ExecutablePath $defaultExe -UpdateGolden
        }
    }
    'srun' {
        Invoke-Conformance -ExecutablePath $strictExe
    }
    'clean' {
        Clean-DgtlArtifacts
    }
    'crun' {
        Clean-DgtlArtifacts
        Build-DgtlBinary -OutputPath $defaultExe
        Invoke-Conformance -ExecutablePath $defaultExe
    }
    default {
        throw "Unknown command '$command'. Run 'dgtl help' for usage."
    }
}