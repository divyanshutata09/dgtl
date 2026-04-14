$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
#pragma warning disable PSUseApprovedVerbs

$root = 'D:\TataG\DGTL'
$myDocs = Join-Path $root 'My_Docs'
$includeRoot = Join-Path $root 'include\dgtl'
$topFolders = @('analysis','core','graph','ir','isa','jit','pass','pipeline')

function Select-MatchList([string[]]$lines,[string]$pattern,[int]$max=200){
    return @($lines | Where-Object { $_ -match $pattern } | ForEach-Object { $_.Trim() } | Select-Object -Unique | Select-Object -First $max)
}

function Get-Intent([string]$path){
    $p = $path.ToLowerInvariant()
    if($p -match 'emitter|encoder|lowering|linker|target'){ return 'Lower IR into target-specific representation while preserving semantic correctness and backend constraints.' }
    if($p -match 'jit|runtime|orchestrator|scheduler'){ return 'Drive runtime execution, specialization, and backend dispatch with predictable safety checks.' }
    if($p -match 'pass|transform|legalizer|optimizer|dce|gvn|folder'){ return 'Apply correctness-preserving transformations to improve cost, latency, and code quality.' }
    if($p -match 'analysis|profiler|liveness|cost|dependency'){ return 'Compute signals and invariants used to guide optimization and safe scheduling decisions.' }
    if($p -match 'graph|region|fusion|edge'){ return 'Model execution structure and dependencies to enable region-level scheduling and fusion.' }
    if($p -match 'ir|dialect|stream|node|type'){ return 'Represent program semantics in compact, verifiable IR forms suitable for transformation and emission.' }
    if($p -match 'core|context|target|formal|memory|arena|symbol'){ return 'Provide foundational contracts for memory, validation, target behavior, and shared compiler state.' }
    return 'Provide reusable infrastructure used by the DGTL compilation and execution pipeline.'
}

function New-ControlFlowGraph([bool]$hasTx){
    $n1='Inputs'; $n2='Validate'; $n3='Analyze'; $n4='Transform'; $n5='Finalize'
    if($hasTx){ $n3='Begin Transaction'; $n4='Mutate Shadow/Core'; $n5='Commit or Rollback' }
    return @(
        '```mermaid',
        'flowchart TD',
        "A[$n1] --> B[$n2]",
        "B --> C[$n3]",
        "C --> D[$n4]",
        "D --> E[$n5]",
        'B --> X[Guard and Early Exit]',
        '```'
    )
}

function New-ControlFlowGraphAdvanced([bool]$hasTx,[bool]$hasLoop,[bool]$hasSwitch,[bool]$hasThrow,[bool]$hasGuard){
    $n1='Inputs'; $n2='Validate'; $n3='Analyze'; $n4='Transform'; $n5='Finalize'
    if($hasTx){ $n3='Begin Transaction'; $n4='Mutate Shadow/Core'; $n5='Commit or Rollback' }
    $map = @(
        '```mermaid',
        'flowchart TD',
        "A[$n1] --> B[$n2]",
        "B --> C[$n3]",
        "C --> D[$n4]",
        "D --> E[$n5]"
    )
    if($hasLoop){
        $map += 'C --> L[Iterative Walk Over IR or Regions]'
        $map += 'L --> D'
    }
    if($hasSwitch){
        $map += 'C --> S[Opcode or Policy Dispatch Switch]'
        $map += 'S --> D'
    }
    if($hasGuard){
        $map += 'B --> X[Guard and Early Exit]'
    }
    if($hasThrow){
        $map += 'D --> T[Error Path or Trap]'
    }
    $map += '```'
    return $map
}

function Get-CoreLogic([string]$fileName,[string[]]$lines,[bool]$txMarkers){
    $logic = @(
        '- Inputs are validated early and non-actionable states return fast.',
        '- Main logic operates on typed IR structures and explicit state transitions.'
    )

    $hasLoop = @($lines | Where-Object { $_ -match '^\s*for\s*\(|^\s*while\s*\(' }).Count -gt 0
    $hasSwitch = @($lines | Where-Object { $_ -match '^\s*switch\s*\(' }).Count -gt 0
    $hasHash = @($lines | Where-Object { $_ -match 'intent_hash|calculate_hash|compute_master_hash_bits' }).Count -gt 0
    $hasTrap = @($lines | Where-Object { $_ -match 'throw\s+|trap|rollback' }).Count -gt 0

    if($hasLoop){ $logic += '- Traversal is iterative to keep performance linear over stream and region size.' }
    if($hasSwitch){ $logic += '- Dispatch logic uses explicit switching to keep opcode or policy behavior transparent.' }
    if($hasHash){ $logic += '- Structural hashes are refreshed or verified to preserve correctness across rewrites.' }
    if($txMarkers){
        $logic += '- Transactional semantics are present: begin, mutate, validate, then commit or rollback.'
    } else {
        $logic += '- The design favors deterministic transformations and bounded side effects.'
    }
    if($hasTrap){ $logic += '- Defensive error paths are explicit to prevent silent corruption propagation.' }

    return $logic
}

function Get-FileSpecificWhys([string]$path,[string[]]$funcs,[string[]]$lines){
    $p = $path.ToLowerInvariant()
    $out = @()

    if($p -match 'liveness'){ $out += '- This file formalizes live-range truth so downstream passes avoid removing still-required values.' }
    if($p -match 'cost|profiler'){ $out += '- Predictive scoring is centralized here to make pass selection decisions explainable and stable.' }
    if($p -match 'dependency|memory'){ $out += '- Memory dependency logic is isolated to prevent unsafe reorderings in optimization passes.' }
    if($p -match 'region|graph|fusion'){ $out += '- Region-level modeling here enables larger-than-instruction scheduling and fusion decisions.' }
    if($p -match 'stream|node|dialect|types'){ $out += '- IR contracts here define semantic invariants every pass and emitter relies on.' }
    if($p -match 'emitter|encoder|target|regalloc|lowering'){ $out += '- Backend lowering is explicit here to keep architecture behavior auditable and deterministic.' }
    if($p -match 'orchestrator|runner|executor|scheduler|tiered'){ $out += '- Runtime orchestration is partitioned to separate policy selection from execution mechanics.' }
    if($p -match 'dce|gvn|folder|legalizer|transform|speculative|inliner|unswitch|reordering'){ $out += '- Transformation safety is bounded here through checks that preserve IR validity and intent.' }

    $hasSentinel = @($lines | Where-Object { $_ -match 'SENTINEL|validate_shadow_integrity|intent_hash' }).Count -gt 0
    if($hasSentinel){ $out += '- Sentinel-style integrity checks are intentionally present to detect corruption before commit stages.' }

    if($out.Count -eq 0){
        $out += '- This file captures a focused subsystem contract so complexity does not leak across module boundaries.'
    }

    return $out
}

function Get-Invariants([string[]]$lines){
    $inv = @()
    if(@($lines | Where-Object { $_ -match 'validate_shadow_integrity' }).Count -gt 0){ $inv += '- Shadow integrity must validate before state is committed.' }
    if(@($lines | Where-Object { $_ -match 'begin_transaction|commit_transaction|rollback_transaction' }).Count -gt 0){ $inv += '- Transaction boundaries must remain balanced across success and failure paths.' }
    if(@($lines | Where-Object { $_ -match 'intent_hash|calculate_hash|compute_master_hash_bits' }).Count -gt 0){ $inv += '- Intent hash values must be consistent with opcode and operands after mutation.' }
    if(@($lines | Where-Object { $_ -match 'INVALID_SLOT|is_null\(|size\(\)\s*==\s*0' }).Count -gt 0){ $inv += '- Null or invalid sentinel states must be short-circuited early.' }
    if(@($lines | Where-Object { $_ -match 'throw\s+|runtime_error|bad_alloc' }).Count -gt 0){ $inv += '- Error paths must fail loudly to avoid silent semantic drift.' }
    if($inv.Count -eq 0){ $inv += '- Public operations should preserve deterministic behavior for equivalent inputs.' }
    return $inv
}

$generated = 0
foreach($top in $topFolders){
    $srcTop = Join-Path $includeRoot $top
    if(-not (Test-Path $srcTop)){ continue }

    $headers = Get-ChildItem -Path $srcTop -Recurse -File -Filter *.hpp
    foreach($h in $headers){
        $rel = $h.FullName.Substring($srcTop.Length).TrimStart('\\')
        $relNorm = $rel -replace '\\','/'
        $relDir = Split-Path $rel -Parent
        $outDir = Join-Path (Join-Path $myDocs $top) $relDir
        if(-not (Test-Path $outDir)){ New-Item -ItemType Directory -Path $outDir -Force | Out-Null }
        $outPath = Join-Path $outDir ($h.Name + '.md')

        $lines = Get-Content -Path $h.FullName
        $includes = @(Select-MatchList $lines '^\s*#include\s+[<\"].+[>\"]' 200)
        $classes = @(Select-MatchList $lines '^\s*class\s+[A-Za-z_][A-Za-z0-9_]*' 120)
        $structs = @(Select-MatchList $lines '^\s*struct\s+[A-Za-z_][A-Za-z0-9_]*' 120)
        $enums = @(Select-MatchList $lines '^\s*enum\s+class\s+[A-Za-z_][A-Za-z0-9_]*' 120)
        $funcs = @(Select-MatchList $lines '^\s*(?:\[\[nodiscard\]\]\s*)?(?:inline\s+)?(?:static\s+)?(?:virtual\s+)?[A-Za-z_][A-Za-z0-9_:<>,\s*&]*\s+[A-Za-z_][A-Za-z0-9_]*\s*\([^;]*\)\s*(?:const)?\s*(?:noexcept)?\s*(?:override)?\s*(?:=\s*0)?\s*[{;]$' 300)
        $txMarkers = @($lines | Where-Object { $_ -match 'begin_transaction|commit_transaction|rollback_transaction|validate_shadow_integrity' }).Count -gt 0
        $hasLoop = @($lines | Where-Object { $_ -match '^\s*for\s*\(|^\s*while\s*\(' }).Count -gt 0
        $hasSwitch = @($lines | Where-Object { $_ -match '^\s*switch\s*\(' }).Count -gt 0
        $hasThrow = @($lines | Where-Object { $_ -match 'throw\s+|runtime_error|bad_alloc' }).Count -gt 0
        $hasGuard = @($lines | Where-Object { $_ -match 'is_null\(|size\(\)\s*==\s*0|INVALID_SLOT|\[\[unlikely\]\]' }).Count -gt 0
        $intent = Get-Intent $h.FullName
        $coreLogic = Get-CoreLogic $h.Name $lines $txMarkers
        $specificWhys = Get-FileSpecificWhys $h.FullName $funcs $lines
        $invariants = Get-Invariants $lines

        $doc = @()
        $doc += '# ' + $h.Name
        $doc += ''
        $doc += 'Source Header: include/dgtl/' + $top + '/' + $relNorm
        $doc += 'Last Synced: ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss')
        $doc += ''
        $doc += '## Vision'
        $doc += 'This component exists to make the compiler/runtime pipeline explicit, safe, and predictable under high-performance constraints.'
        $doc += ''
        $doc += '## Why This File Exists'
        $doc += '- ' + $intent
        $doc += '- Keep responsibilities localized so surrounding passes and backends can compose without hidden coupling.'
        $doc += '- Preserve correctness with explicit guardrails before optimization gains.'
        $doc += ($specificWhys | ForEach-Object { $_ })
        $doc += ''
        $doc += '## Core Logic'
        $doc += ($coreLogic | ForEach-Object { $_ })
        $doc += ''
        $doc += '## Control Flow Graph and Map'
        $doc += (New-ControlFlowGraphAdvanced $txMarkers $hasLoop $hasSwitch $hasThrow $hasGuard)
        $doc += ''
        $doc += '## Control Flow Narrative'
        $doc += '- Entry points validate stream and state before any mutation or expensive work.'
        if($hasLoop){ $doc += '- Processing is mainly loop-driven to walk nodes, regions, or dependency structures.' }
        if($hasSwitch){ $doc += '- Dispatch branches by opcode or strategy to keep behavior explicit and testable.' }
        if($txMarkers){ $doc += '- Mutation paths are transactional to ensure safe rollback on validation failure.' }
        if($hasThrow){ $doc += '- Failure branches escalate with explicit errors or traps when invariants are violated.' }
        $doc += ''
        $doc += '## Include Dependencies'
        if($includes.Count -gt 0){ $doc += ($includes | ForEach-Object { '- ' + $_ }) } else { $doc += '- none' }
        $doc += ''
        $doc += '## Class and Struct Catalog'
        if($classes.Count -gt 0){ $doc += '### Classes'; $doc += ($classes | ForEach-Object { '- ' + $_ }) }
        if($structs.Count -gt 0){ $doc += '### Structs'; $doc += ($structs | ForEach-Object { '- ' + $_ }) }
        if($classes.Count -eq 0 -and $structs.Count -eq 0){ $doc += '- none detected' }
        $doc += ''
        $doc += '## Enum Catalog'
        if($enums.Count -gt 0){ $doc += ($enums | ForEach-Object { '- ' + $_ }) } else { $doc += '- none detected' }
        $doc += ''
        $doc += '## Function Catalog'
        if($funcs.Count -gt 0){ $doc += ($funcs | ForEach-Object { '- ' + $_ }) } else { $doc += '- none detected' }
        $doc += ''
        $doc += '## Design Decisions and Whys'
        $doc += '- Explicit typing across IR nodes and flags reduces hidden semantic drift.'
        $doc += '- Guard checks prevent invalid states from propagating into emitters and runtime paths.'
        $doc += '- Layered modules allow policy evolution in pass and pipeline without backend rewrites.'
        $doc += ''
        $doc += '## Critical Invariants'
        $doc += ($invariants | ForEach-Object { $_ })
        $doc += ''
        $doc += '## Practical Reading Order'
        $doc += '- Start with type and enum declarations.'
        $doc += '- Follow entry functions for control-flow intent.'
        $doc += '- Then inspect helper methods for safety and transformation details.'

        Set-Content -Path $outPath -Value $doc -Encoding utf8
        $generated++
    }
}

$cmdOut = Join-Path $myDocs 'commands'
if(-not (Test-Path $cmdOut)){ New-Item -ItemType Directory -Path $cmdOut -Force | Out-Null }

$scripts = @(
    @{ Name='fast_regression.ps1'; Path=Join-Path $root 'md_docs\commands\fast_regression.ps1'; Why='One-step compile plus backend conformance gate for rapid regression checks.' },
    @{ Name='backend_conformance_check.ps1'; Path=Join-Path $root 'md_docs\commands\backend_conformance_check.ps1'; Why='Validates emitter outputs against deterministic backend expectations.' }
)

foreach($s in $scripts){
    $l = @()
    if(Test-Path $s.Path){
        $l = Get-Content $s.Path
    }
    $steps = @($l | Where-Object { $_ -match '^\s*Write-Host|^\s*if\s*\(|^\s*&\s+powershell|^\s*for(each)?\s*\(|^\s*\$' } | Select-Object -First 120)
    $d = @()
    $d += '# ' + $s.Name
    $d += ''
    $relativeSource = ($s.Path.Substring($root.Length + 1) -replace '\\','/')
    $d += 'Source Script: ' + $relativeSource
    $d += 'Last Synced: ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss')
    $d += ''
    $d += '## Vision'
    $d += 'Provide deterministic prelaunch automation with clear pass/fail behavior.'
    $d += ''
    $d += '## Why This Script Exists'
    $d += '- ' + $s.Why
    $d += '- Keep operational behavior deterministic across local and CI runs.'
    $d += '- Make failure causes visible through explicit step boundaries.'
    $d += ''
    $d += '## Core Logic'
    $d += '- Resolve execution context and validate required paths.'
    $d += '- Execute ordered verification stages without hidden side effects.'
    $d += '- Aggregate outcome and return explicit process status.'
    $d += ''
    $d += '## Control Flow Graph and Map'
    $d += '```mermaid'
    $d += 'flowchart TD'
    $d += 'A[Start] --> B[Validate Inputs and Paths]'
    $d += 'B --> C[Execute Build or Check Stages]'
    $d += 'C --> D[Collect and Report Results]'
    $d += 'D --> E[Exit with Success or Failure Code]'
    $d += '```'
    $d += ''
    $d += '## Control Flow Narrative'
    $d += '- Script starts with strict preconditions and path checks.'
    $d += '- Main stages execute in deterministic order to avoid flaky behavior.'
    $d += '- Failures exit immediately with non-zero status to stop unsafe release flow.'
    $d += ''
    $d += '## Key Logic Snippets'
    if($steps.Count -gt 0){
        $d += ($steps | ForEach-Object { '- ' + $_.Trim() })
    } else {
        $d += '- Source script snippets unavailable; validate script path if this is unexpected.'
    }
    $d += ''
    $d += '## Class and Function Catalog'
    $d += '- N/A for script document.'
    $d += ''
    $d += '## Design Decisions and Whys'
    $d += '- Uses explicit exit codes to support CI and local prelaunch checks.'
    $d += '- Keeps build and conformance concerns composable and auditable.'
    $d += ''
    $d += '## Critical Invariants'
    $d += '- A non-zero stage failure must propagate to non-zero script exit.'
    $d += '- Verification order must remain stable to keep outcomes reproducible.'
    Set-Content -Path (Join-Path $cmdOut ($s.Name + '.md')) -Value $d -Encoding utf8
}

$idx = @()
$idx += '# commands_index.md'
$idx += ''
$idx += 'Source Folder: My_Docs/commands'
$idx += 'Last Synced: ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss')
$idx += ''
$idx += '## Vision'
$idx += 'Single source of truth for operational commands in prelaunch stage.'
$idx += ''
$idx += '## Why This File Exists'
$idx += '- Centralize command discovery and run order for release checks.'
$idx += '- Keep script-level knowledge explicit and reviewable.'
$idx += ''
$idx += '## Core Logic'
$idx += '- Index operational scripts and expected usage sequence.'
$idx += '- Provide one navigation point for deterministic execution workflows.'
$idx += ''
$idx += '## Control Flow Graph and Map'
$idx += '```mermaid'
$idx += 'flowchart TD'
$idx += 'A[Read Index] --> B[Select Script Doc]'
$idx += 'B --> C[Run Script]'
$idx += 'C --> D[Observe Exit and Output]'
$idx += 'D --> E[Proceed or Fix and Re-run]'
$idx += '```'
$idx += ''
$idx += '## Control Flow Narrative'
$idx += '- Reader locates the exact script for the intended validation step.'
$idx += '- Script documentation then guides deterministic execution and result interpretation.'
$idx += ''
$idx += '## Class and Function Catalog'
$idx += '- N/A for index document.'
$idx += ''
$idx += '## Scripts'
$idx += '- fast_regression.ps1.md'
$idx += '- backend_conformance_check.ps1.md'
$idx += ''
$idx += '## Critical Invariants'
$idx += '- Script list must stay aligned with actual command files.'
$idx += '- Documentation order should reflect recommended execution order.'
$idx += ''
$idx += '## Why'
$idx += '- Keep command execution deterministic and easy to audit.'
Set-Content -Path (Join-Path $cmdOut 'commands_index.md') -Value $idx -Encoding utf8

Write-Host ('Enhanced markdown regenerated: ' + $generated)
#pragma warning restore PSUseApprovedVerbs
