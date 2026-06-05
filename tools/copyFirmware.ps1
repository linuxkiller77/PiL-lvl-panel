# copyFirmware.ps1
# Copies the latest ESP-IDF build firmware files into webflasher/firmware
# and regenerates webflasher/manifest.json for ESP Web Tools.
#
# Expected location:
#   project-root/tools/copyFirmware.ps1
#
# Normal use from project root or tools folder:
#   .\tools\copyFirmware.ps1
#   or
#   cd tools; .\copyFirmware.ps1

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Resolve-Path (Join-Path $ScriptDir "..")
$BuildDir = Join-Path $ProjectRoot "build"
$WebFlasherDir = Join-Path $ProjectRoot "webflasher"
$WebFlasherFirmwareDir = Join-Path $WebFlasherDir "firmware"
$ManifestPath = Join-Path $WebFlasherDir "manifest.json"
$FlasherArgsPath = Join-Path $BuildDir "flasher_args.json"
$VersionHeaderPath = Join-Path $ProjectRoot "main\pilab_panel_version.h"

function Get-PanelVersion {
    if (Test-Path $VersionHeaderPath) {
        $m = Select-String -Path $VersionHeaderPath -Pattern '#define\s+PILAB_PANEL_VERSION\s+"([^"]+)"' | Select-Object -First 1
        if ($m -and $m.Matches.Count -gt 0) {
            return $m.Matches[0].Groups[1].Value
        }
    }
    return "v6.45.41-s3-lvgl-runtime-rollback-stable"
}

function Convert-OffsetToInt {
    param([Parameter(Mandatory=$true)] $Offset)

    if ($Offset -is [int] -or $Offset -is [long] -or $Offset -is [int64]) {
        return [int64]$Offset
    }

    $s = [string]$Offset
    if ($s.StartsWith("0x", [System.StringComparison]::OrdinalIgnoreCase)) {
        return [Convert]::ToInt64($s.Substring(2), 16)
    }
    return [Convert]::ToInt64($s, 10)
}

function Get-DestinationName {
    param([Parameter(Mandatory=$true)][string]$SourceRelative)

    $normalized = $SourceRelative -replace '/', '\'
    $name = Split-Path $normalized -Leaf

    # Normalize common ESP-IDF names so the webflasher folder remains clean/stable.
    if ($normalized -like "*bootloader*bootloader.bin") { return "bootloader.bin" }
    if ($normalized -like "*partition_table*partition-table.bin") { return "partition-table.bin" }
    if ($name -eq "ota_data_initial.bin") { return "ota_data_initial.bin" }
    if ($name -eq "PiLab_Panel.bin") { return "PiLab_Panel.bin" }

    return $name
}

function Add-PartFromBuildFile {
    param(
        [AllowEmptyCollection()]
        [Parameter(Mandatory=$true)]
        [System.Collections.ArrayList]$Parts,

        [Parameter(Mandatory=$true)]
        [string]$SourceRelative,

        [Parameter(Mandatory=$true)]
        $Offset
    )

    $normalizedSource = $SourceRelative -replace '/', '\'
    $src = Join-Path $BuildDir $normalizedSource
    if (!(Test-Path $src)) {
        throw "Required firmware file not found from flasher args: $src"
    }

    $destName = Get-DestinationName $normalizedSource
    $dest = Join-Path $WebFlasherFirmwareDir $destName
    Copy-Item -Path $src -Destination $dest -Force

    $size = (Get-Item $dest).Length
    $offsetInt = Convert-OffsetToInt $Offset
    Write-Host ("Copied 0x{0:X} {1} -> firmware\{2} ({3:N0} bytes)" -f $offsetInt, $normalizedSource, $destName, $size)

    [void]$Parts.Add([ordered]@{
        path = "firmware/$destName"
        offset = $offsetInt
    })
}

function Add-PartsFromFlasherArgs {
    param(
        [AllowEmptyCollection()]
        [Parameter(Mandatory=$true)]
        [System.Collections.ArrayList]$Parts,

        [Parameter(Mandatory=$true)]
        $ArgsJson
    )

    if ($ArgsJson.flash_files) {
        # ESP-IDF normally writes flash_files as an object:
        #   "flash_files": { "0x0": "bootloader/bootloader.bin", ... }
        $flashFiles = $ArgsJson.flash_files.PSObject.Properties | Sort-Object { Convert-OffsetToInt $_.Name }
        foreach ($entry in $flashFiles) {
            Add-PartFromBuildFile -Parts $Parts -SourceRelative ([string]$entry.Value) -Offset $entry.Name
        }
        return
    }

    if ($ArgsJson.flash_files_encrypted) {
        throw "flasher_args.json contains flash_files_encrypted, but plain flash_files was not found. This copy script is for normal unencrypted web flashing."
    }

    throw "Could not find flash_files in $FlasherArgsPath."
}

Write-Host "PiLab Panel WebFlasher firmware copy"
Write-Host "Project root: $ProjectRoot"
Write-Host "Build dir:    $BuildDir"
Write-Host "Output dir:   $WebFlasherFirmwareDir"
Write-Host "Manifest:     $ManifestPath"
Write-Host ""

if (!(Test-Path $BuildDir)) {
    throw "Build folder not found: $BuildDir. Run idf.py build first."
}

New-Item -ItemType Directory -Path $WebFlasherFirmwareDir -Force | Out-Null

$parts = New-Object System.Collections.ArrayList

if (Test-Path $FlasherArgsPath) {
    Write-Host "Using ESP-IDF flasher metadata: $FlasherArgsPath"
    $args = Get-Content $FlasherArgsPath -Raw | ConvertFrom-Json
    Add-PartsFromFlasherArgs -Parts $parts -ArgsJson $args
} else {
    Write-Host "flasher_args.json not found. Using standard PiLab Panel ESP32-S3 fallback offsets."

    $fallback = @(
        @{ Source = "bootloader\bootloader.bin"; Offset = 0 },
        @{ Source = "partition_table\partition-table.bin"; Offset = 32768 },
        @{ Source = "PiLab_Panel.bin"; Offset = 65536 }
    )

    foreach ($f in $fallback) {
        Add-PartFromBuildFile -Parts $parts -SourceRelative $f.Source -Offset $f.Offset
    }
}

if ($parts.Count -le 0) {
    throw "No firmware parts were copied. Check build/flasher_args.json or run idf.py build again."
}

$version = Get-PanelVersion
$manifest = [ordered]@{
    name = "PiLab Panel Firmware"
    version = $version
    new_install_prompt_erase = $true
    builds = @(
        [ordered]@{
            chipFamily = "ESP32-S3"
            parts = @($parts)
        }
    )
}

$manifest | ConvertTo-Json -Depth 8 | Set-Content -Path $ManifestPath -Encoding UTF8

Write-Host ""
Write-Host "Updated manifest version: $version"
Write-Host "Manifest parts: $($parts.Count)"
Write-Host "Done. Web flasher firmware files are updated."
