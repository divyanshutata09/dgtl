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
$debugLogsDir = Join-Path $root 'debug_logs'
$sessionLogDir = Join-Path $debugLogsDir 'sessions'
$debuggerSource = Join-Path $root 'tools\cpp\dgtl_debugger_main.cpp'

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

New-Item -ItemType Directory -Force -Path $debugLogsDir | Out-Null
New-Item -ItemType Directory -Force -Path $sessionLogDir | Out-Null

$latestMd = Join-Path $debugLogsDir 'latest-error.md'
$latestTxt = Join-Path $debugLogsDir 'latest-error.txt'
$latestJson = Join-Path $debugLogsDir 'latest-error.json'

$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$lastSessionLog = Join-Path $sessionLogDir 'last-session.log'
$lastNonDebugLog = Join-Path $sessionLogDir 'last-nondebug-session.log'
$timestampedLog = Join-Path $sessionLogDir ("dgtl-session-{0}.log" -f $timestamp)
$commandText = if ($Args.Count -gt 0) { "dgtl " + ($Args -join ' ') } else { 'dgtl' }
$isDebugCommand = $commandText -match '^dgtl\s+debug(\s|$)'

Set-Content -Path (Join-Path $sessionLogDir 'last-command.txt') -Value $commandText -Encoding UTF8

# Forward all arguments to the C++ orchestrator while teeing output into canonical debugger logs
$previousErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
Push-Location $root
try {
    & $cppOrchestratorExe @Args 2>&1 | Tee-Object -FilePath $lastSessionLog | Tee-Object -FilePath $timestampedLog
    $exitCode = $LASTEXITCODE
}
finally {
    Pop-Location
}
$ErrorActionPreference = $previousErrorActionPreference

Set-Content -Path (Join-Path $sessionLogDir 'last-exit-code.txt') -Value ([string]$exitCode) -Encoding UTF8
if (-not $isDebugCommand) {
    Copy-Item -Path $lastSessionLog -Destination $lastNonDebugLog -Force
    Set-Content -Path (Join-Path $sessionLogDir 'last-nondebug-command.txt') -Value $commandText -Encoding UTF8
    Set-Content -Path (Join-Path $sessionLogDir 'last-nondebug-exit-code.txt') -Value ([string]$exitCode) -Encoding UTF8
}

if (-not $isDebugCommand -and $exitCode -ne 0) {
    $debuggerExe = Join-Path $root 'build\tools\dgtl-debugger.exe'
    $buildLogDir = Join-Path $root 'build\logs'
    $compilerLog = Join-Path $buildLogDir 'last-compiler-invocations.log'
    $preservedCompilerLog = Join-Path $sessionLogDir 'last-compiler-invocations.failure.log'
    $debugTimestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $latestMd = Join-Path $debugLogsDir 'latest-error.md'
    $latestTxt = Join-Path $debugLogsDir 'latest-error.txt'
    $latestJson = Join-Path $debugLogsDir 'latest-error.json'
    $tsMd = Join-Path $debugLogsDir ("error-{0}.md" -f $debugTimestamp)
    $tsTxt = Join-Path $debugLogsDir ("error-{0}.txt" -f $debugTimestamp)
    $tsJson = Join-Path $debugLogsDir ("error-{0}.json" -f $debugTimestamp)

    if (Test-Path $debuggerExe) {
        & $debuggerExe --relative-paths --export $latestMd | Out-Null
        & $debuggerExe --relative-paths --export $latestTxt | Out-Null
        & $debuggerExe --relative-paths --json --export $latestJson | Out-Null
        Copy-Item -Path $latestMd -Destination $tsMd -Force
        Copy-Item -Path $latestTxt -Destination $tsTxt -Force
        Copy-Item -Path $latestJson -Destination $tsJson -Force
    }
}
exit $exitCode
