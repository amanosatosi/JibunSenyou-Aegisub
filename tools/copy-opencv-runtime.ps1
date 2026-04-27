#!/usr/bin/env powershell

param (
    [Parameter(Mandatory = $true)]
    [string]$DestinationDir
)

$ErrorActionPreference = "Stop"

function Get-OpenCVBinDir {
    if ($env:OPENCV_BIN_DIR -and (Test-Path -LiteralPath $env:OPENCV_BIN_DIR)) {
        return (Resolve-Path -LiteralPath $env:OPENCV_BIN_DIR).Path
    }

    $vcpkgRoot = $env:VCPKG_INSTALLATION_ROOT
    if (-not $vcpkgRoot) {
        $vcpkgRoot = $env:VCPKG_ROOT
    }
    if (-not $vcpkgRoot) {
        $vcpkgRoot = "C:\vcpkg"
    }

    $candidate = Join-Path $vcpkgRoot "installed\x64-windows\bin"
    if (Test-Path -LiteralPath $candidate) {
        return (Resolve-Path -LiteralPath $candidate).Path
    }

    return $null
}

$OpenCVBinDir = Get-OpenCVBinDir
if (-not $OpenCVBinDir) {
    Write-Output "OpenCV runtime DLL source not found; skipping OpenCV runtime copy."
    exit 0
}

if (-not (Test-Path -LiteralPath $DestinationDir)) {
    New-Item -ItemType Directory -Path $DestinationDir -Force | Out-Null
}

$patterns = @(
    "opencv_*.dll",
    "ade.dll",
    "ittnotify.dll",
    "zlib*.dll",
    "libpng*.dll",
    "jpeg*.dll",
    "libjpeg*.dll",
    "tiff*.dll",
    "libtiff*.dll",
    "lzma*.dll",
    "liblzma*.dll",
    "zstd*.dll",
    "webp*.dll",
    "libwebp*.dll",
    "libsharpyuv*.dll",
    "openjp2*.dll",
    "quirc*.dll",
    "tbb*.dll"
)

$copied = @{}
foreach ($pattern in $patterns) {
    Get-ChildItem -LiteralPath $OpenCVBinDir -Filter $pattern -File -ErrorAction SilentlyContinue | ForEach-Object {
        if ($_.Name -match "cuda|cudnn|cublas|cufft|npp|dnn") {
            return
        }

        $dest = Join-Path $DestinationDir $_.Name
        Copy-Item -LiteralPath $_.FullName -Destination $dest -Force
        $copied[$_.Name] = $true
    }
}

if ($copied.Count -eq 0) {
    Write-Output "No OpenCV runtime DLLs matched in $OpenCVBinDir."
}
else {
    Write-Output "Copied OpenCV runtime DLLs:"
    $copied.Keys | Sort-Object | ForEach-Object { Write-Output "  $_" }
}
