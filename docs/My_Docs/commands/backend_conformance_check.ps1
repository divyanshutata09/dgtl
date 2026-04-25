param(
    [switch]$UpdateGolden,
    [string]$Executable = '.\build\dgtl_main.exe'
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false

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
    $normalized = [regex]::Replace($normalized, '(?m)^\s*\[(PASS|FAIL)\]\s+constant-fold\s+::.*$', '  [$1] constant-fold :: <normalized>')
    $normalized = [regex]::Replace($normalized, '(?m)^\s*\[(PASS|FAIL)\]\s+ai-cost\s+::.*$', '  [$1] ai-cost :: <normalized>')
    $normalized = [regex]::Replace($normalized, '(?m)^\s*\[(PASS|FAIL)\]\s+latency\s+::.*$', '  [$1] latency :: <normalized>')
    $normalized = [regex]::Replace($normalized, '(?m)^(00\s+61\s+73\s+6D)(?:\s+[0-9A-Fa-f]{2}){4,}\s*$', '$1 <hex-dump>')
    $normalized = [regex]::Replace($normalized, '(?m)^([0-9A-Fa-f]{2}(?:\s+[0-9A-Fa-f]{2}){7,})\s*$', '<hex-dump>')
    $normalized = [regex]::Replace($normalized, 'output_size=[0-9]+', 'output_size=<normalized>')
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

    $stdoutTmp = Join-Path $logsDir "conformance_$($c.Name).stdout.tmp"
    $stderrTmp = Join-Path $logsDir "conformance_$($c.Name).stderr.tmp"
    if (Test-Path $stdoutTmp) { Remove-Item $stdoutTmp -Force }
    if (Test-Path $stderrTmp) { Remove-Item $stderrTmp -Force }

    $proc = Start-Process -FilePath $exe `
        -ArgumentList @($c.Args) `
        -NoNewWindow `
        -Wait `
        -PassThru `
        -RedirectStandardOutput $stdoutTmp `
        -RedirectStandardError $stderrTmp

    $stdoutText = if (Test-Path $stdoutTmp) { Get-Content -Path $stdoutTmp -Raw } else { '' }
    $stderrText = if (Test-Path $stderrTmp) { Get-Content -Path $stderrTmp -Raw } else { '' }

    $output = $stdoutText
    if ($stderrText -ne '') {
        if ($output -ne '' -and -not $output.EndsWith("`n")) {
            $output += "`n"
        }
        $output += $stderrText
    }

    $backendExitCode = $proc.ExitCode
    $normalized = Normalize-ConformanceText -Text $output
    ($output -replace "`r","") | Set-Content -Path $log

    if ($UpdateGolden) {
        if ($backendExitCode -ne 0) {
            Write-Host "  backend exit code: $backendExitCode" -ForegroundColor Red
            Write-Host "  FAIL (see $log)" -ForegroundColor Red
            $failed += $c.Name
            continue
        }
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

    if ($backendExitCode -ne 0) {
        Write-Host "  backend exit code: $backendExitCode" -ForegroundColor Red
        $ok = $false
    }

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
