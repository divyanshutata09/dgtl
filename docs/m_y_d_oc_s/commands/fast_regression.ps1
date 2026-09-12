param(
    [switch]$VerboseCompile
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")

$dgtl = Join-Path $root 'dgtl.ps1'
if (-not (Test-Path $dgtl)) {
    throw "Missing command runner: $dgtl"
}

& powershell -ExecutionPolicy Bypass -File $dgtl crun
exit $LASTEXITCODE
