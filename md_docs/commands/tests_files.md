$ErrorActionPreference = 'Stop'

New-Item -ItemType Directory -Force .\build | Out-Null
New-Item -ItemType Directory -Force .\build\logs | Out-Null

$srcFiles = Get-ChildItem .\src -Recurse -Filter *.cpp |
    Select-Object -ExpandProperty FullName

$testFiles = Get-ChildItem .\tests -Recurse -Filter *.cpp |
    Sort-Object FullName

$ok = 0
$failed = 0
$failList = @()
$start = Get-Date

foreach ($test in $testFiles) {
    $name = $test.BaseName
    $outExe = ".\build\$name.exe"
    $logFile = ".\build\logs\$name.compile.log"

    Write-Host "[BUILD] $name"

    & g++ -std=c++26 -O2 $test.FullName $srcFiles -I .\include -I .\src -o $outExe 2> $logFile

    if ($LASTEXITCODE -eq 0) {
        $ok++
        Write-Host "[OK] $outExe" -ForegroundColor Green
    } else {
        $failed++
        $failList += $name
        Write-Host "[FAIL] $name (see $logFile)" -ForegroundColor Red
    }
}

$elapsed = [Math]::Round(((Get-Date) - $start).TotalSeconds, 2)

Write-Host ""
Write-Host "========== BUILD SUMMARY =========="
Write-Host "Total tests : $($testFiles.Count)"
Write-Host "Succeeded   : $ok"
Write-Host "Failed      : $failed"
Write-Host "Elapsed (s) : $elapsed"

if ($failed -gt 0) {
    Write-Host "Failed tests:" -ForegroundColor Yellow
    $failList | ForEach-Object { Write-Host " - $_" -ForegroundColor Yellow }
    exit 1
}

exit 0