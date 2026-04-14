param(
    [switch]$VerboseCompile
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
Push-Location $root

try {
    Write-Host "[1/2] Building dgtl_main.exe ..."

    $srcFiles = Get-ChildItem -Recurse -File (Join-Path $root "src") -Filter *.cpp |
        ForEach-Object { '"' + $_.FullName + '"' }

    if ($srcFiles.Count -eq 0) {
        throw "No source files found under src/."
    }

    $compileCmd = "g++ -std=c++2b -I include " + ($srcFiles -join ' ') + " -o dgtl_main.exe"
    if ($VerboseCompile) {
        Write-Host "Compile command: $compileCmd"
    }

    Invoke-Expression $compileCmd

    Write-Host "[2/2] Running backend conformance ..."
    $conformanceScript = Join-Path $root "md_docs\commands\backend_conformance_check.ps1"
    if (-not (Test-Path $conformanceScript)) {
        throw "Missing conformance script: $conformanceScript"
    }

    & powershell -ExecutionPolicy Bypass -File $conformanceScript

    if ($LASTEXITCODE -ne 0) {
        throw "Conformance script failed with exit code $LASTEXITCODE"
    }

    Write-Host "FAST REGRESSION PASSED"
    exit 0
}
catch {
    Write-Error $_
    exit 1
}
finally {
    Pop-Location
}
