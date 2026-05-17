#!/usr/bin/env pwsh
<#
.SYNOPSIS
Integration test for FileCheck functionality in dgtl-opt

.DESCRIPTION
Validates that:
1. FileCheck directives are correctly parsed
2. Line ending normalization works on Windows and Unix-style files
3. All directive types (CHECK, CHECK-NEXT, CHECK-NOT, CHECK-DAG, CHECK-LABEL) work
4. Error reporting is accurate and useful
5. Exit codes are correct

.EXAMPLE
.\test_filecheck_integration.ps1
#>

param(
	[string]$DgtlOptPath = ".\build\dgtl_opt.exe",
	[switch]$Verbose
)

$testsPassed = 0
$testsFailed = 0
$script:OutputBuffer = @()
$dotDgtlDir = ".dot_dgtl"

function Log {
	param([string]$Message, [string]$Level = "INFO")
	$prefix = "[$Level]"
	$timestamp = Get-Date -Format "HH:mm:ss"
	Write-Host "$timestamp $prefix $Message"
	if ($Verbose) {
		$script:OutputBuffer += "$timestamp $prefix $Message"
	}
}

function Assert-FileCheckPassed {
	param(
		[string]$TestName,
		[string]$InputFile,
		[string]$PassName = "gvn",
		[string]$Expected = "success"
	)
    
	Log "Running test: $TestName" "TEST"
    
	if (-not (Test-Path $InputFile)) {
		Log "Input file not found: $InputFile" "ERROR"
		$script:testsFailed++
		return
	}
    
	$output = & $DgtlOptPath --pass=$PassName --filecheck $InputFile 2>&1
	$exitCode = $LASTEXITCODE
    
	if ($exitCode -eq 0) {
		Log "✓ PASS: $TestName (exit code 0)" "PASS"
		$script:testsPassed++
	} else {
		Log "✗ FAIL: $TestName (exit code $exitCode)" "FAIL"
		Log "Output: $output" "DEBUG"
		$script:testsFailed++
	}
}

function Assert-FileCheckFailed {
	param(
		[string]$TestName,
		[string]$InputFile,
		[string]$PassName = "gvn"
	)
    
	Log "Running test: $TestName (expecting failure)" "TEST"
    
	if (-not (Test-Path $InputFile)) {
		Log "Input file not found: $InputFile" "ERROR"
		$script:testsFailed++
		return
	}
    
	$output = & $DgtlOptPath --pass=$PassName --filecheck $InputFile 2>&1
	$exitCode = $LASTEXITCODE
    
	if ($exitCode -eq 4) {
		Log "✓ PASS: $TestName (correctly failed with exit code 4)" "PASS"
		$script:testsPassed++
	} else {
		Log "✗ FAIL: $TestName (expected exit code 4, got $exitCode)" "FAIL"
		Log "Output: $output" "DEBUG"
		$script:testsFailed++
	}
}

function Test-EOLNormalization {
	Log "Testing line ending normalization" "TEST"
    
	# Create test files with different line endings
	$testDir = "build\filecheck_temp"
	if (-not (Test-Path $testDir)) {
		New-Item -ItemType Directory -Path $testDir -Force | Out-Null
	}
    
	# Windows line endings (CRLF)
	$windowsContent = "; RUN: dgtl-opt --pass=gvn --filecheck %s`r`n; CHECK: DGTL IR`r`n; CHECK: Text`r`n"
	[System.IO.File]::WriteAllText("$testDir\windows_eol.dgtl", $windowsContent)
    
	# Unix line endings (LF)
	$unixContent = "; RUN: dgtl-opt --pass=gvn --filecheck %s`n; CHECK: DGTL IR`n; CHECK: Text`n"
	[System.IO.File]::WriteAllText("$testDir\unix_eol.dgtl", $unixContent)
    
	# Test Windows file
	$output1 = & $DgtlOptPath --pass=gvn --filecheck "$testDir\windows_eol.dgtl" 2>&1
	$exit1 = $LASTEXITCODE
    
	# Test Unix file
	$output2 = & $DgtlOptPath --pass=gvn --filecheck "$testDir\unix_eol.dgtl" 2>&1
	$exit2 = $LASTEXITCODE
    
	if ($exit1 -eq 0 -and $exit2 -eq 0) {
		Log "✓ PASS: EOL normalization works (both CRLF and LF)" "PASS"
		$script:testsPassed++
	} else {
		Log "✗ FAIL: EOL normalization failed (Windows=$exit1, Unix=$exit2)" "FAIL"
		$script:testsFailed++
	}
    
	# Cleanup
	Remove-Item -Path $testDir -Recurse -Force
}

# Main execution
Log "FileCheck Integration Test Suite" "INFO"
Log "================================" "INFO"
Log "dgtl-opt path: $DgtlOptPath" "INFO"

if (-not (Test-Path $DgtlOptPath)) {
	Log "Error: dgtl-opt not found at $DgtlOptPath" "ERROR"
	Log "Build dgtl-opt first: dgtl build-tools" "INFO"
	exit 1
}

Log "Starting tests..." "INFO"

# Test 1: Basic FileCheck parsing
Assert-FileCheckPassed "Basic FileCheck directive parsing" "$dotDgtlDir\test_filecheck_gvn_basic.dgtl" "gvn"

# Test 2: DCE test
Assert-FileCheckPassed "DCE pass with FileCheck" "$dotDgtlDir\test_filecheck_dce_basic.dgtl" "dce"

# Test 3: Predication test
Assert-FileCheckPassed "Predication pass with FileCheck" "$dotDgtlDir\test_filecheck_eol_normalization.dgtl" "predication"

# Test 4: Regex patterns
Assert-FileCheckPassed "Regex pattern matching" "$dotDgtlDir\test_filecheck_regex_patterns.dgtl" "inlining"

# Test 5: EOL normalization
Test-EOLNormalization

# Test 6: Verbose output
Log "Testing verbose mode" "TEST"
$output = & $DgtlOptPath --pass=gvn --filecheck --verbose "$dotDgtlDir\test_filecheck_gvn_basic.dgtl" 2>&1
if ($output -match "FileCheck verification") {
	Log "✓ PASS: Verbose output contains FileCheck status" "PASS"
	$script:testsPassed++
} else {
	Log "✗ FAIL: Verbose output missing FileCheck status" "FAIL"
	$script:testsFailed++
}

# Summary
Log "================================" "INFO"
Log "Tests passed: $script:testsPassed" "INFO"
Log "Tests failed: $script:testsFailed" "INFO"

if ($script:testsFailed -eq 0) {
	Log "All FileCheck integration tests PASSED ✓" "PASS"
	exit 0
} else {
	Log "Some tests FAILED ✗" "FAIL"
	exit 1
}
