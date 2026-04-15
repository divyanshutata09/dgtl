$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$profilePath = $PROFILE.CurrentUserCurrentHost
$profileDir = Split-Path -Parent $profilePath

if (-not (Test-Path $profileDir)) {
    New-Item -ItemType Directory -Force -Path $profileDir | Out-Null
}

if (-not (Test-Path $profilePath)) {
    New-Item -ItemType File -Force -Path $profilePath | Out-Null
}

$beginMarker = '# DGTL_COMMAND_BEGIN'
$endMarker = '# DGTL_COMMAND_END'

$existing = Get-Content -Path $profilePath -Raw
$escapedRoot = $root.Path.Replace("'", "''")
$block = @"
$beginMarker
function dgtl {
    & '$escapedRoot\dgtl.ps1' @args
}
$endMarker
"@

$legacyPattern = '(?ms)^function\s+dgtl\s*\{.*?^\}'

if ($existing -match [regex]::Escape($beginMarker) + '[\s\S]*' + [regex]::Escape($endMarker)) {
    $updated = [regex]::Replace(
        $existing,
        [regex]::Escape($beginMarker) + '[\s\S]*?' + [regex]::Escape($endMarker),
        [System.Text.RegularExpressions.MatchEvaluator]{ param($m) $block }
    )
    Set-Content -Path $profilePath -Value $updated
} elseif ($existing -match $legacyPattern) {
    $updated = [regex]::Replace(
        $existing,
        $legacyPattern,
        [System.Text.RegularExpressions.MatchEvaluator]{ param($m) $block.TrimEnd() }
    )
    Set-Content -Path $profilePath -Value $updated
} else {
    $prefix = if ([string]::IsNullOrWhiteSpace($existing)) { '' } else { $existing.TrimEnd() + "`r`n`r`n" }
    Set-Content -Path $profilePath -Value ($prefix + $block + "`r`n")
}

Write-Host "Installed 'dgtl' into $profilePath"
Write-Host 'Open a new PowerShell session, then run: dgtl help'