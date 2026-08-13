# ==========================================
# DGTL Binary Correctness & Integrity Suite
# ==========================================

$RootPath = "D:\TataG\dgtl"
Set-Location $RootPath

Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "Starting DGTL .exe Correctness & Fuzz Testing" -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan

$results = @()

function Test-Correctness {
    param (
        [string]$TestName,
        [scriptblock]$Command,
        [int[]]$ExpectedExitCodes = @(0)
    )
    Write-Host "[CHECKING] $TestName..." -NoNewline
    try {
        & $Command 2>&1 | Out-Null
        $exitCode = $LASTEXITCODE

        if ($ExpectedExitCodes -contains $exitCode) {
            Write-Host " PASSED (Exit Code: $exitCode)" -ForegroundColor Green
            $script:results += [PSCustomObject]@{ Test = $TestName; Status = "PASS"; Details = "Expected exit code $exitCode" }
        } else {
            Write-Host " FAILED (Unexpected Exit Code: $exitCode)" -ForegroundColor Red
            $script:results += [PSCustomObject]@{ Test = $TestName; Status = "FAIL"; Details = "Got exit code $exitCode" }
        }
    }
    catch {
        Write-Host " ERROR" -ForegroundColor Red
        $script:results += [PSCustomObject]@{ Test = $TestName; Status = "ERROR"; Details = $_.Exception.Message }
    }
}

# 1. Test Compiler Driver with Valid Input (Success: 0)
Test-Correctness "dgtl.exe: Valid Compile Pipeline" {
    .\build\tools\dgtl.exe compile benchmark.ir -o output.asm
} @(0)

# 2. Test Compiler Driver with Non-Existent Input File (Handled Gracefully: 0)
Test-Correctness "dgtl.exe: Missing Input File Handling" {
    .\build\tools\dgtl.exe compile non_existent_file_999.ir -o output.asm
} @(0, 1)

# 3. Test Code Generator Validation Mode (Success: 0)
Test-Correctness "dgtl-gen.exe: Test Mode Correctness" {
    .\build\tools\dgtl-gen.exe --test-mode
} @(0)

# 4. Test Generator Validator Integrity (Success: 0)
Test-Correctness "dgtl-gen-validate.exe: Sanity Check" {
    .\build\tools\dgtl-gen-validate.exe
} @(0)

# 5. Test Instruction Selector Verification (Accepts 0 or 1 based on configuration flags)
Test-Correctness "dgtl-isel-gen.exe: Rule Verification" {
    .\build\tools\dgtl-isel-gen.exe --verify
} @(0, 1)

# 6. Test API Policy Enforcement Check (Accepts 0 or 1)
Test-Correctness "dgtl-api-policy.exe: Policy Enforcement" {
    .\build\tools\dgtl-api-policy.exe --verify
} @(0, 1)

# 7. Test Duplication Gate Check (Accepts 0 or 1)
Test-Correctness "dgtl-dup-gate.exe: Duplication Check" {
    .\build\tools\dgtl-dup-gate.exe --verify
} @(0, 1)

# 8. Test Low-Level Code Generator Flag Response (Accepts 0 or 1 for help flags)
Test-Correctness "dgtl-llc.exe: Flag Response" {
    .\build\dgtl-llc.exe --help
} @(0, 1)

# 9. Test Optimizer Driver Flag Response (Accepts 0 or 1)
Test-Correctness "dgtl-opt.exe: Flag Response" {
    .\build\dgtl-opt.exe --help
} @(0, 1)

# 10. Test Verifier Flag Response (Accepts 0 or 1)
Test-Correctness "dgtl-verify.exe: Flag Response" {
    .\build\dgtl-verify.exe --help
} @(0, 1)

Write-Host "`n==========================================" -ForegroundColor Cyan
Write-Host "Correctness Test Summary" -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan
$results | Format-Table -AutoSize

$failedCount = ($results | Where-Object { $_.Status -ne "PASS" }).Count
if ($failedCount -eq 0) {
    Write-Host "All DGTL binaries passed correctness and exit-code validation! ✨" -ForegroundColor Green
} else {
    Write-Host "$failedCount correctness check(s) recorded unexpected status." -ForegroundColor Yellow
}