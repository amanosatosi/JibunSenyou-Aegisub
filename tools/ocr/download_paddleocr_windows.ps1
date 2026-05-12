#!/usr/bin/env powershell

param (
    [Parameter(Mandatory = $true)]
    [string]$DestinationDir
)

$ErrorActionPreference = "Stop"

$Version = "v1.4.1"
$ArchiveName = "PaddleOCR-json_v1.4.1_windows_x64.7z"
$ArchiveUrl = "https://github.com/hiroi-sora/PaddleOCR-json/releases/download/$Version/$ArchiveName"
$ArchiveSha256 = "c0912a70acb1f8f18fafe1f438a2935292a6ec7e2859156fa48a33e91358d71d"

function Invoke-WebRequestWithRetry {
    param(
        [Parameter(Mandatory = $true)][string]$Uri,
        [Parameter(Mandatory = $true)][string]$OutFile,
        [int]$MaxAttempts = 5,
        [int]$InitialDelaySeconds = 5
    )

    for ($attempt = 1; $attempt -le $MaxAttempts; ++$attempt) {
        try {
            Invoke-WebRequest $Uri -OutFile $OutFile -UseBasicParsing
            return
        }
        catch {
            if ($attempt -ge $MaxAttempts) {
                throw
            }
            $delay = [math]::Min(120, $InitialDelaySeconds * [math]::Pow(2, $attempt - 1))
            Write-Host "Download failed for $Uri (attempt $attempt of $MaxAttempts). Retrying in $delay seconds..."
            Start-Sleep -Seconds $delay
        }
    }
}

function Assert-FileHash {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$ExpectedSha256
    )

    $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
    if ($actual -ne $ExpectedSha256.ToLowerInvariant()) {
        throw "SHA256 mismatch for $Path. Expected $ExpectedSha256, got $actual."
    }
}

function Copy-DirectoryContents {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination
    )

    if (!(Test-Path -LiteralPath $Destination)) {
        New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    }

    Get-ChildItem -LiteralPath $Source -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $Destination -Recurse -Force
    }
}

$DestinationDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($DestinationDir)
$BinDir = Join-Path $DestinationDir "bin"
$ModelsDir = Join-Path $DestinationDir "models"
$LicensesDir = Join-Path $DestinationDir "licenses"

if ((Test-Path -LiteralPath (Join-Path $BinDir "PaddleOCR-json.exe")) -and
    (Test-Path -LiteralPath (Join-Path $ModelsDir "config_en.txt")) -and
    (Test-Path -LiteralPath (Join-Path $ModelsDir "config_japan.txt")) -and
    (Test-Path -LiteralPath (Join-Path $ModelsDir "config_chinese.txt"))) {
    Write-Host "PaddleOCR runtime already exists at $DestinationDir"
    exit 0
}

$WorkDir = Join-Path (Split-Path -Parent $DestinationDir) "_ocr-download"
$ArchivePath = Join-Path $WorkDir $ArchiveName
$ExtractDir = Join-Path $WorkDir "extract"

if (!(Test-Path -LiteralPath $WorkDir)) {
    New-Item -ItemType Directory -Path $WorkDir -Force | Out-Null
}

if (!(Test-Path -LiteralPath $ArchivePath)) {
    Write-Host "Downloading $ArchiveName"
    Invoke-WebRequestWithRetry -Uri $ArchiveUrl -OutFile $ArchivePath
}

Assert-FileHash -Path $ArchivePath -ExpectedSha256 $ArchiveSha256

if (Test-Path -LiteralPath $ExtractDir) {
    Remove-Item -LiteralPath $ExtractDir -Force -Recurse
}
New-Item -ItemType Directory -Path $ExtractDir -Force | Out-Null

7z x $ArchivePath "-o$ExtractDir" -y
if (!$?) { Exit $LASTEXITCODE }

$Exe = Get-ChildItem -LiteralPath $ExtractDir -Recurse -File |
    Where-Object { $_.Name -in @("PaddleOCR-json.exe", "PaddleOCR_json.exe") } |
    Select-Object -First 1
if (!$Exe) {
    throw "Could not find PaddleOCR-json.exe in $ArchiveName."
}

$RuntimeRoot = $Exe.Directory.FullName
$ArchiveModelsDir = Join-Path $RuntimeRoot "models"
if (!(Test-Path -LiteralPath $ArchiveModelsDir)) {
    throw "Could not find models directory next to $($Exe.FullName)."
}

if (Test-Path -LiteralPath $DestinationDir) {
    Remove-Item -LiteralPath $DestinationDir -Force -Recurse
}
New-Item -ItemType Directory -Path $BinDir -Force | Out-Null
New-Item -ItemType Directory -Path $ModelsDir -Force | Out-Null
New-Item -ItemType Directory -Path $LicensesDir -Force | Out-Null

Get-ChildItem -LiteralPath $RuntimeRoot -Force | ForEach-Object {
    if ($_.Name -ne "models") {
        Copy-Item -LiteralPath $_.FullName -Destination $BinDir -Recurse -Force
    }
}

if (Test-Path -LiteralPath (Join-Path $BinDir "PaddleOCR_json.exe")) {
    Rename-Item -LiteralPath (Join-Path $BinDir "PaddleOCR_json.exe") -NewName "PaddleOCR-json.exe" -Force
}

Copy-DirectoryContents -Source $ArchiveModelsDir -Destination $ModelsDir

Get-ChildItem -LiteralPath $RuntimeRoot -Recurse -File |
    Where-Object { $_.Name -match "^(LICENSE|NOTICE|COPYING|README)" } |
    ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $LicensesDir $_.Name) -Force
    }

@"
Aegisub bundled OCR runtime
===========================

Component: PaddleOCR-json $Version for Windows x64
Source: $ArchiveUrl
SHA256: $ArchiveSha256

PaddleOCR-json is built from PaddleOCR C++/Paddle Inference and bundles
PaddleOCR model files for offline recognition. The upstream release notes for
$Version state that the Windows package includes Simplified Chinese,
Traditional Chinese, English, Japanese, Korean, and Russian OCR model sets.

The Aegisub build stores the runtime under:
  ocr/bin
and the model/config files under:
  ocr/models

Third-party licenses copied from the upstream archive are stored in this folder
when present.
"@ | Set-Content -LiteralPath (Join-Path $LicensesDir "THIRD_PARTY_OCR.txt") -Encoding UTF8

$RequiredFiles = @(
    (Join-Path $BinDir "PaddleOCR-json.exe"),
    (Join-Path $ModelsDir "config_en.txt"),
    (Join-Path $ModelsDir "config_japan.txt"),
    (Join-Path $ModelsDir "config_chinese.txt")
)

foreach ($file in $RequiredFiles) {
    if (!(Test-Path -LiteralPath $file)) {
        throw "Required OCR file missing after extraction: $file"
    }
}

Write-Host "PaddleOCR runtime prepared at $DestinationDir"
