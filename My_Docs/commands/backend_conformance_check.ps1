param(
    [switch]$UpdateGolden
)

$ErrorActionPreference = 'Stop'

New-Item -ItemType Directory -Force .\build\logs | Out-Null
New-Item -ItemType Directory -Force .\md_docs\commands\golden | Out-Null

$exe = '.\build\dgtl_main.exe'
if (-not (Test-Path $exe)) {
    throw "Missing $exe. Build first."
}

$checks = @(
    @{
        Name = 'arm64'
        Args = @('--backend','arm64')
        MustContain = @('.arch armv8-a','_DGTL_entry:')
        MustNotContain = @('DGTL-EMIT-ERR','Unhandled')
    },
    @{
        Name = 'x64'
        Args = @('--backend','x64')
        MustContain = @('Backend=x64','Nodes=')
        MustNotContain = @('STRIKE ERROR')
    },
    @{
        Name = 'nasm'
        Args = @('--backend','nasm')
        MustContain = @('DGTL_kernel_entry','section .text')
        MustNotContain = @('[ERROR] Unsupported Op')
    },
    @{
        Name = 'wasm'
        Args = @('--backend','wasm')
        MustContain = @('Backend=wasm','00 61 73 6D')
        MustNotContain = @('WASM emission produced no output')
    }
)

$failed = @()

foreach ($c in $checks) {
    $log = ".\build\logs\conformance_$($c.Name).log"
    $golden = ".\md_docs\commands\golden\$($c.Name).golden.txt"
    Write-Host "[CHECK] $($c.Name)"

    $output = & $exe @($c.Args) 2>&1 | Out-String
    $normalized = ($output -replace "`r","")
    $normalized | Set-Content -Path $log

    if ($UpdateGolden) {
        $normalized | Set-Content -Path $golden
        Write-Host "  golden updated: $golden" -ForegroundColor Yellow
        continue
    }

    if (-not (Test-Path $golden)) {
        Write-Host "  missing golden: $golden" -ForegroundColor Red
        $failed += $c.Name
        continue
    }

    $goldenText = (Get-Content $golden -Raw) -replace "`r", ""
    $normalizedCompare = $normalized.TrimEnd()
    $goldenCompare = $goldenText.TrimEnd()

    $ok = $true

    if ($normalizedCompare -ne $goldenCompare) {
        Write-Host "  golden mismatch" -ForegroundColor Red
        $ok = $false
    }

    foreach ($needle in $c.MustContain) {
        if ($normalized -notmatch [regex]::Escape($needle)) {
            Write-Host "  missing: $needle" -ForegroundColor Red
            $ok = $false
        }
    }

    foreach ($bad in $c.MustNotContain) {
        if ($normalized -match [regex]::Escape($bad)) {
            Write-Host "  forbidden found: $bad" -ForegroundColor Red
            $ok = $false
        }
    }

    if ($ok) {
        Write-Host "  PASS" -ForegroundColor Green
    } else {
        Write-Host "  FAIL (see $log)" -ForegroundColor Red
        $failed += $c.Name
    }
}

Write-Host ''
if ($failed.Count -gt 0) {
    Write-Host 'Conformance FAILED for:' -ForegroundColor Red
    $failed | ForEach-Object { Write-Host " - $_" -ForegroundColor Red }
    exit 1
}

Write-Host 'Conformance PASSED for all backends.' -ForegroundColor Green
exit 0
