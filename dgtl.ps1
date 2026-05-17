# dgtl.ps1
# Thin wrapper that forwards all commands to the C++ orchestrator
# This file is now just a compatibility shim; all functionality has been migrated to tools/cpp/dgtl_main.cpp

param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Args
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$cppOrchestratorExe = Join-Path $root 'build\tools\dgtl.exe'
$cppOrchestratorSource = Join-Path $root 'tools\cpp\dgtl_main.cpp'

$needsRebuild = -not (Test-Path $cppOrchestratorExe)
if (-not $needsRebuild) {
    $srcTime = (Get-Item $cppOrchestratorSource).LastWriteTimeUtc
    $exeTime = (Get-Item $cppOrchestratorExe).LastWriteTimeUtc
    if ($srcTime -gt $exeTime) {
        $needsRebuild = $true
    }
}

# Ensure the C++ orchestrator is compiled and up to date
if ($needsRebuild) {
    Write-Host "[dgtl] Compiling C++ orchestrator (dgtl.exe)..."
    $gppPath = 'g++'  # TODO: implement full GCC path resolution if needed
    $buildToolsDir = Join-Path $root 'build\tools'
    New-Item -ItemType Directory -Force -Path $buildToolsDir | Out-Null
    
    & $gppPath -std=c++26 -O2 -pipe $cppOrchestratorSource -o $cppOrchestratorExe
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to compile C++ orchestrator: $cppOrchestratorExe"
    }
    Write-Host "[dgtl] Wrote $cppOrchestratorExe"
}

# Forward all arguments to the C++ orchestrator
& $cppOrchestratorExe @Args
exit $LASTEXITCODE
