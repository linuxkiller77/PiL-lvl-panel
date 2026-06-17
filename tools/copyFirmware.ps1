# copyFirmware.ps1
# Copies ESP-IDF build firmware files into webflasher/firmware/<variant>
# and regenerates the matching ESP Web Tools manifest.
#
# Examples:
#   .\tools\copyFirmware.ps1 -Variant 8mb  -BuildDir build_8mb
#   .\tools\copyFirmware.ps1 -Variant 16mb -BuildDir build_16mb
#
# If no variant is provided, 16mb/build is used for backwards compatibility.

param(
    [ValidateSet("8mb", "16mb")]
    [string]$Variant = "16mb",

    [string]$BuildDir = ""
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Resolve-Path (Join-Path $ScriptDir "..")

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    if ($Variant -eq "8mb") { $BuildDir = "build_8mb" } else { $BuildDir = "build_16mb" }
}

$BuildDirPath = Join-Path $ProjectRoot $BuildDir
$WebFlasherDir = Join-Path $ProjectRoot "webflasher"
$WebFlasherFirmwareDir = Join-Path $WebFlasherDir "firmware\$Variant"
$ManifestPath = Join-Path $WebFlasherDir "manifest_$Variant.json"
$DefaultManifestPath = Join-Path $WebFlasherDir "manifest.json"
$FlasherArgsPath = Join-Path $BuildDirPath "flasher_args.json"
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
    if ($Offset -is [int] -or $Offset -is [long] -or $Offset -is [int64]) { return [int64]$Offset }
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
    if ($normalized -like "*bootloader*bootloader.bin") { return "bootloader.bin" }
    if ($normalized -like "*partition_table*partition-table.bin") { return "partition-table.bin" }
    if ($name -eq "ota_data_initial.bin") { return "ota_data_initial.bin" }
    if ($name -eq "PiLab_Panel.bin") { return "PiLab_Panel.bin" }
    return $name
}

function Add-PartFromBuildFile {
    param(
        [AllowEmptyCollection()][Parameter(Mandatory=$true)][System.Collections.ArrayList]$Parts,
        [Parameter(Mandatory=$true)][string]$SourceRelative,
        [Parameter(Mandatory=$true)]$Offset
    )
    $normalizedSource = $SourceRelative -replace '/', '\'
    $src = Join-Path $BuildDirPath $normalizedSource
    if (!(Test-Path $src)) { throw "Required firmware file not found from flasher args: $src" }
    $destName = Get-DestinationName $normalizedSource
    $dest = Join-Path $WebFlasherFirmwareDir $destName
    Copy-Item -Path $src -Destination $dest -Force
    $size = (Get-Item $dest).Length
    $offsetInt = Convert-OffsetToInt $Offset
    Write-Host ("Copied 0x{0:X} {1} -> firmware\{2}\{3} ({4:N0} bytes)" -f $offsetInt, $normalizedSource, $Variant, $destName, $size)
    [void]$Parts.Add([ordered]@{ path = "firmware/$Variant/$destName"; offset = $offsetInt })
}

function Add-PartsFromFlasherArgs {
    param([AllowEmptyCollection()][Parameter(Mandatory=$true)][System.Collections.ArrayList]$Parts,[Parameter(Mandatory=$true)]$ArgsJson)
    if ($ArgsJson.flash_files) {
        $flashFiles = $ArgsJson.flash_files.PSObject.Properties | Sort-Object { Convert-OffsetToInt $_.Name }
        foreach ($entry in $flashFiles) { Add-PartFromBuildFile -Parts $Parts -SourceRelative ([string]$entry.Value) -Offset $entry.Name }
        return
    }
    if ($ArgsJson.flash_files_encrypted) { throw "Encrypted flash_files found; this script expects normal unencrypted web flashing." }
    throw "Could not find flash_files in $FlasherArgsPath."
}

Write-Host "PiLab Panel WebFlasher firmware copy"
Write-Host "Project root: $ProjectRoot"
Write-Host "Variant:      $Variant"
Write-Host "Build dir:    $BuildDirPath"
Write-Host "Output dir:   $WebFlasherFirmwareDir"
Write-Host "Manifest:     $ManifestPath"
Write-Host ""

if (!(Test-Path $BuildDirPath)) { throw "Build folder not found: $BuildDirPath. Run the matching idf.py build first." }
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
    foreach ($f in $fallback) { Add-PartFromBuildFile -Parts $parts -SourceRelative $f.Source -Offset $f.Offset }
}

if ($parts.Count -le 0) { throw "No firmware parts were copied." }

$version = "$(Get-PanelVersion)-$Variant"
$flashLabel = if ($Variant -eq "8mb") { "8MB Flash" } else { "16MB Flash" }
$manifest = [ordered]@{
    name = "PiLab Panel Firmware - Waveshare ESP32-S3 7 inch LCD - $flashLabel"
    version = $version
    new_install_prompt_erase = $true
    builds = @([ordered]@{ chipFamily = "ESP32-S3"; parts = @($parts) })
}

$manifest | ConvertTo-Json -Depth 8 | Set-Content -Path $ManifestPath -Encoding UTF8
if ($Variant -eq "16mb") {
    $manifest | ConvertTo-Json -Depth 8 | Set-Content -Path $DefaultManifestPath -Encoding UTF8
}

Write-Host ""
Write-Host "Updated manifest version: $version"
Write-Host "Manifest parts: $($parts.Count)"
Write-Host "Done. Web flasher firmware files are updated for $Variant."
