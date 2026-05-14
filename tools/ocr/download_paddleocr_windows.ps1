#!/usr/bin/env powershell

param (
    [Parameter(Mandatory = $true)]
    [string]$DestinationDir
)

$ErrorActionPreference = "Stop"

$ReleaseTag = "v1.4.1-dev"
$RuntimeVersion = "v1.4.1-dev.1"
$ArchiveName = "PaddleOCR-json_v1.4.1_dev.1_windows_x86-64_cpu_mkl.7z"
$ArchiveUrl = "https://github.com/hiroi-sora/PaddleOCR-json/releases/download/$ReleaseTag/$ArchiveName"
$ArchiveSha256 = "46c3c82e889e5ed0c8a066ed3a089cd200d1e482823601bab23f5e41d137700f"

$PPOcrV5DetRepo = "https://huggingface.co/PaddlePaddle/PP-OCRv5_mobile_det/resolve/74393d9baa66aca476e8c9e5dbdd71930cc534a8"
$PPOcrV5RecRepo = "https://huggingface.co/PaddlePaddle/PP-OCRv5_server_rec/resolve/6ba04e6e502b95b7ff3f7ced26476d1be0f3ba62"
$PPOcrV5DictUrl = "https://raw.githubusercontent.com/PaddlePaddle/PaddleOCR/eaede685bcaf22f287edf8865f4dd8d374acb75e/ppocr/utils/dict/ppocrv5_dict.txt"

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

function Download-File {
    param(
        [Parameter(Mandatory = $true)][string]$Uri,
        [Parameter(Mandatory = $true)][string]$OutFile,
        [string]$ExpectedSha256 = ""
    )

    if (!(Test-Path -LiteralPath $OutFile)) {
        Invoke-WebRequestWithRetry -Uri $Uri -OutFile $OutFile
    }

    if ($ExpectedSha256) {
        Assert-FileHash -Path $OutFile -ExpectedSha256 $ExpectedSha256
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
$RuntimeMarker = Join-Path $DestinationDir "OCR_RUNTIME_VERSION.txt"

if ((Test-Path -LiteralPath (Join-Path $BinDir "PaddleOCR-json.exe")) -and
    (Test-Path -LiteralPath $RuntimeMarker) -and
    ((Get-Content -LiteralPath $RuntimeMarker -Raw).Trim() -eq $RuntimeVersion) -and
    (Test-Path -LiteralPath (Join-Path $ModelsDir "config_ppocrv5.txt")) -and
    (Test-Path -LiteralPath (Join-Path $ModelsDir "PP-OCRv5_mobile_det_infer\inference.pdiparams")) -and
    (Test-Path -LiteralPath (Join-Path $ModelsDir "PP-OCRv5_server_rec_infer\inference.pdiparams")) -and
    (Test-Path -LiteralPath (Join-Path $ModelsDir "ppocrv5_dict.txt"))) {
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

$DetDir = Join-Path $ModelsDir "PP-OCRv5_mobile_det_infer"
$RecDir = Join-Path $ModelsDir "PP-OCRv5_server_rec_infer"
New-Item -ItemType Directory -Path $DetDir -Force | Out-Null
New-Item -ItemType Directory -Path $RecDir -Force | Out-Null

Download-File -Uri "$PPOcrV5DetRepo/inference.json?download=true" -OutFile (Join-Path $DetDir "inference.json")
Download-File -Uri "$PPOcrV5DetRepo/inference.yml?download=true" -OutFile (Join-Path $DetDir "inference.yml")
Download-File -Uri "$PPOcrV5DetRepo/inference.pdiparams?download=true" -OutFile (Join-Path $DetDir "inference.pdiparams") -ExpectedSha256 "afa1820cb16c1fd0dad589d0f8b389139061c1ef6d68019685fd07be997dda5b"

Download-File -Uri "$PPOcrV5RecRepo/inference.json?download=true" -OutFile (Join-Path $RecDir "inference.json")
Download-File -Uri "$PPOcrV5RecRepo/inference.yml?download=true" -OutFile (Join-Path $RecDir "inference.yml")
Download-File -Uri "$PPOcrV5RecRepo/inference.pdiparams?download=true" -OutFile (Join-Path $RecDir "inference.pdiparams") -ExpectedSha256 "63853f062a5f4089befc16f565a68277618e0da5cb45468b49d11079de0ada77"

Download-File -Uri $PPOcrV5DictUrl -OutFile (Join-Path $ModelsDir "ppocrv5_dict.txt")

@"
# Aegisub default OCR model combo:
# PP-OCRv5_mobile_det + PP-OCRv5_server_rec

det_model_dir models/PP-OCRv5_mobile_det_infer
cls_model_dir models/ch_ppocr_mobile_v2.0_cls_infer
rec_model_dir models/PP-OCRv5_server_rec_infer
rec_char_dict_path models/ppocrv5_dict.txt
"@ | Set-Content -LiteralPath (Join-Path $ModelsDir "config_ppocrv5.txt") -Encoding ASCII

Get-ChildItem -LiteralPath $RuntimeRoot -Recurse -File |
    Where-Object { $_.Name -match "^(LICENSE|NOTICE|COPYING|README)" } |
    ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $LicensesDir $_.Name) -Force
    }

@"
Aegisub bundled OCR runtime
===========================

Component: PaddleOCR-json $RuntimeVersion for Windows x86-64 CPU MKL
Source: $ArchiveUrl
SHA256: $ArchiveSha256

PaddleOCR-json is built from PaddleOCR C++/Paddle Inference. This dev runtime
uses Paddle Inference 3.0.0 beta-1, which can load the inference.json model
format used by the official PP-OCRv5 model files. Aegisub adds the official
PP-OCRv5_mobile_det and PP-OCRv5_server_rec inference files and uses that
combination by default for Chinese, English, Traditional Chinese, and Japanese.
Korean remains available through the PaddleOCR-json bundled Korean config.

PP-OCRv5_mobile_det inference.pdiparams SHA256:
afa1820cb16c1fd0dad589d0f8b389139061c1ef6d68019685fd07be997dda5b

PP-OCRv5_server_rec inference.pdiparams SHA256:
63853f062a5f4089befc16f565a68277618e0da5cb45468b49d11079de0ada77

The Aegisub build stores the runtime under:
  ocr/bin
and the model/config files under:
  ocr/models

Third-party licenses copied from the upstream archive are stored in this folder
when present.
"@ | Set-Content -LiteralPath (Join-Path $LicensesDir "THIRD_PARTY_OCR.txt") -Encoding UTF8

$RuntimeVersion | Set-Content -LiteralPath $RuntimeMarker -Encoding ASCII

$RequiredFiles = @(
    $RuntimeMarker,
    (Join-Path $BinDir "PaddleOCR-json.exe"),
    (Join-Path $ModelsDir "config_ppocrv5.txt"),
    (Join-Path $ModelsDir "config_korean.txt"),
    (Join-Path $ModelsDir "PP-OCRv5_mobile_det_infer\inference.json"),
    (Join-Path $ModelsDir "PP-OCRv5_mobile_det_infer\inference.pdiparams"),
    (Join-Path $ModelsDir "PP-OCRv5_server_rec_infer\inference.json"),
    (Join-Path $ModelsDir "PP-OCRv5_server_rec_infer\inference.pdiparams"),
    (Join-Path $ModelsDir "ppocrv5_dict.txt")
)

foreach ($file in $RequiredFiles) {
    if (!(Test-Path -LiteralPath $file)) {
        throw "Required OCR file missing after extraction: $file"
    }
}

Write-Host "PaddleOCR runtime prepared at $DestinationDir"
