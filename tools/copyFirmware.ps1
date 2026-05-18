# copy_firmware_to_webflasher.ps1
# Copies the latest ESP-IDF build firmware files into the webflasher firmware folder.

$ErrorActionPreference = "Stop"

# Script is expected to live in: project-root/tools/
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Resolve-Path (Join-Path $ScriptDir "..")

$BuildDir = Join-Path $ProjectRoot "build"
$WebFlasherFirmwareDir = Join-Path $ProjectRoot "webflasher\firmware"

$Files = @(
    @{
        Source = Join-Path $BuildDir "bootloader\bootloader.bin"
        Dest   = Join-Path $WebFlasherFirmwareDir "bootloader.bin"
    },
    @{
        Source = Join-Path $BuildDir "partition_table\partition-table.bin"
        Dest   = Join-Path $WebFlasherFirmwareDir "partition-table.bin"
    },
    @{
        Source = Join-Path $BuildDir "ota_data_initial.bin"
        Dest   = Join-Path $WebFlasherFirmwareDir "ota_data_initial.bin"
    },
    @{
        Source = Join-Path $BuildDir "P4_PLC_Base.bin"
        Dest   = Join-Path $WebFlasherFirmwareDir "P4_PLC_Base.bin"
    }
)

Write-Host "PiLab WebFlasher firmware copy"
Write-Host "Project root: $ProjectRoot"
Write-Host "Build dir:    $BuildDir"
Write-Host "Output dir:   $WebFlasherFirmwareDir"
Write-Host ""

if (!(Test-Path $BuildDir)) {
    throw "Build folder not found: $BuildDir. Run idf.py build first."
}

if (!(Test-Path $WebFlasherFirmwareDir)) {
    Write-Host "Creating firmware folder: $WebFlasherFirmwareDir"
    New-Item -ItemType Directory -Path $WebFlasherFirmwareDir -Force | Out-Null
}

foreach ($File in $Files) {
    if (!(Test-Path $File.Source)) {
        throw "Required firmware file not found: $($File.Source)"
    }

    Copy-Item -Path $File.Source -Destination $File.Dest -Force

    $Size = (Get-Item $File.Dest).Length
    Write-Host ("Copied {0} -> {1} ({2:N0} bytes)" -f $File.Source, $File.Dest, $Size)
}

Write-Host ""
Write-Host "Done. Web flasher firmware files are updated."