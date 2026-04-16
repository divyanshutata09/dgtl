param(
    [switch]$UpdateGolden,
    [string]$Executable = '.\build\dgtl_main.exe'
)

$ErrorActionPreference = 'Stop'

$commandsDir = $PSScriptRoot
$myDocsDir = Split-Path -Parent $commandsDir
$parentOfMyDocs = Split-Path -Parent $myDocsDir
$repoRoot = if ((Split-Path -Leaf $parentOfMyDocs) -ieq 'docs') {
    Split-Path -Parent $parentOfMyDocs
} else {
    $parentOfMyDocs
}

$logsDir = Join-Path $repoRoot 'build\logs'
$goldenDir = Join-Path $commandsDir 'golden'

function Get-ConformanceFingerprint {
    param(
        [string]$ExePath,
        [string[]]$GoldenPaths
    )
    if (-not (Test-Path $ExePath)) { return '' }
    $exeItem = Get-Item $ExePath
    $parts = @("$ExePath|$($exeItem.LastWriteTimeUtc.Ticks)|$($exeItem.Length)")
    foreach ($g in $GoldenPaths) {
        if (Test-Path $g) {
            $gi = Get-Item $g
            $parts += "$g|$($gi.LastWriteTimeUtc.Ticks)|$($gi.Length)"
        } else {
            $parts += "$g|missing"
        }
    }
    return ($parts -join ';')
}

function Normalize-ConformanceText {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Text
    )

    $normalized = $Text -replace "`r", ""
    $normalized = [regex]::Replace($normalized, '(avg_us|opt_us|emit_us|total_us|exec_us)=[0-9]+(?:\.[0-9]+)?', '$1=<normalized>')
    return $normalized.TrimEnd()
}

New-Item -ItemType Directory -Force $logsDir | Out-Null
New-Item -ItemType Directory -Force $goldenDir | Out-Null

$exe = $Executable
if (-not [System.IO.Path]::IsPathRooted($exe)) {
    $exe = Join-Path $repoRoot $exe
}
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

$goldenPaths = $checks | ForEach-Object { Join-Path $goldenDir "$($_.Name).golden.txt" }
$exeBaseName = [System.IO.Path]::GetFileNameWithoutExtension($exe)
$cacheFile   = Join-Path $logsDir "conformance_${exeBaseName}.cache"
$fingerprint = Get-ConformanceFingerprint -ExePath $exe -GoldenPaths $goldenPaths

if (-not $UpdateGolden -and (Test-Path $cacheFile)) {
    $cached = Get-Content $cacheFile -Raw -ErrorAction SilentlyContinue
    if ($null -ne $cached -and $cached.Trim() -eq $fingerprint) {
        Write-Host '[cache] Conformance up-to-date. Skipping backend runs.' -ForegroundColor Cyan
        Write-Host 'Conformance PASSED for all backends.' -ForegroundColor Green
        exit 0
    }
}

$failed = @()

foreach ($c in $checks) {
    $log = Join-Path $logsDir "conformance_$($c.Name).log"
    $golden = Join-Path $goldenDir "$($c.Name).golden.txt"
    Write-Host "[CHECK] $($c.Name)"

    $output = & $exe @($c.Args) 2>&1 | Out-String
    $normalized = Normalize-ConformanceText -Text $output
    ($output -replace "`r","") | Set-Content -Path $log

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

    $goldenText = Get-Content $golden -Raw
    $goldenCompare = Normalize-ConformanceText -Text $goldenText

    $ok = $true

    if ($normalized -ne $goldenCompare) {
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

if (-not $UpdateGolden) {
    $fingerprint | Set-Content -Path $cacheFile
}
Write-Host 'Conformance PASSED for all backends.' -ForegroundColor Green
exit 0
