#!/usr/bin/env powershell

param (
    [Parameter(Mandatory = $true)]
    [string]$DestinationDir
)

$ErrorActionPreference = "Stop"

$ReleaseTag = "v1.4.1-dev"
$RuntimeVersion = "v1.4.1-dev.1"
$BundleVersion = "$RuntimeVersion-ppocrv5-mobile-det-server-rec-v3"
$ArchiveName = "PaddleOCR-json_v1.4.1_dev.1_windows_x86-64_cpu_mkl.7z"
$ArchiveUrl = "https://github.com/hiroi-sora/PaddleOCR-json/releases/download/$ReleaseTag/$ArchiveName"
$ArchiveSha256 = "46c3c82e889e5ed0c8a066ed3a089cd200d1e482823601bab23f5e41d137700f"

$PPOcrV5DetArchiveName = "PP-OCRv5_mobile_det_infer.tar"
$PPOcrV5RecArchiveName = "PP-OCRv5_server_rec_infer.tar"
$PPOcrV5ModelBaseUrl = "https://paddle-model-ecology.bj.bcebos.com/paddlex/official_inference_model/paddle3.0.0"
$PPOcrV5DetArchiveUrl = "$PPOcrV5ModelBaseUrl/$PPOcrV5DetArchiveName"
$PPOcrV5RecArchiveUrl = "$PPOcrV5ModelBaseUrl/$PPOcrV5RecArchiveName"
$PPOcrV5DetArchiveSha256 = "50446e5d01ac2a73d5319c89513281f6578414c888c602f9af13f93feefffc58"
$PPOcrV5RecArchiveSha256 = "d99be2ffd348943ab52876179168be4fb5b14f5f0812f2ae4c76d89ec2ea750a"
$PPOcrV5DictUrl = "https://raw.githubusercontent.com/PaddlePaddle/PaddleOCR/eaede685bcaf22f287edf8865f4dd8d374acb75e/ppocr/utils/dict/ppocrv5_dict.txt"

$RequiredConfigNames = @(
    "config_ppocrv5.txt",
    "config_korean.txt"
)

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

function Get-Sha256Hash {
    param(
        [Parameter(Mandatory = $true)][string]$Path
    )

    $resolved = (Resolve-Path -LiteralPath $Path).ProviderPath
    $stream = [System.IO.File]::OpenRead($resolved)
    try {
        $sha256 = [System.Security.Cryptography.SHA256]::Create()
        try {
            $hash = $sha256.ComputeHash($stream)
            return [System.BitConverter]::ToString($hash).Replace("-", "").ToLowerInvariant()
        }
        finally {
            $sha256.Dispose()
        }
    }
    finally {
        $stream.Dispose()
    }
}

function Assert-FileHash {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$ExpectedSha256
    )

    $actual = Get-Sha256Hash -Path $Path
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

function Get-OcrConfigSettings {
    param(
        [Parameter(Mandatory = $true)][string]$ConfigPath
    )

    $settings = @{}
    foreach ($line in Get-Content -LiteralPath $ConfigPath) {
        $trimmed = $line.Trim()
        if (!$trimmed -or $trimmed.StartsWith("#")) {
            continue
        }

        $parts = $trimmed -split "\s+", 2
        if ($parts.Count -eq 2) {
            $settings[$parts[0]] = $parts[1].Trim()
        }
    }
    return $settings
}

function Resolve-OcrConfigPath {
    param(
        [Parameter(Mandatory = $true)][string]$ConfiguredPath,
        [Parameter(Mandatory = $true)][string]$ModelsDir
    )

    $separator = [string][System.IO.Path]::DirectorySeparatorChar
    $normalized = $ConfiguredPath.Replace("/", $separator).Replace("\", $separator)
    if ([System.IO.Path]::IsPathRooted($normalized)) {
        return $normalized
    }

    if ($normalized -match "^models[\\/](.+)$") {
        return Join-Path $ModelsDir $Matches[1]
    }

    return Join-Path $ModelsDir $normalized
}

function Get-FolderFileList {
    param(
        [Parameter(Mandatory = $true)][string]$FolderPath
    )

    if (!(Test-Path -LiteralPath $FolderPath -PathType Container)) {
        return "<folder does not exist>"
    }

    $files = Get-ChildItem -LiteralPath $FolderPath -Force |
        Sort-Object Name |
        ForEach-Object {
            if ($_.PSIsContainer) {
                "$($_.Name)/"
            }
            else {
                $_.Name
            }
        }

    if (!$files) {
        return "<empty>"
    }

    return ($files -join ", ")
}

function New-OcrValidationError {
    param(
        [Parameter(Mandatory = $true)][string]$Message,
        [Parameter(Mandatory = $true)][string]$FolderPath,
        [Parameter(Mandatory = $true)][string]$ConfigPath,
        [Parameter(Mandatory = $true)][string]$DetModelDir,
        [Parameter(Mandatory = $true)][string]$ClsModelDir,
        [Parameter(Mandatory = $true)][string]$RecModelDir,
        [Parameter(Mandatory = $true)][string]$DictionaryPath
    )

    $filesPresent = Get-FolderFileList -FolderPath $FolderPath
    return @"
OCR model validation failed.
$Message

Folder path:
$FolderPath
Files present:
$filesPresent

Active config file path:
$ConfigPath
Selected det_model_dir:
$DetModelDir
Selected cls_model_dir:
$ClsModelDir
Selected rec_model_dir:
$RecModelDir
Selected dictionary path:
$DictionaryPath
"@
}

function Assert-OcrModelDirectory {
    param(
        [Parameter(Mandatory = $true)][string]$Role,
        [Parameter(Mandatory = $true)][string]$ModelDir,
        [Parameter(Mandatory = $true)][string]$ConfigPath,
        [Parameter(Mandatory = $true)][string]$DetModelDir,
        [Parameter(Mandatory = $true)][string]$ClsModelDir,
        [Parameter(Mandatory = $true)][string]$RecModelDir,
        [Parameter(Mandatory = $true)][string]$DictionaryPath
    )

    foreach ($fileName in @("inference.pdmodel", "inference.pdiparams")) {
        $filePath = Join-Path $ModelDir $fileName
        if (!(Test-Path -LiteralPath $filePath -PathType Leaf)) {
            throw (New-OcrValidationError `
                -Message "$Role model directory is missing required file: $filePath" `
                -FolderPath $ModelDir `
                -ConfigPath $ConfigPath `
                -DetModelDir $DetModelDir `
                -ClsModelDir $ClsModelDir `
                -RecModelDir $RecModelDir `
                -DictionaryPath $DictionaryPath)
        }
    }
}

function Assert-OcrModelConfig {
    param(
        [Parameter(Mandatory = $true)][string]$ConfigPath,
        [Parameter(Mandatory = $true)][string]$ModelsDir
    )

    if (!(Test-Path -LiteralPath $ConfigPath -PathType Leaf)) {
        throw "OCR model configuration is missing: $ConfigPath"
    }

    $settings = Get-OcrConfigSettings -ConfigPath $ConfigPath
    foreach ($key in @("det_model_dir", "rec_model_dir", "rec_char_dict_path")) {
        if (!$settings.ContainsKey($key) -or [string]::IsNullOrWhiteSpace($settings[$key])) {
            throw "OCR model configuration is incomplete: $ConfigPath`nMissing required setting: $key"
        }
    }

    $detModelDir = Resolve-OcrConfigPath -ConfiguredPath $settings["det_model_dir"] -ModelsDir $ModelsDir
    $clsModelDir = ""
    if ($settings.ContainsKey("cls_model_dir") -and ![string]::IsNullOrWhiteSpace($settings["cls_model_dir"])) {
        $clsModelDir = Resolve-OcrConfigPath -ConfiguredPath $settings["cls_model_dir"] -ModelsDir $ModelsDir
    }
    $recModelDir = Resolve-OcrConfigPath -ConfiguredPath $settings["rec_model_dir"] -ModelsDir $ModelsDir
    $dictionaryPath = Resolve-OcrConfigPath -ConfiguredPath $settings["rec_char_dict_path"] -ModelsDir $ModelsDir

    Assert-OcrModelDirectory `
        -Role "Detection" `
        -ModelDir $detModelDir `
        -ConfigPath $ConfigPath `
        -DetModelDir $detModelDir `
        -ClsModelDir $clsModelDir `
        -RecModelDir $recModelDir `
        -DictionaryPath $dictionaryPath

    if ($clsModelDir) {
        Assert-OcrModelDirectory `
            -Role "Classification" `
            -ModelDir $clsModelDir `
            -ConfigPath $ConfigPath `
            -DetModelDir $detModelDir `
            -ClsModelDir $clsModelDir `
            -RecModelDir $recModelDir `
            -DictionaryPath $dictionaryPath
    }

    Assert-OcrModelDirectory `
        -Role "Recognition" `
        -ModelDir $recModelDir `
        -ConfigPath $ConfigPath `
        -DetModelDir $detModelDir `
        -ClsModelDir $clsModelDir `
        -RecModelDir $recModelDir `
        -DictionaryPath $dictionaryPath

    if (!(Test-Path -LiteralPath $dictionaryPath -PathType Leaf)) {
        $dictionaryFolder = Split-Path -Parent $dictionaryPath
        throw (New-OcrValidationError `
            -Message "Recognition dictionary is missing: $dictionaryPath" `
            -FolderPath $dictionaryFolder `
            -ConfigPath $ConfigPath `
            -DetModelDir $detModelDir `
            -ClsModelDir $clsModelDir `
            -RecModelDir $recModelDir `
            -DictionaryPath $dictionaryPath)
    }
}

function Test-OcrRuntimeComplete {
    param(
        [Parameter(Mandatory = $true)][string]$BinDir,
        [Parameter(Mandatory = $true)][string]$ModelsDir,
        [Parameter(Mandatory = $true)][string]$RuntimeMarker,
        [Parameter(Mandatory = $true)][string]$BundleVersion
    )

    if (!(Test-Path -LiteralPath (Join-Path $BinDir "PaddleOCR-json.exe") -PathType Leaf)) {
        return $false
    }
    if (!(Test-Path -LiteralPath $RuntimeMarker -PathType Leaf)) {
        return $false
    }
    if ((Get-Content -LiteralPath $RuntimeMarker -Raw).Trim() -ne $BundleVersion) {
        return $false
    }

    try {
        foreach ($configName in $RequiredConfigNames) {
            Assert-OcrModelConfig -ConfigPath (Join-Path $ModelsDir $configName) -ModelsDir $ModelsDir
        }
    }
    catch {
        Write-Host "Existing PaddleOCR runtime at $DestinationDir is incomplete and will be rebuilt."
        Write-Host $_.Exception.Message
        return $false
    }

    return $true
}

$DestinationDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($DestinationDir)
$BinDir = Join-Path $DestinationDir "bin"
$ModelsDir = Join-Path $DestinationDir "models"
$LicensesDir = Join-Path $DestinationDir "licenses"
$RuntimeMarker = Join-Path $DestinationDir "OCR_RUNTIME_VERSION.txt"

if (Test-OcrRuntimeComplete -BinDir $BinDir -ModelsDir $ModelsDir -RuntimeMarker $RuntimeMarker -BundleVersion $BundleVersion) {
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

$DetArchivePath = Join-Path $WorkDir $PPOcrV5DetArchiveName
$RecArchivePath = Join-Path $WorkDir $PPOcrV5RecArchiveName
Download-File -Uri $PPOcrV5DetArchiveUrl -OutFile $DetArchivePath -ExpectedSha256 $PPOcrV5DetArchiveSha256
Download-File -Uri $PPOcrV5RecArchiveUrl -OutFile $RecArchivePath -ExpectedSha256 $PPOcrV5RecArchiveSha256

tar -xf $DetArchivePath -C $ModelsDir
if (!$?) { Exit $LASTEXITCODE }
tar -xf $RecArchivePath -C $ModelsDir
if (!$?) { Exit $LASTEXITCODE }

Download-File -Uri $PPOcrV5DictUrl -OutFile (Join-Path $ModelsDir "ppocrv5_dict.txt")

@(
    "# Aegisub default OCR model combo:",
    "# PP-OCRv5_mobile_det + PP-OCRv5_server_rec",
    "",
    "det_model_dir models/PP-OCRv5_mobile_det_infer",
    "cls_model_dir models/ch_ppocr_mobile_v2.0_cls_infer",
    "rec_model_dir models/PP-OCRv5_server_rec_infer",
    "rec_char_dict_path models/ppocrv5_dict.txt"
) | Set-Content -LiteralPath (Join-Path $ModelsDir "config_ppocrv5.txt") -Encoding ASCII

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

PaddleOCR-json is built from PaddleOCR C++/Paddle Inference. Aegisub targets
this default model combo:

  Detection:   PP-OCRv5_mobile_det
  Recognition: PP-OCRv5_server_rec
  Dictionary:  ppocrv5_dict.txt

The current PaddleOCR-json executable still tries to load inference.pdmodel from
each configured model directory. The official PP-OCRv5 archives hosted by
Paddle currently contain inference.json, inference.yml, and inference.pdiparams.
Packaging validation therefore fails unless the selected PP-OCRv5 folders also
contain inference.pdmodel.

The Aegisub build stores the runtime under:
  ocr/bin
and the model/config files under:
  ocr/models

Third-party licenses copied from the upstream archive are stored in this folder
when present.
"@ | Set-Content -LiteralPath (Join-Path $LicensesDir "THIRD_PARTY_OCR.txt") -Encoding UTF8

$RequiredFiles = @(
    (Join-Path $BinDir "PaddleOCR-json.exe")
)

foreach ($configName in $RequiredConfigNames) {
    $RequiredFiles += (Join-Path $ModelsDir $configName)
}

foreach ($file in $RequiredFiles) {
    if (!(Test-Path -LiteralPath $file)) {
        throw "Required OCR file missing after extraction: $file"
    }
}

foreach ($configName in $RequiredConfigNames) {
    Assert-OcrModelConfig -ConfigPath (Join-Path $ModelsDir $configName) -ModelsDir $ModelsDir
}

$BundleVersion | Set-Content -LiteralPath $RuntimeMarker -Encoding ASCII

Write-Host "PaddleOCR runtime prepared at $DestinationDir"
