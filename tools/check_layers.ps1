<#
.SYNOPSIS
    DGTL layering checker. Enforces the import contract between backend layers.

.DESCRIPTION
    Scans every *.ixx and *.cpp under include\dgtl and src, determines each file's
    layer from its path, parses `import dgtl.xxx;` / `export import dgtl.xxx;` lines,
    resolves each imported module to the file that declares `export module <name>`,
    and checks the edge against the allowed-imports table below.

    Layer rules (FROM may import TO):
        RUNTIME : RUNTIME, CORE (Foundational platform support, like llvm::Support)
        HIR     : HIR, CORE, RUNTIME (High-Level Semantic IR)
        IR      : IR, HIR, CORE, RUNTIME
        MIR     : MIR, IR, CORE, RUNTIME
        ISA     : ISA, MIR, OBJECT, CORE, RUNTIME
        OBJECT  : OBJECT, CORE, RUNTIME
        DRIVER  : DRIVER, HIR, IR, MIR, ISA, OBJECT, CORE, RUNTIME, EXECUTION
        JIT     : JIT, OBJECT, CORE, DRIVER, RUNTIME, EXECUTION
        LINKER  : LINKER, OBJECT, CORE, RUNTIME, EXECUTION
        SDK, UNCHECKED : anything

    Imports of UNCHECKED-layer modules from strict layers are warnings, not failures.
    Imports that cannot be resolved to a declaring file are warnings.

    Waivers live in tools\layer_waivers.txt, one per line:
        <relative file path> -> <module name>
        <relative file path> -> *
    Paths are relative to the DGTL root (e.g. include\dgtl\jit\JIT_Executor.ixx).
    The path may contain PowerShell -like wildcards (e.g. include\dgtl\isa\arm64\*).
    '#' starts a comment. The waiver list may only shrink.

.PARAMETER Root
    DGTL root directory. Defaults to the parent of this script's directory.

.PARAMETER WaiverFile
    Waiver file path. Defaults to <script dir>\layer_waivers.txt.

.PARAMETER EmitWaivers
    Print every current violation in waiver-file syntax (for bootstrapping).

.PARAMETER ShowWaived
    Also list waived violations individually.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File D:\TataG\DGTL\tools\check_layers.ps1
#>
[CmdletBinding()]
param(
    [string]$Root,
    [string]$WaiverFile,
    [switch]$EmitWaivers,
    [switch]$ShowWaived
)

Set-StrictMode -Version 2
$ErrorActionPreference = 'Stop'

if (-not $Root)       { $Root = Split-Path -Parent $PSScriptRoot }
if (-not $WaiverFile) { $WaiverFile = Join-Path $PSScriptRoot 'layer_waivers.txt' }
$Root = (Resolve-Path $Root).Path.TrimEnd('\')

# ---------------------------------------------------------------------------
# Layer contract
# ---------------------------------------------------------------------------
$Allowed = @{
    'IR'        = @('IR', 'HIR', 'CORE', 'RUNTIME')
    'HIR'       = @('HIR', 'IR', 'CORE', 'RUNTIME')
    'MIR'       = @('MIR', 'IR', 'CORE', 'RUNTIME')
    'ISA'       = @('ISA', 'MIR', 'OBJECT', 'CORE', 'RUNTIME')
    'OBJECT'    = @('OBJECT', 'CORE', 'RUNTIME')
    'DRIVER'    = @('DRIVER', 'HIR', 'IR', 'MIR', 'ISA', 'OBJECT', 'CORE', 'RUNTIME', 'EXECUTION')
    'JIT'       = @('JIT', 'OBJECT', 'CORE', 'DRIVER', 'RUNTIME', 'EXECUTION')
    'LINKER'    = @('LINKER', 'OBJECT', 'CORE', 'RUNTIME', 'EXECUTION')
    'RUNTIME'   = @('RUNTIME', 'IR', 'CORE')
    'EXECUTION' = @('EXECUTION', 'MIR', 'OBJECT', 'CORE', 'RUNTIME')
}
# SDK and UNCHECKED are not in $Allowed => unrestricted.

$DirLayer = @{
    'ir'        = 'IR'
    'hir'       = 'HIR'
    'mir'       = 'MIR'
    'isa'       = 'ISA'
    'object'    = 'OBJECT'
    'codegen'   = 'DRIVER'
    'jit'       = 'JIT'
    'linker'    = 'LINKER'
    'core'      = 'CORE'
    'utils'     = 'CORE'
    'runtime'   = 'RUNTIME'
    'execution' = 'EXECUTION'
    'sdk'       = 'SDK'
    'api'       = 'SDK'
}

function Get-RelPath([string]$full) {
    if ($full.StartsWith($Root, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $full.Substring($Root.Length).TrimStart('\')
    }
    return $full
}

function Get-Layer([string]$rel) {
    $parts = $rel -split '[\\/]'
    $dir = $null
    if ($parts.Length -ge 4 -and $parts[0] -ieq 'include' -and $parts[1] -ieq 'dgtl') {
        $dir = $parts[2]
    } elseif ($parts.Length -ge 3 -and $parts[0] -ieq 'src') {
        $dir = $parts[1]
    }
    if ($dir) {
        $key = $dir.ToLowerInvariant()
        if ($DirLayer.ContainsKey($key)) { return $DirLayer[$key] }
    }
    return 'UNCHECKED'
}

function Normalize-Path([string]$p) {
    return ($p.Trim() -replace '/', '\').TrimStart('.', '\').ToLowerInvariant()
}

# ---------------------------------------------------------------------------
# Collect files
# ---------------------------------------------------------------------------
$scanDirs = @((Join-Path $Root 'include\dgtl'), (Join-Path $Root 'src')) | Where-Object { Test-Path $_ }
$files = @(Get-ChildItem -Path $scanDirs -Recurse -File -Include '*.ixx', '*.cpp' | Sort-Object FullName)

$reExportModule = '^\s*export\s+module\s+([A-Za-z_][\w\.]*)\s*(:\s*[\w\.]+)?\s*;'
$reImport       = '^\s*(?:export\s+)?import\s+(dgtl[\w\.]*)\s*(:\s*[\w\.]+)?\s*;'

# module name -> rel path (primary interface preferred over partitions)
$moduleFile    = @{}
$moduleIsPrim  = @{}
# rel path -> list of imported primary module names
$fileImports   = @{}
$fileLayer     = @{}

foreach ($f in $files) {
    $rel = Get-RelPath $f.FullName
    $fileLayer[$rel] = Get-Layer $rel
    $imports = New-Object System.Collections.Generic.List[string]
    $inBlockComment = $false
    foreach ($line in [System.IO.File]::ReadAllLines($f.FullName)) {
        # Skip /* ... */ block comments (coarse, line-based).
        if ($inBlockComment) {
            if ($line -match '\*/') { $inBlockComment = $false }
            continue
        }
        if ($line -match '^\s*/\*' -and $line -notmatch '\*/') { $inBlockComment = $true; continue }

        if ($line -match $reExportModule) {
            $name = $Matches[1]
            $isPrim = -not $Matches[2]
            if (-not $moduleFile.ContainsKey($name) -or ($isPrim -and -not $moduleIsPrim[$name])) {
                $moduleFile[$name] = $rel
                $moduleIsPrim[$name] = $isPrim
            }
            continue
        }
        if ($line -match $reImport) {
            $mod = $Matches[1]
            if (-not $imports.Contains($mod)) { $imports.Add($mod) }
        }
    }
    $fileImports[$rel] = $imports
}

# ---------------------------------------------------------------------------
# Waivers
# ---------------------------------------------------------------------------
$waivers = New-Object System.Collections.Generic.List[object]
if (Test-Path $WaiverFile) {
    $ln = 0
    foreach ($raw in [System.IO.File]::ReadAllLines($WaiverFile)) {
        $ln++
        $line = $raw
        $hash = $line.IndexOf('#')
        if ($hash -ge 0) { $line = $line.Substring(0, $hash) }
        $line = $line.Trim()
        if (-not $line) { continue }
        $idx = $line.IndexOf('->')
        if ($idx -lt 0) {
            Write-Warning ("layer_waivers.txt:{0}: malformed waiver '{1}'" -f $ln, $raw)
            continue
        }
        $waivers.Add([pscustomobject]@{
            File   = Normalize-Path $line.Substring(0, $idx)
            Module = $line.Substring($idx + 2).Trim().ToLowerInvariant()
            Line   = $ln
            Text   = $line
            Used   = $false
        })
    }
} else {
    Write-Warning "Waiver file not found: $WaiverFile"
}

function Find-Waiver([string]$rel, [string]$mod) {
    $nf = Normalize-Path $rel
    $nm = $mod.ToLowerInvariant()
    foreach ($w in $waivers) {
        $fileMatch = if ($w.File.IndexOfAny([char[]]'*?') -ge 0) { $nf -like $w.File } else { $w.File -eq $nf }
        if ($fileMatch -and ($w.Module -eq '*' -or $w.Module -eq $nm)) { return $w }
    }
    return $null
}

# ---------------------------------------------------------------------------
# Check
# ---------------------------------------------------------------------------
$violations = New-Object System.Collections.Generic.List[object]
$waived     = New-Object System.Collections.Generic.List[object]
$warnings   = New-Object System.Collections.Generic.List[string]

foreach ($rel in ($fileImports.Keys | Sort-Object)) {
    $from = $fileLayer[$rel]
    if (-not $Allowed.ContainsKey($from)) { continue }   # SDK / UNCHECKED unrestricted; CORE handled below
    foreach ($mod in $fileImports[$rel]) {
        if (-not $moduleFile.ContainsKey($mod)) {
            $warnings.Add(("{0} -> {1} ({2} -> ?): unresolved module" -f $rel, $mod, $from))
            continue
        }
        $target = $moduleFile[$mod]
        if ($target -ieq $rel) { continue }
        $to = $fileLayer[$target]
        if ($Allowed[$from] -contains $to) { continue }
        if ($to -eq 'SDK' -or $to -eq 'UNCHECKED') {
            $warnings.Add(("{0} -> {1} ({2} -> {3})" -f $rel, $mod, $from, $to))
            continue
        }
        $v = [pscustomobject]@{ File = $rel; Module = $mod; From = $from; To = $to }
        $w = Find-Waiver $rel $mod
        if ($w) { $w.Used = $true; $waived.Add($v) } else { $violations.Add($v) }
    }
}

# CORE is foundational: it may import CORE and RUNTIME, plus canonical contract types
# (dgtl.ir_enums, dgtl.ir_node, dgtl.ir.stream, dgtl.api.config) for Compiler_Context and Target.
$CoreAllowed = @('CORE', 'RUNTIME')
$CoreAllowedModules = @('dgtl.ir_enums', 'dgtl.ir_node', 'dgtl.ir.stream', 'dgtl.api.config')

foreach ($rel in ($fileImports.Keys | Sort-Object)) {
    if ($fileLayer[$rel] -ne 'CORE') { continue }
    foreach ($mod in $fileImports[$rel]) {
        if (-not $moduleFile.ContainsKey($mod)) { continue }
        if ($CoreAllowedModules -contains $mod.ToLowerInvariant()) { continue }
        $to = $fileLayer[$moduleFile[$mod]]
        if ($CoreAllowed -notcontains $to) { $warnings.Add(("{0} -> {1} (CORE -> {2})" -f $rel, $mod, $to)) }
    }
}

# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------
if ($EmitWaivers) {
    foreach ($v in (@($violations) + @($waived))) { "{0} -> {1}" -f $v.File, $v.Module }
    exit 0
}

Write-Host ("DGTL layer check: {0} files, {1} modules, root {2}" -f $files.Count, $moduleFile.Count, $Root)

if ($warnings.Count -gt 0) {
    Write-Host ""
    Write-Host ("Warnings ({0}):" -f $warnings.Count) -ForegroundColor Yellow
    foreach ($w in $warnings) { Write-Host "  WARN  $w" -ForegroundColor Yellow }
}

if ($ShowWaived -and $waived.Count -gt 0) {
    Write-Host ""
    Write-Host ("Waived ({0}):" -f $waived.Count) -ForegroundColor DarkGray
    foreach ($v in $waived) {
        Write-Host ("  WAIVED  {0} -> {1} ({2} -> {3})" -f $v.File, $v.Module, $v.From, $v.To) -ForegroundColor DarkGray
    }
}

$stale = @($waivers | Where-Object { -not $_.Used })
if ($stale.Count -gt 0) {
    Write-Host ""
    Write-Host ("Stale waivers ({0}) - remove them, the list may only shrink:" -f $stale.Count) -ForegroundColor Cyan
    foreach ($s in $stale) { Write-Host ("  layer_waivers.txt:{0}: {1}" -f $s.Line, $s.Text) -ForegroundColor Cyan }
}

if ($violations.Count -gt 0) {
    Write-Host ""
    Write-Host ("Violations ({0}):" -f $violations.Count) -ForegroundColor Red
    foreach ($v in $violations) {
        Write-Host ("  {0} -> {1} ({2} -> {3})" -f $v.File, $v.Module, $v.From, $v.To) -ForegroundColor Red
    }
    Write-Host ""
    Write-Host ("FAILED: {0} violation(s), {1} waived, {2} warning(s)." -f $violations.Count, $waived.Count, $warnings.Count) -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host ("OK: 0 violations, {0} waived, {1} warning(s)." -f $waived.Count, $warnings.Count) -ForegroundColor Green
exit 0
