<#
.SYNOPSIS
    DGTL 2.1.0 Sovereign Industry-Grade Test Suite Driver & Harness (NEWTEST)

.DESCRIPTION
    Automated, high-fidelity compilation and execution harness for DGTL's 12-tier 
    industrial verification suite. Supports single-tier isolation, master suite execution,
    differential oracle health audits, dynamic toolchain discovery, and CI/CD reporting.

.PARAMETER Target
    Target suite or specific test file. Options:
    - 'all'        : Runs the Golden Diagnostic Oracle and the Master Industry Suite. (Default)
    - 'oracle'     : Runs only the 12-layer client debug oracle audit (test_client_debug_oracle.cpp).
    - 'suite'      : Runs the master combined runner (newtest_runner.cpp).
    - 'tier1'..'tier12': Runs all standalone tests within the specified architectural tier.
    - '<filename>' : Runs a specific standalone test file (e.g., tier7_stress_concurrency_threads.cpp).

.PARAMETER Compiler
    Explicit C++ compiler executable path or name (default: auto-detected g++, clang++, or msys2 ucrt64).

.PARAMETER CppStandard
    Target C++ language standard dialect (default: c++26, with automatic fallback to c++23/c++20).

.PARAMETER OptLevel
    Optimization level flag for test binaries (default: O2).

.PARAMETER Clean
    Clean the test build directory (build/newtest) prior to execution.

.PARAMETER List
    List all available test files, architectural tiers, and targets without compiling.

.PARAMETER NoRun
    Compile test targets only without executing them.

.PARAMETER FailFast
    Halt immediately on the first compilation or execution error.

.PARAMETER VerboseOutput
    Display complete compiler command invocations and debug diagnostics.

.EXAMPLE
    .\run_newtest.ps1
    .\run_newtest.ps1 -Target oracle
    .\run_newtest.ps1 -Target tier7 -VerboseOutput
    .\run_newtest.ps1 -Target tier12_tool_orchestrator_and_forwarder.cpp
    .\run_newtest.ps1 -List
#>

[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$Target = "all",

    [string]$Compiler = "",
    [string]$CppStandard = "c++26",
    [string]$OptLevel = "O2",
    [switch]$Clean,
    [switch]$List,
    [switch]$NoRun,
    [switch]$FailFast,
    [switch]$VerboseOutput,
    [switch]$NoColor,
    [string]$JUnitXml = "",
    [ValidateSet("static", "shared")]
    [string]$LinkType = "static"
)

$ErrorActionPreference = 'Stop'

# ==============================================================================
# SECTION 1 - ROBUST PATH & REPOSITORY ROOT RESOLUTION
# ==============================================================================
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

# Walk up directory hierarchy until DGTL workspace root is verified
$RootDir = $ScriptDir
while ($RootDir -and -not (Test-Path (Join-Path $RootDir "include\dgtl_sdk\dgtl.h"))) {
    $parent = Split-Path -Parent $RootDir
    if ($parent -eq $RootDir) { break }
    $RootDir = $parent
}

if (-not (Test-Path (Join-Path $RootDir "include\dgtl_sdk\dgtl.h"))) {
    Write-Error "[FATAL] Unable to locate DGTL repository root from script directory: $ScriptDir"
    exit 1
}

$BuildDir       = Join-Path $RootDir "build\newtest"
$IncludeDir     = Join-Path $RootDir "include"
$SdkIncludeDir  = Join-Path $RootDir "build\DGTL_SDK\include"
$BinDir         = Join-Path $RootDir "build\bin"

# Locate sovereign libraries based on LinkType (static vs shared)
if ($LinkType -eq "shared") {
    $LibCandidates = @(
        (Join-Path $RootDir "build\DGTL_SDK\lib\x64\libdgtl.dll.a"),
        (Join-Path $RootDir "build\libdgtl.dll.a"),
        (Join-Path $RootDir "libdgtl.dll.a"),
        (Join-Path $RootDir "build\DGTL_SDK\lib\x64\dgtl_dll.lib"),
        (Join-Path $RootDir "build\dgtl_dll.lib")
    )
    $LibPath = $LibCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1

    # Locate dgtl.dll directory and prepend to PATH for dynamic execution
    $DllCandidates = @(
        (Join-Path $RootDir "build\DGTL_SDK\bin\dgtl.dll"),
        (Join-Path $RootDir "build\bin\dgtl.dll"),
        (Join-Path $RootDir "build\dgtl.dll"),
        (Join-Path $RootDir "dgtl.dll")
    )
    $DllPath = $DllCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if ($DllPath) {
        $dllDir = Split-Path -Parent $DllPath
        $env:PATH = "$dllDir;" + $env:PATH
    }
} else {
    $LibCandidates = @(
        (Join-Path $RootDir "dgtl.lib"),
        (Join-Path $RootDir "build\dgtl.lib"),
        (Join-Path $RootDir "build\DGTL_SDK\lib\x64\dgtl.lib"),
        (Join-Path $RootDir "libdgtl.a"),
        (Join-Path $RootDir "build\libdgtl.a"),
        (Join-Path $RootDir "build\DGTL_SDK\lib\x64\libdgtl.a")
    )
    $LibPath = $LibCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}

# ==============================================================================
# SECTION 2 - TERMINAL FORMATTING
# ==============================================================================
function Format-Title([string]$text) {
    if ($NoColor) { Write-Host $text } else { Write-Host $text -ForegroundColor Cyan }
}
function Format-Success([string]$text) {
    if ($NoColor) { Write-Host $text } else { Write-Host $text -ForegroundColor Green }
}
function Format-Warn([string]$text) {
    if ($NoColor) { Write-Host $text } else { Write-Host $text -ForegroundColor Yellow }
}
function Format-Error([string]$text) {
    if ($NoColor) { Write-Host $text } else { Write-Host $text -ForegroundColor Red }
}
function Format-Info([string]$text) {
    if ($NoColor) { Write-Host $text } else { Write-Host $text -ForegroundColor DarkGray }
}

# ==============================================================================
# SECTION 3 - TOOLCHAIN & COMPILER AUTO-DISCOVERY
# ==============================================================================
function Find-SovereignCompiler {
    if ($Compiler -and (Get-Command $Compiler -ErrorAction SilentlyContinue)) {
        return (Get-Command $Compiler).Source
    }

    $candidates = @(
        "g++",
        "clang++",
        "C:\msys64\ucrt64\bin\g++.exe",
        "C:\msys64\mingw64\bin\g++.exe",
        "C:\msys64\usr\bin\g++.exe"
    )

    foreach ($cand in $candidates) {
        $cmd = Get-Command $cand -ErrorAction SilentlyContinue
        if ($cmd) {
            return $cmd.Source
        }
    }
    return $null
}

# ==============================================================================
# SECTION 4 - CLEAN & LIST OPERATIONS
# ==============================================================================
if ($Clean) {
    if (Test-Path $BuildDir) {
        Format-Warn "[CLEAN] Purging test build directory: $BuildDir"
        Remove-Item -Recurse -Force $BuildDir -ErrorAction SilentlyContinue
    }
}

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

$AllTestFiles = Get-ChildItem -Path $ScriptDir -Filter "*.cpp" | Where-Object { $_.Name -ne "newtest_runner.cpp" } | Sort-Object Name

if ($List) {
    Format-Title "`n========================================================================"
    Format-Title "  DGTL 2.1.0 INDUSTRY-GRADE TEST SUITE CATALOG ($($AllTestFiles.Count + 1) TESTS) "
    Format-Title "========================================================================"
    Write-Host "Target Suites:" -ForegroundColor Yellow
    Write-Host "  all    : Runs Oracle diagnostic and Master test suite"
    Write-Host "  oracle : test_client_debug_oracle.cpp (12-layer stack audit)"
    Write-Host "  suite  : newtest_runner.cpp (All tiers compiled master)"
    Write-Host "`nArchitectural Tiers:" -ForegroundColor Yellow
    for ($i = 1; $i -le 12; $i++) {
        $tierFiles = $AllTestFiles | Where-Object { $_.Name -match "^tier$i" -or ($i -eq 1 -and $_.Name -match "^test_layer") }
        Write-Host ("  tier{0,-2}: {1} tests" -f $i, $tierFiles.Count) -ForegroundColor Cyan
    }
    Write-Host "`nStandalone Files:" -ForegroundColor Yellow
    foreach ($tf in $AllTestFiles) {
        Write-Host "  $($tf.Name)"
    }
    exit 0
}

# ==============================================================================
# SECTION 5 - PRE-FLIGHT ENVIRONMENT AUDIT
# ==============================================================================
Format-Title "========================================================================"
Format-Title "  DGTL 2.1.0 SOVEREIGN INDUSTRY-GRADE TEST SUITE (NEWTEST)              "
Format-Title "========================================================================"

$CompilerExe = Find-SovereignCompiler
if (-not $CompilerExe) {
    Format-Error "[FATAL] No compliant C++ compiler (g++ / clang++) discovered in PATH or MSYS2!"
    exit 1
}

$CompilerVerRaw = & $CompilerExe --version 2>&1 | Select-Object -First 1
Format-Info "  Root Directory : $RootDir"
Format-Info "  Script Directory: $ScriptDir"
Format-Info "  Build Scratch  : $BuildDir"
Format-Info "  Compiler       : $CompilerExe ($CompilerVerRaw)"
Format-Info "  Dialect Target : -$CppStandard -$OptLevel"

if (-not $LibPath) {
    Format-Error "[FATAL] dgtl.lib not found in repository root or build/!"
    Format-Warn  "        Run 'dgtl build' or 'dgtl.cmd' to compile the DGTL sovereign library first."
    exit 1
}
Format-Info "  Sovereign Lib  : $LibPath ($([math]::Round((Get-Item $LibPath).Length / 1MB, 2)) MB)`n"

# Verify C++ standard support
$probeCode = "int main(){return 0;}"
$probeTmp = Join-Path $BuildDir "probe.cpp"
Set-Content -Path $probeTmp -Value $probeCode -Encoding Ascii
$probeOut = & $CompilerExe "-std=$CppStandard" $probeTmp -o (Join-Path $BuildDir "probe.exe") 2>&1
if ($LASTEXITCODE -ne 0) {
    Format-Warn "[WARN] Compiler does not support -$CppStandard; falling back to -std=c++23"
    $CppStandard = "c++23"
    $probeOut = & $CompilerExe "-std=$CppStandard" $probeTmp -o (Join-Path $BuildDir "probe.exe") 2>&1
    if ($LASTEXITCODE -ne 0) {
        Format-Warn "[WARN] Compiler does not support -std=c++23; falling back to -std=c++20"
        $CppStandard = "c++20"
    }
}
Remove-Item -Path $probeTmp -Force -ErrorAction SilentlyContinue
Remove-Item -Path (Join-Path $BuildDir "probe.exe") -Force -ErrorAction SilentlyContinue

# ==============================================================================
# SECTION 6 - COMPILATION & EXECUTION ENGINE
# ==============================================================================
$TestResults = [System.Collections.Generic.List[PSCustomObject]]::new()

function Execute-IndustryTest {
    param(
        [string]$SourceFile,
        [string]$OutputExe,
        [string]$Description
    )

    $srcPath = Join-Path $ScriptDir $SourceFile
    $outPath = Join-Path $BuildDir $OutputExe

    if (-not (Test-Path $srcPath)) {
        Format-Error "[FAIL] Source file not found: $srcPath"
        return $false
    }

    Write-Host ("[BUILD] {0,-45} -> {1}" -f $SourceFile, $OutputExe) -ForegroundColor Yellow

    $compileArgs = @(
        "-std=$CppStandard",
        "-$OptLevel",
        "-pipe",
        "-Wall",
        "-Wextra",
        "-Wno-unknown-pragmas",
        "-Wno-unused-function",
        "-I$IncludeDir",
        "-I$ScriptDir"
    )

    if (Test-Path $SdkIncludeDir) {
        $compileArgs += "-I$SdkIncludeDir"
    }

    if ($LinkType -eq "shared") {
        $compileArgs += "-DDGTL_IMPORT_SHARED"
    }

    $compileArgs += @(
        $srcPath,
        $LibPath,
        "-lkernel32",
        "-lntdll",
        "-o",
        $outPath
    )

    if ($VerboseOutput) {
        Format-Info "  CMD: $CompilerExe $($compileArgs -join ' ')"
    }

    $swBuild = [System.Diagnostics.Stopwatch]::StartNew()
    $buildOutput = & $CompilerExe @compileArgs 2>&1
    $swBuild.Stop()
    $buildMs = $swBuild.ElapsedMilliseconds

    if ($LASTEXITCODE -ne 0) {
        Format-Error "[FAIL] Compilation failed for $SourceFile ($buildMs ms)"
        if ($buildOutput) {
            $buildOutput | ForEach-Object { Write-Host "       $_" -ForegroundColor Red }
        }
        $TestResults.Add([PSCustomObject]@{
            Target    = $SourceFile
            BuildMs   = $buildMs
            RunMs     = 0
            Status    = "BUILD_FAIL"
            ExitCode  = $LASTEXITCODE
            Details   = ($buildOutput -join "`n")
        })
        if ($FailFast) { exit 1 }
        return $false
    }

    if ($NoRun) {
        Format-Success "  +-- [OK] Built in $buildMs ms (Execution skipped via -NoRun)"
        $TestResults.Add([PSCustomObject]@{
            Target    = $SourceFile
            BuildMs   = $buildMs
            RunMs     = 0
            Status    = "BUILT"
            ExitCode  = 0
            Details   = "Compilation successful, execution skipped."
        })
        return $true
    }

    Write-Host ("[EXEC]  Running {0}..." -f $OutputExe) -ForegroundColor Cyan

    $swRun = [System.Diagnostics.Stopwatch]::StartNew()
    $execOutput = & $outPath 2>&1
    $runExitCode = $LASTEXITCODE
    $swRun.Stop()
    $runMs = $swRun.ElapsedMilliseconds

    if ($runExitCode -eq 0) {
        Format-Success ("  +-- [PASS] Completed in {0} ms (Build: {1} ms)`n" -f $runMs, $buildMs)
        if ($execOutput -and ($VerboseOutput -or $SourceFile -eq "test_client_debug_oracle.cpp")) {
            $execOutput | ForEach-Object { Write-Host "     $_" }
            Write-Host ""
        }
        $TestResults.Add([PSCustomObject]@{
            Target    = $SourceFile
            BuildMs   = $buildMs
            RunMs     = $runMs
            Status    = "PASS"
            ExitCode  = 0
            Details   = ""
        })
        return $true
    } else {
        Format-Error ("  +-- [FAIL] Exited with code {0} in {1} ms" -f $runExitCode, $runMs)
        if ($execOutput) {
            $execOutput | ForEach-Object { Write-Host "       $_" -ForegroundColor Red }
        }
        Write-Host ""
        $TestResults.Add([PSCustomObject]@{
            Target    = $SourceFile
            BuildMs   = $buildMs
            RunMs     = $runMs
            Status    = "FAIL"
            ExitCode  = $runExitCode
            Details   = ($execOutput -join "`n")
        })
        if ($FailFast) { exit $runExitCode }
        return $false
    }
}

# ==============================================================================
# SECTION 7 - TARGET DISPATCH
# ==============================================================================
$OverallSuccess = $true

if ($Target -eq "all") {
    # 1. Oracle Health Audit
    $ok = Execute-IndustryTest "test_client_debug_oracle.cpp" "test_client_debug_oracle.exe" "12-Layer Stack Oracle"
    if (-not $ok) { $OverallSuccess = $false }

    # 2. Master Integrated Suite
    $ok = Execute-IndustryTest "newtest_runner.cpp" "newtest_master.exe" "Master Industry-Grade Suite"
    if (-not $ok) { $OverallSuccess = $false }
}
elseif ($Target -eq "oracle") {
    $ok = Execute-IndustryTest "test_client_debug_oracle.cpp" "test_client_debug_oracle.exe" "12-Layer Stack Oracle"
    if (-not $ok) { $OverallSuccess = $false }
}
elseif ($Target -eq "suite" -or $Target -eq "master") {
    $ok = Execute-IndustryTest "newtest_runner.cpp" "newtest_master.exe" "Master Industry-Grade Suite"
    if (-not $ok) { $OverallSuccess = $false }
}
elseif ($Target -match "^tier(\d+)$") {
    $tierNum = $Matches[1]
    Format-Title "`n[TIER $tierNum] Executing Isolated Tests for Tier $tierNum..."
    $tierFiles = $AllTestFiles | Where-Object { 
        $_.Name -match "^tier$tierNum" -or ($tierNum -eq "1" -and $_.Name -match "^test_layer")
    }

    if ($tierFiles.Count -eq 0) {
        Format-Warn "[WARN] No tests found matching Tier $tierNum"
    } else {
        foreach ($tf in $tierFiles) {
            $exeName = [System.IO.Path]::ChangeExtension($tf.Name, ".exe")
            $ok = Execute-IndustryTest $tf.Name $exeName "Tier $tierNum Test"
            if (-not $ok) { $OverallSuccess = $false }
        }
    }
}
else {
    # Direct file or pattern match
    $matched = $AllTestFiles | Where-Object { $_.Name -eq $Target -or $_.Name -like "*$Target*" }
    if ($matched.Count -eq 0) {
        Format-Error "[FATAL] Target '$Target' does not match any known test file or tier."
        Format-Warn  "        Run with -List to inspect all available test targets."
        exit 1
    }

    foreach ($tf in $matched) {
        $exeName = [System.IO.Path]::ChangeExtension($tf.Name, ".exe")
        $ok = Execute-IndustryTest $tf.Name $exeName "Custom Test Target"
        if (-not $ok) { $OverallSuccess = $false }
    }
}

# ==============================================================================
# SECTION 8 - TABULAR SUMMARY REPORT
# ==============================================================================
Format-Title "`n========================================================================"
Format-Title "  DGTL 2.1.0 NEWTEST EXECUTION REPORT SUMMARY                           "
Format-Title "========================================================================"

$totalTests  = $TestResults.Count
$totalPassed = @($TestResults | Where-Object { $_.Status -eq "PASS" -or $_.Status -eq "BUILT" }).Count
$totalFailed = $totalTests - $totalPassed

$TestResults | ForEach-Object {
    $statusColor = switch ($_.Status) {
        "PASS"       { "Green" }
        "BUILT"      { "Cyan" }
        "BUILD_FAIL" { "Red" }
        "FAIL"       { "Red" }
        Default      { "White" }
    }
    $line = "{0,-46} | Status: {1,-10} | Build: {2,5} ms | Run: {3,5} ms" -f $_.Target, $_.Status, $_.BuildMs, $_.RunMs
    Write-Host $line -ForegroundColor $statusColor
}

Write-Host "------------------------------------------------------------------------" -ForegroundColor DarkGray

if ($JUnitXml) {
    try {
        $xmlTarget = $JUnitXml
        if (-not [System.IO.Path]::IsPathRooted($xmlTarget)) {
            $xmlTarget = Join-Path $RootDir $xmlTarget
        }
        $xmlDir = Split-Path -Parent $xmlTarget
        if ($xmlDir -and -not (Test-Path $xmlDir)) {
            New-Item -ItemType Directory -Path $xmlDir -Force | Out-Null
        }

        $totalSeconds = 0.0
        if ($TestResults.Count -gt 0) {
            $totalMs = ($TestResults | Measure-Object -Property BuildMs -Sum).Sum + ($TestResults | Measure-Object -Property RunMs -Sum).Sum
            $totalSeconds = [Math]::Round($totalMs / 1000.0, 3)
        }

        $sb = [System.Text.StringBuilder]::new()
        [void]$sb.AppendLine('<?xml version="1.0" encoding="UTF-8"?>')
        [void]$sb.AppendLine("<testsuites name=`"DGTL_NewTest`" tests=`"$totalTests`" failures=`"$totalFailed`" time=`"$totalSeconds`">")
        [void]$sb.AppendLine("  <testsuite name=`"DGTL_2.1.0_Verification`" tests=`"$totalTests`" failures=`"$totalFailed`" time=`"$totalSeconds`">")

        foreach ($r in $TestResults) {
            $testTime = [Math]::Round(($r.BuildMs + $r.RunMs) / 1000.0, 3)
            $safeName = [System.Security.SecurityElement]::Escape($r.Target)
            [void]$sb.AppendLine("    <testcase name=`"$safeName`" classname=`"dgtl.newtest`" time=`"$testTime`">")
            if ($r.Status -eq "BUILD_FAIL" -or $r.Status -eq "FAIL") {
                $safeStatus = [System.Security.SecurityElement]::Escape($r.Status)
                $safeDetails = [System.Security.SecurityElement]::Escape([string]$r.Details)
                [void]$sb.AppendLine("      <failure message=`"Test failed with status: $safeStatus`" type=`"$safeStatus`">$safeDetails</failure>")
            }
            [void]$sb.AppendLine("    </testcase>")
        }

        [void]$sb.AppendLine("  </testsuite>")
        [void]$sb.AppendLine("</testsuites>")

        [System.IO.File]::WriteAllText($xmlTarget, $sb.ToString(), [System.Text.Encoding]::UTF8)
        Format-Info "  JUnit XML report generated: $xmlTarget"
    } catch {
        Format-Warn "  Failed to write JUnit XML report: $_"
    }
}

if ($OverallSuccess -and $totalFailed -eq 0) {
    Format-Success "  RESULT: 100% PASSED ($totalPassed / $totalTests tests successful)"
    exit 0
} else {
    Format-Error   "  RESULT: FAILED ($totalFailed / $totalTests failures detected)"
    exit 1
}
