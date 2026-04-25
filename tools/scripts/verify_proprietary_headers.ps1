param(
    [string]$Root = ".",
    [string[]]$Include = @("*.cpp", "*.hpp", "*.h", "*.c", "*.cc"),
    [string[]]$ExcludeRegex = @("[\\/]build[\\/]", "[\\/]docs[\\/]", "[\\/]My_Docs[\\/]"),
    [int]$HeaderScanLines = 12,
    [switch]$SummaryOnly,
    [switch]$Json,
    [string]$JsonOut,
    [switch]$RelativePaths
)

$ErrorActionPreference = "Stop"

$requiredHeader = "Copyright (c) 2026 TataG Systems. All rights reserved."

if ($HeaderScanLines -lt 1) {
    throw "HeaderScanLines must be >= 1."
}

$rootPath = (Resolve-Path -Path $Root).Path

$files = Get-ChildItem -Path $rootPath -Recurse -File -Include $Include

if ($ExcludeRegex -and $ExcludeRegex.Count -gt 0) {
    $files = $files | Where-Object {
        $path = $_.FullName
        -not ($ExcludeRegex | Where-Object { $path -match $_ })
    }
}

if (-not $files) {
    if ($Json) {
        $emptyResult = [ordered]@{
            status = "ok"
            root = $rootPath
            scannedCount = 0
            missingCount = 0
            missingFiles = @()
            requiredHeader = $requiredHeader
            generatedAtUtc = (Get-Date).ToUniversalTime().ToString("o")
        }
        $jsonText = $emptyResult | ConvertTo-Json -Depth 6
        if ($JsonOut) {
            $outDir = Split-Path -Path $JsonOut -Parent
            if ($outDir) {
                New-Item -ItemType Directory -Path $outDir -Force | Out-Null
            }
            $jsonText | Set-Content -Path $JsonOut -Encoding utf8
        }
        Write-Output $jsonText
    } else {
        Write-Host "No source files found."
    }
    exit 0
}

$missing = New-Object System.Collections.Generic.List[string]

foreach ($file in $files) {
    $head = Get-Content -Path $file.FullName -TotalCount $HeaderScanLines -ErrorAction Stop
    $joined = ($head -join "`n")
    if ($joined -notmatch [Regex]::Escape($requiredHeader)) {
        if ($RelativePaths) {
            $relative = $file.FullName.Substring($rootPath.Length).TrimStart('\','/')
            $missing.Add($relative)
        } else {
            $missing.Add($file.FullName)
        }
    }
}

$sortedMissing = @($missing | Sort-Object)
$result = [ordered]@{
    status = $(if ($sortedMissing.Count -eq 0) { "ok" } else { "failed" })
    root = $rootPath
    scannedCount = @($files).Count
    missingCount = $sortedMissing.Count
    missingFiles = $sortedMissing
    requiredHeader = $requiredHeader
    generatedAtUtc = (Get-Date).ToUniversalTime().ToString("o")
}

if ($Json) {
    $jsonText = $result | ConvertTo-Json -Depth 6
    if ($JsonOut) {
        $outDir = Split-Path -Path $JsonOut -Parent
        if ($outDir) {
            New-Item -ItemType Directory -Path $outDir -Force | Out-Null
        }
        $jsonText | Set-Content -Path $JsonOut -Encoding utf8
    }
    Write-Output $jsonText
} else {
    if ($result.missingCount -eq 0) {
        Write-Host "OK: Proprietary header found in all scanned source files."
    } else {
        Write-Host "Missing proprietary header in $($result.missingCount) file(s) out of $($result.scannedCount) scanned:"
        if (-not $SummaryOnly) {
            $sortedMissing | ForEach-Object { Write-Host " - $_" }
        }
    }
}

if ($result.missingCount -eq 0) {
    exit 0
}

exit 1
