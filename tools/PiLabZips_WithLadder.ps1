param(
    [switch]$IncludeWebDist,
    [switch]$Help
)

$ErrorActionPreference = "Stop"

function Show-Help {
    Write-Host ""
    Write-Host "PiLabZips.ps1"
    Write-Host "============="
    Write-Host "Creates Git/date-named PiLab firmware, web, and ladder editor snapshot zip files."
    Write-Host ""
    Write-Host "Usage:"
    Write-Host "  .\PiLabZips.ps1 [options]"
    Write-Host ""
    Write-Host "Examples:"
    Write-Host "  .\PiLabZips.ps1"
    Write-Host "  .\PiLabZips.ps1 -IncludeWebDist"
    Write-Host ""
    Write-Host "Output:"
    Write-Host "  Private\snapshots\main_YYYY-MM-DD_<branch>_<hash>.zip"
    Write-Host "  Private\snapshots\web_YYYY-MM-DD_<branch>_<hash>.zip"
    Write-Host "  Private\snapshots\Ladder_YYYY-MM-DD_<branch>_<hash>.zip"
    Write-Host ""
    Write-Host "Parameters:"
    Write-Host "  -IncludeWebDist   Include Web\dist and LadderEditor\dist in snapshots. Default excludes generated Vite output."
    Write-Host "  -Help             Show this help."
    Write-Host ""
}

if ($Help) { Show-Help; exit 0 }

function Write-Step($Message) {
    Write-Host "==> $Message"
}

function Get-ProjectRoot {
    # Preferred: ask Git where the repo root is, starting from this script folder.
    $gitCmd = Get-Command git -ErrorAction SilentlyContinue
    if ($gitCmd) {
        try {
            $root = & $gitCmd.Source -C $PSScriptRoot rev-parse --show-toplevel 2>$null
            if ($LASTEXITCODE -eq 0 -and $root) {
                return (Resolve-Path $root.Trim()).Path
            }
        } catch { }
    }

    # Fallback: if this script lives in tools, repo root is the parent folder.
    $scriptDir = (Resolve-Path $PSScriptRoot).Path
    if ((Split-Path $scriptDir -Leaf).ToLowerInvariant() -eq "tools") {
        return (Resolve-Path (Join-Path $scriptDir "..")).Path
    }

    # Last fallback: current directory.
    return (Resolve-Path (Get-Location)).Path
}

function Get-SafeName($Value) {
    if ([string]::IsNullOrWhiteSpace($Value)) { return "unknown" }
    return ($Value.Trim() -replace '[\\/:*?"<>|\s]+', '-')
}

function Get-GitInfo($ProjectRoot) {
    $info = [ordered]@{
        GitFound = $false
        GitPath = ""
        Branch = "nogit"
        ShortHash = "nogit"
        FullHash = "nogit"
        LatestCommit = "nogit"
        StatusShort = ""
        RepoRoot = "nogit"
        Warning = ""
    }

    $gitCmd = Get-Command git -ErrorAction SilentlyContinue
    if (-not $gitCmd) {
        $info.Warning = "Git was not found in PATH."
        return $info
    }

    $info.GitFound = $true
    $info.GitPath = $gitCmd.Source

    try {
        $repoRoot = & $gitCmd.Source -C $ProjectRoot rev-parse --show-toplevel 2>$null
        if ($LASTEXITCODE -ne 0 -or -not $repoRoot) {
            $info.Warning = "Git did not recognize the project root as a repository."
            return $info
        }

        $branch = & $gitCmd.Source -C $ProjectRoot rev-parse --abbrev-ref HEAD 2>$null
        if ($LASTEXITCODE -ne 0 -or -not $branch) { throw "Failed to read Git branch." }

        $shortHash = & $gitCmd.Source -C $ProjectRoot rev-parse --short HEAD 2>$null
        if ($LASTEXITCODE -ne 0 -or -not $shortHash) { throw "Failed to read Git short hash." }

        $fullHash = & $gitCmd.Source -C $ProjectRoot rev-parse HEAD 2>$null
        $latestCommit = & $gitCmd.Source -C $ProjectRoot log -1 --oneline 2>$null
        $statusShort = & $gitCmd.Source -C $ProjectRoot status --short 2>$null

        $info.RepoRoot = $repoRoot.Trim()
        $info.Branch = Get-SafeName $branch
        $info.ShortHash = Get-SafeName $shortHash
        $info.FullHash = $fullHash.Trim()
        $info.LatestCommit = $latestCommit.Trim()
        $info.StatusShort = ($statusShort -join [Environment]::NewLine)
    } catch {
        $info.Warning = $_.Exception.Message
    }

    return $info
}

function Copy-IfExists($Source, $Destination) {
    if (Test-Path $Source) {
        Copy-Item $Source $Destination -Recurse -Force
    }
}

function Remove-IfExists($Path) {
    Remove-Item $Path -Recurse -Force -ErrorAction SilentlyContinue
}

$ProjectRoot = Get-ProjectRoot
$PrivateDir = Join-Path $ProjectRoot "Private"
$SnapshotDir = Join-Path $PrivateDir "snapshots"

New-Item -ItemType Directory -Path $SnapshotDir -Force | Out-Null

$GitInfo = Get-GitInfo $ProjectRoot
$DateStamp = Get-Date -Format "yyyy-MM-dd"
$Suffix = "${DateStamp}_$($GitInfo.Branch)_$($GitInfo.ShortHash)"

$MainZip = Join-Path $SnapshotDir "main_$Suffix.zip"
$WebZip  = Join-Path $SnapshotDir "web_$Suffix.zip"
$LadderZip = Join-Path $SnapshotDir "Ladder_$Suffix.zip"

$StageRoot = Join-Path $env:TEMP ("pilab_snapshot_" + [guid]::NewGuid().ToString("N"))
$MainStage = Join-Path $StageRoot "main"
$WebStage  = Join-Path $StageRoot "web"
$LadderStage = Join-Path $StageRoot "ladder"

New-Item -ItemType Directory -Path $MainStage -Force | Out-Null
New-Item -ItemType Directory -Path $WebStage -Force | Out-Null
New-Item -ItemType Directory -Path $LadderStage -Force | Out-Null

Write-Host ""
Write-Host "PiLab Snapshot Zips"
Write-Host "==================="
Write-Host "Script dir:   $PSScriptRoot"
Write-Host "Current dir:  $(Get-Location)"
Write-Host "Project root: $ProjectRoot"
Write-Host "Output dir:   $SnapshotDir"
Write-Host "Git path:     $($GitInfo.GitPath)"
Write-Host "Git branch:   $($GitInfo.Branch)"
Write-Host "Git hash:     $($GitInfo.ShortHash)"
Write-Host "Suffix:       $Suffix"
if ($GitInfo.Warning) {
    Write-Host "WARNING: Git metadata fallback used: $($GitInfo.Warning)" -ForegroundColor Yellow
}
Write-Host ""


function Stage-ViteProject($SourceFolder, $StageFolder, $DisplayName, $IncludeDist) {
    if (Test-Path $SourceFolder) {
        # Vite project files are copied to the zip root, not inside a nested source folder.
        Copy-Item (Join-Path $SourceFolder "*") $StageFolder -Recurse -Force
        Remove-IfExists (Join-Path $StageFolder "node_modules")
        if (-not $IncludeDist) {
            Remove-IfExists (Join-Path $StageFolder "dist")
        }
    } else {
        Write-Host "WARNING: $DisplayName folder not found: $SourceFolder" -ForegroundColor Yellow
    }
}

try {
    Write-Step "Staging firmware files"

    $FirmwareFolders = @(
        "main",
        "components",
        "managed_components",
        "Documentation"
    )

    foreach ($folder in $FirmwareFolders) {
        Copy-IfExists (Join-Path $ProjectRoot $folder) $MainStage
    }

    $FirmwareFiles = @(
        ".gitignore",
        "CMakeLists.txt",
        "dependencies.lock",
        "LICENSE",
        "partitions.csv",
        "README.md",
        "sdkconfig",
        "sdkconfig.defaults"
    )

    foreach ($file in $FirmwareFiles) {
        Copy-IfExists (Join-Path $ProjectRoot $file) $MainStage
    }

    # Remove noisy/generated files from staged firmware snapshot.
    Remove-IfExists (Join-Path $MainStage "managed_components\joltwallet__littlefs\test\testfs.bin")
    Remove-IfExists (Join-Path $MainStage "managed_components\joltwallet__littlefs\src\littlefs\.git")

    Write-Step "Staging web app files"

    $WebSource = Join-Path $ProjectRoot "Web"
    Stage-ViteProject $WebSource $WebStage "Web" $IncludeWebDist

    Write-Step "Staging ladder editor files"

    $LadderSource = Join-Path $ProjectRoot "LadderEditor"
    Stage-ViteProject $LadderSource $LadderStage "LadderEditor" $IncludeWebDist

    $SnapshotInfo = @"
PiLab Snapshot Info
===================
Created:       $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
Project Root:  $ProjectRoot
Script Dir:    $PSScriptRoot
Git Found:     $($GitInfo.GitFound)
Git Path:      $($GitInfo.GitPath)
Git Repo Root: $($GitInfo.RepoRoot)
Git Branch:    $($GitInfo.Branch)
Git ShortHash: $($GitInfo.ShortHash)
Git FullHash:  $($GitInfo.FullHash)
Latest Commit: $($GitInfo.LatestCommit)
Git Warning:   $($GitInfo.Warning)

Git Status --short:
$($GitInfo.StatusShort)
"@

    $SnapshotInfo | Set-Content -Path (Join-Path $MainStage "snapshot_info.txt") -Encoding UTF8
    $SnapshotInfo | Set-Content -Path (Join-Path $WebStage "snapshot_info.txt") -Encoding UTF8
    $SnapshotInfo | Set-Content -Path (Join-Path $LadderStage "snapshot_info.txt") -Encoding UTF8

    Remove-IfExists $MainZip
    Remove-IfExists $WebZip
    Remove-IfExists $LadderZip

    Write-Step "Creating $(Split-Path $MainZip -Leaf)"
    Compress-Archive -Path (Join-Path $MainStage "*") -DestinationPath $MainZip -Force

    Write-Step "Creating $(Split-Path $WebZip -Leaf)"
    Compress-Archive -Path (Join-Path $WebStage "*") -DestinationPath $WebZip -Force

    Write-Step "Creating $(Split-Path $LadderZip -Leaf)"
    Compress-Archive -Path (Join-Path $LadderStage "*") -DestinationPath $LadderZip -Force

    $MainSize = [math]::Round((Get-Item $MainZip).Length / 1MB, 3)
    $WebSize  = [math]::Round((Get-Item $WebZip).Length / 1MB, 3)
    $LadderSize = [math]::Round((Get-Item $LadderZip).Length / 1MB, 3)

    Write-Host ""
    Write-Host "Created:"
    Write-Host "  $MainZip ($MainSize MB)"
    Write-Host "  $WebZip  ($WebSize MB)"
    Write-Host "  $LadderZip ($LadderSize MB)"

    if (-not $IncludeWebDist) {
        Write-Host ""
        Write-Host "Tip: use -IncludeWebDist only if you want the built Vite output included too."
    }
} finally {
    Remove-IfExists $StageRoot
}
