param(
    [switch]$SummaryOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

$failures = New-Object System.Collections.Generic.List[string]

function Assert-Exists {
    param(
        [string]$RelativePath
    )

    $fullPath = Join-Path $repoRoot $RelativePath
    if (-not (Test-Path $fullPath)) {
        $failures.Add("missing required file: $RelativePath")
    }
}

function Assert-ContainsText {
    param(
        [string]$RelativePath,
        [string]$Pattern,
        [string]$Description
    )

    $fullPath = Join-Path $repoRoot $RelativePath
    if (-not (Test-Path $fullPath)) {
        $failures.Add("cannot validate pattern '$Description' because file is missing: $RelativePath")
        return
    }

    $match = Select-String -Path $fullPath -Pattern $Pattern -Quiet
    if (-not $match) {
        $failures.Add("missing expected policy pattern '$Description' in $RelativePath")
    }
}

# Required policy documents.
Assert-Exists 'docs/api/PUBLIC_API_TIERS_AND_CONTRACT.md'
Assert-Exists 'docs/api/PUBLIC_API_RELEASE_POLICY.md'
Assert-Exists 'docs/api/FRONTEND_CONFORMANCE_KIT.md'

# Required public API headers.
Assert-Exists 'include/dgtl/api/Api_all.hpp'
Assert-Exists 'include/dgtl/api/Frontend.hpp'
Assert-Exists 'include/dgtl/api/Frontend_Pipeline.hpp'
Assert-Exists 'include/dgtl/api/Frontend_Payload.hpp'
Assert-Exists 'include/dgtl/api/Backend_Driver_API.hpp'

# Stable umbrella wiring.
Assert-ContainsText 'include/dgtl/all.hpp' 'dgtl/api/Api_all\.hpp' 'top-level umbrella must include public API umbrella'
Assert-ContainsText 'include/dgtl/api/Api_all.hpp' 'dgtl/api/Frontend_Payload\.hpp' 'public API umbrella must expose frontend payload'

# Universal payload + driver ingestion contract anchors.
Assert-ContainsText 'include/dgtl/core/Backend_Driver_API.hpp' 'frontend_payload_path' 'backend driver request must carry frontend payload path'
Assert-ContainsText 'include/dgtl/core/Backend_Driver_API.hpp' 'emit_frontend_module\(' 'frontend module emission helper must exist'
Assert-ContainsText 'src/main.cpp' '--frontend-payload' 'DGTL driver must accept frontend payload CLI option'
Assert-ContainsText 'src/main.cpp' 'build_ir_from_frontend_payload\(' 'DGTL driver must load IR from frontend payload'

# Conformance test anchors for payload path.
Assert-Exists 'tests/functional/pipeline/test_frontend_payload_io.cpp'
Assert-ContainsText 'tests/test_suite_main.cpp' 'run_test_frontend_payload_io' 'aggregate suite must include frontend payload IO test'
Assert-ContainsText 'dgtl.ps1' 'test_frontend_payload_io\.cpp' 'test build script must include frontend payload IO source'

if ($failures.Count -gt 0) {
    Write-Host '[api-policy] FAIL' -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host "  - $failure"
    }
    exit 1
}

if (-not $SummaryOnly) {
    Write-Host '[api-policy] PASS' -ForegroundColor Green
}

exit 0
