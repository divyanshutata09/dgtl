# ==========================================
# DGTL Complete Throughput & Binary Stress
# ==========================================

$RootPath = "D:\TataG\dgtl"
Set-Location $RootPath

Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "Starting Complete DGTL Throughput Stress" -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan

$throughputResults = @()
$Iterations = 10

function Test-Throughput {
    param (
        [string]$ToolName,
        [scriptblock]$Command,
        [int]$Runs = 10
    )
    Write-Host "[BENCHMARKING] $ToolName ($Runs iterations)..." -NoNewline
    
    try {
        # Warm-up run
        & $Command 2>&1 | Out-Null

        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        for ($i = 1; $i -le $Runs; $i++) {
            & $Command 2>&1 | Out-Null
        }
        $sw.Stop()

        $totalMs = $sw.ElapsedMilliseconds
        if ($totalMs -eq 0) { $totalMs = 1 }
        $opsPerSec = [Math]::Round(($Runs / ($totalMs / 1000)), 2)
        $avgMs = [Math]::Round(($totalMs / $Runs), 2)

        Write-Host " Done! (" -NoNewline
        Write-Host "$opsPerSec ops/sec" -ForegroundColor Green -NoNewline
        Write-Host " | Avg: $avgMs ms)"

        $script:throughputResults += [PSCustomObject]@{
            Tool = $ToolName
            Iterations = $Runs
            TotalTimeMs = $totalMs
            AvgTimeMs = $avgMs
            ThroughputOpsSec = $opsPerSec
        }
    }
    catch {
        Write-Host " SKIPPED/ERROR (Binary requires specific arguments)" -ForegroundColor Yellow
    }
}

# 1. Core Compilation Pipeline Throughput (dgtl.exe)
Test-Throughput "tools\dgtl.exe (Compile Pipeline)" {
    .\build\tools\dgtl.exe compile benchmark.ir -o output.asm
} $Iterations

# 2. Large IR Benchmark Throughput
Test-Throughput "tools\dgtl.exe (Large IR Pipeline)" {
    .\build\tools\dgtl.exe compile large_benchmark.ir -o output.asm
} $Iterations

# 3. Production Release Binary (dgtl_release.exe)
if (Test-Path ".\build\dgtl_release.exe") {
    Test-Throughput "dgtl_release.exe" {
        .\build\dgtl_release.exe --help
    } $Iterations
}

# 4. Strict Production Binary (dgtl_main_strict.exe)
if (Test-Path ".\build\dgtl_main_strict.exe") {
    Test-Throughput "dgtl_main_strict.exe" {
        .\build\dgtl_main_strict.exe --help
    } $Iterations
}

# 5. Code Generator Tool Throughput
Test-Throughput "tools\dgtl-gen.exe" {
    .\build\tools\dgtl-gen.exe --test-mode
} $Iterations

# 6. Generator Validator Throughput
Test-Throughput "tools\dgtl-gen-validate.exe" {
    .\build\tools\dgtl-gen-validate.exe
} $Iterations

# 7. Instruction Selector Gen Throughput
Test-Throughput "tools\dgtl-isel-gen.exe" {
    .\build\tools\dgtl-isel-gen.exe --verify
} $Iterations

# 8. API Policy Enforcement Throughput
Test-Throughput "tools\dgtl-api-policy.exe" {
    .\build\tools\dgtl-api-policy.exe --verify
} $Iterations

# 9. Duplication Gate Throughput
Test-Throughput "tools\dgtl-dup-gate.exe" {
    .\build\tools\dgtl-dup-gate.exe --verify
} $Iterations

# 10. Low-Level Code Generator Throughput (dgtl-llc)
Test-Throughput "dgtl-llc.exe" {
    .\build\dgtl-llc.exe --help
} $Iterations

# 11. Optimizer Driver Throughput (dgtl-opt)
Test-Throughput "dgtl-opt.exe" {
    .\build\dgtl-opt.exe --help
} $Iterations

# 12. Verifier Throughput (dgtl-verify)
Test-Throughput "dgtl-verify.exe" {
    .\build\dgtl-verify.exe --help
} $Iterations

Write-Host "`n==========================================" -ForegroundColor Cyan
Write-Host "Throughput Benchmark Summary" -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan
$throughputResults | Format-Table -AutoSize

Write-Host "Throughput stress test completed successfully! 🚀" -ForegroundColor Green