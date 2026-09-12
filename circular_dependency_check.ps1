powershell -NoProfile -ExecutionPolicy Bypass -Command @'
$files = Get-ChildItem -Path "include" -Filter "*.ixx" -Recurse
$modToFile = @{}
$fileToMod = @{}
$deps = @{}

foreach ($f in $files) {
    $content = Get-Content $f.FullName -Raw
    $modName = $null
    if ($content -match '^\s*export\s+module\s+([a-zA-Z0-9_\.]+)\s*;') {
        $modName = $matches[1]
    } elseif ($content -match '^\s*module\s+([a-zA-Z0-9_\.]+)\s*;') {
        $modName = $matches[1]
    }
    if ($modName) {
        $modToFile[$modName] = $f.FullName
        $fileToMod[$f.FullName] = $modName
    }
}

foreach ($f in $files) {
    $srcMod = $fileToMod[$f.FullName]
    if (-not $srcMod) { continue }
    if (-not $deps.ContainsKey($srcMod)) { $deps[$srcMod] = [System.Collections.Generic.HashSet[string]]::new() }

    $lines = Get-Content $f.FullName
    foreach ($line in $lines) {
        if ($line -match '^\s*(?:export\s+)?import\s+([a-zA-Z0-9_\.]+)\s*;') {
            $imported = $matches[1]
            if ($imported -notmatch '^(std|std\..*)$' -and $modToFile.ContainsKey($imported)) {
                $null = $deps[$srcMod].Add($imported)
            }
        }
    }
}

Write-Host "Total scanned C++26 modules: $($modToFile.Count)" -ForegroundColor Cyan

# Tarjan's / DFS cycle detector with recursion stack tracing
$visited = @{}
$recStack = @{}
$cyclesFound = 0

function Find-Cycle($u, $path) {
    $global:visited[$u] = $true
    $global:recStack[$u] = $true
    $currentPath = $path + @($u)

    if ($global:deps.ContainsKey($u)) {
        foreach ($v in $global:deps[$u]) {
            if (-not $global:visited.ContainsKey($v) -or -not $global:visited[$v]) {
                Find-Cycle $v $currentPath
            } elseif ($global:recStack.ContainsKey($v) -and $global:recStack[$v]) {
                $global:cyclesFound++
                $cycleIdx = $currentPath.IndexOf($v)
                $cycleSlice = $currentPath[$cycleIdx..($currentPath.Length - 1)] + @($v)
                Write-Host "[CYCLE DETECTED] $($cycleSlice -join ' -> ')" -ForegroundColor Red
            }
        }
    }
    $global:recStack[$u] = $false
}

foreach ($m in $modToFile.Keys) {
    if (-not $visited.ContainsKey($m) -or -not $visited[$m]) {
        Find-Cycle $m @()
    }
}

if ($cyclesFound -eq 0) {
    Write-Host "[SUCCESS] 0 circular dependencies found! Module dependency graph is a valid DAG." -ForegroundColor Green
} else {
    Write-Host "[FAILED] Found $cyclesFound circular dependency cycles!" -ForegroundColor Red
}
'@