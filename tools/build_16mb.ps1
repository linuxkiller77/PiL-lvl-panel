# Build PiLab Panel for Waveshare ESP32-S3-Touch-LCD-7 16MB Flash / 8MB PSRAM boards.
$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) "..")
Push-Location $Root
try {
    idf.py -B build_16mb -DPILAB_FLASH_SIZE=16MB build
} finally {
    Pop-Location
}
