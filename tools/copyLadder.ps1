param(
    [switch]$Help
)

$ErrorActionPreference = "Stop"

function Show-Help {
    Write-Host ""
    Write-Host "copyLadder.ps1"
    Write-Host "==============="
    Write-Host "Builds the Ladder Editor single-file HTML output and copies it into the GitHub Pages experimental ladder folder."
    Write-Host ""
    Write-Host "Usage:"
    Write-Host "  .\copyLadder.ps1"
    Write-Host ""
    Write-Host "What it does:"
    Write-Host "  1. Auto-detects the PiLab repo root."
    Write-Host "  2. Runs npm run single in LadderEditor\."
    Write-Host "  3. Copies LadderEditor\dist\index.html to experimental\ladder\pilab_ladder_editor.html."
    Write-Host ""
    Write-Host "Parameters:"
    Write-Host "  -Help   Show this help."
    Write-Host ""
}

if ($Help) { Show-Help; exit 0 }

function Find-ProjectRoot {
    $start = $PSScriptRoot
    if ([string]::IsNullOrWhiteSpace($start)) { $start = (Get-Location).Path }

    $git = Get-Command git -ErrorAction SilentlyContinue
    if ($git) {
        $root = (& $git.Source -C $start rev-parse --show-toplevel 2>$null)
        if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($root)) {
            return (Get-Item -LiteralPath $root.Trim()).FullName
        }
    }

    $dir = Get-Item -LiteralPath $start
    while ($null -ne $dir) {
        if (
            (Test-Path (Join-Path $dir.FullName "LadderEditor")) -and
            (Test-Path (Join-Path $dir.FullName "experimental"))
        ) {
            return $dir.FullName
        }

        $dir = $dir.Parent
    }

    throw "Could not find project root."
}

$ProjectRoot = Find-ProjectRoot
$LadderDir = Join-Path $ProjectRoot "LadderEditor"
$ExperimentalLadderDir = Join-Path $ProjectRoot "experimental\ladder"

$SourceHtml = Join-Path $LadderDir "dist\index.html"
$DestinationHtml = Join-Path $ExperimentalLadderDir "pilab_ladder_editor.html"

if (!(Test-Path -LiteralPath $LadderDir)) {
    throw "Could not find LadderEditor folder at: $LadderDir"
}

Write-Host "==> Project root: $ProjectRoot"
Write-Host "==> Ladder Editor: $LadderDir"
Write-Host "==> Destination: $DestinationHtml"

Write-Host "==> Building Ladder Editor single-file HTML"
Push-Location $LadderDir
try {
    npm run single
}
finally {
    Pop-Location
}

if (!(Test-Path -LiteralPath $SourceHtml)) {
    throw "Expected single-file build output was not found: $SourceHtml"
}

New-Item -ItemType Directory -Path $ExperimentalLadderDir -Force | Out-Null

Write-Host "==> Copying Ladder Editor to GitHub Pages experimental ladder folder"
Copy-Item -LiteralPath $SourceHtml -Destination $DestinationHtml -Force

Write-Host ""
Write-Host "Ladder Editor copied successfully:"
Write-Host "  $DestinationHtml"