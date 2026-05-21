#!/usr/bin/env powershell

param (
    [Parameter(Mandatory = $true)]
    [string]$DestinationDir
)

$ErrorActionPreference = "Stop"

$ReleaseTag = "v1.4.1-dev"
$RuntimeVersion = "v1.4.1-dev.1"
$PaddleOCRSourceVersion = "v3.0.0"
$PaddlePaddleVersion = "3.0.0"
$BundleVersion = "$RuntimeVersion-ppocrv5-server-det-server-rec-legacy-export-v1"
$ArchiveName = "PaddleOCR-json_v1.4.1_dev.1_windows_x86-64_cpu_mkl.7z"
$ArchiveUrl = "https://github.com/hiroi-sora/PaddleOCR-json/releases/download/$ReleaseTag/$ArchiveName"
$ArchiveSha256 = "46c3c82e889e5ed0c8a066ed3a089cd200d1e482823601bab23f5e41d137700f"

$PaddleOCRSourceArchiveName = "PaddleOCR-$($PaddleOCRSourceVersion.TrimStart("v")).zip"
$PaddleOCRSourceArchiveUrl = "https://github.com/PaddlePaddle/PaddleOCR/archive/refs/tags/$PaddleOCRSourceVersion.zip"
$PaddleOCRSourceFolderName = "PaddleOCR-$($PaddleOCRSourceVersion.TrimStart("v"))"
$PPOcrV5DetConfig = "configs/det/PP-OCRv5/PP-OCRv5_server_det.yml"
$PPOcrV5RecConfig = "configs/rec/PP-OCRv5/PP-OCRv5_server_rec.yml"

$PPOcrV5DetArchiveName = "PP-OCRv5_server_det_infer.tar"
$PPOcrV5RecArchiveName = "PP-OCRv5_server_rec_infer.tar"
$PPOcrV5ModelBaseUrl = "https://paddle-model-ecology.bj.bcebos.com/paddlex/official_inference_model/paddle3.0.0"
$PPOcrV5DetArchiveUrl = "$PPOcrV5ModelBaseUrl/$PPOcrV5DetArchiveName"
$PPOcrV5RecArchiveUrl = "$PPOcrV5ModelBaseUrl/$PPOcrV5RecArchiveName"
$PPOcrV5DetArchiveSha256 = "22a33e0ba6a21425ea4192da03bf4395c9a0c67902bd924b7328fc859073045d"
$PPOcrV5RecArchiveSha256 = "d99be2ffd348943ab52876179168be4fb5b14f5f0812f2ae4c76d89ec2ea750a"
$PPOcrV5DictUrl = "https://raw.githubusercontent.com/PaddlePaddle/PaddleOCR/eaede685bcaf22f287edf8865f4dd8d374acb75e/ppocr/utils/dict/ppocrv5_dict.txt"

$PPOcrV5PretrainedBaseUrl = "https://paddle-model-ecology.bj.bcebos.com/paddlex/official_pretrained_model"
$PPOcrV5DetPretrainedName = "PP-OCRv5_server_det_pretrained.pdparams"
$PPOcrV5RecPretrainedName = "PP-OCRv5_server_rec_pretrained.pdparams"
$PPOcrV5DetPretrainedUrl = "$PPOcrV5PretrainedBaseUrl/$PPOcrV5DetPretrainedName"
$PPOcrV5RecPretrainedUrl = "$PPOcrV5PretrainedBaseUrl/$PPOcrV5RecPretrainedName"
$PPOcrV5DetPretrainedSha256 = "2802f7d4748ea592819ae4550c195c5bdb43755dfdb5ebd25e01bb4d885aebc9"
# Keep the large recognition download size-pinned so partial files are rejected.
$PPOcrV5RecPretrainedExpectedBytes = 214594738

$RequiredConfigNames = @(
    "config_ppocrv5.txt"
)

$AllowedModelAssetNames = @(
    "#cmd.txt",
    "config.txt",
    "config_ppocrv5.txt",
    "PP-OCRv5_server_det_infer",
    "PP-OCRv5_server_rec_infer",
    "ch_ppocr_mobile_v2.0_cls_infer",
    "ppocrv5_dict.txt"
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

function Assert-FileLength {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][long]$ExpectedBytes
    )

    $actual = (Get-Item -LiteralPath $Path).Length
    if ($actual -ne $ExpectedBytes) {
        throw "Size mismatch for $Path. Expected $ExpectedBytes bytes, got $actual bytes."
    }
}

function Download-File {
    param(
        [Parameter(Mandatory = $true)][string]$Uri,
        [Parameter(Mandatory = $true)][string]$OutFile,
        [string]$ExpectedSha256 = "",
        [long]$ExpectedBytes = 0
    )

    if (Test-Path -LiteralPath $OutFile) {
        if ($ExpectedBytes -gt 0 -and (Get-Item -LiteralPath $OutFile).Length -ne $ExpectedBytes) {
            Remove-Item -LiteralPath $OutFile -Force
        }
    }

    if (!(Test-Path -LiteralPath $OutFile)) {
        Invoke-WebRequestWithRetry -Uri $Uri -OutFile $OutFile
    }

    if ($ExpectedBytes -gt 0) {
        Assert-FileLength -Path $OutFile -ExpectedBytes $ExpectedBytes
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

function Get-AllowedModelAssetSet {
    $set = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($name in $AllowedModelAssetNames) {
        [void]$set.Add($name)
    }
    return $set
}

function Remove-UnusedOcrModelAssets {
    param(
        [Parameter(Mandatory = $true)][string]$ModelsDir
    )

    if (!(Test-Path -LiteralPath $ModelsDir -PathType Container)) {
        return
    }

    $allowed = Get-AllowedModelAssetSet
    Get-ChildItem -LiteralPath $ModelsDir -Force | ForEach-Object {
        if (!$allowed.Contains($_.Name)) {
            Remove-Item -LiteralPath $_.FullName -Recurse -Force
        }
    }
}

function Assert-NoUnusedOcrModelAssets {
    param(
        [Parameter(Mandatory = $true)][string]$ModelsDir
    )

    $allowed = Get-AllowedModelAssetSet
    $unexpected = Get-ChildItem -LiteralPath $ModelsDir -Force |
        Where-Object { !$allowed.Contains($_.Name) } |
        ForEach-Object { $_.Name } |
        Sort-Object

    if ($unexpected) {
        throw "OCR models folder contains unused bundled model/config assets that should not be packaged:`n$($unexpected -join "`n")"
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

function Assert-OfficialPPOcrV5ModelDirectory {
    param(
        [Parameter(Mandatory = $true)][string]$Role,
        [Parameter(Mandatory = $true)][string]$ModelDir
    )

    foreach ($fileName in @("inference.json", "inference.pdiparams")) {
        $filePath = Join-Path $ModelDir $fileName
        if (!(Test-Path -LiteralPath $filePath -PathType Leaf)) {
            throw "Official PP-OCRv5 $Role archive is incomplete. Missing $filePath.`nFiles present: $(Get-FolderFileList -FolderPath $ModelDir)"
        }
    }

    $ymlPath = Join-Path $ModelDir "inference.yml"
    $yamlPath = Join-Path $ModelDir "inference.yaml"
    if (!(Test-Path -LiteralPath $ymlPath -PathType Leaf) -and !(Test-Path -LiteralPath $yamlPath -PathType Leaf)) {
        throw "Official PP-OCRv5 $Role archive is incomplete. Missing inference.yml or inference.yaml in $ModelDir.`nFiles present: $(Get-FolderFileList -FolderPath $ModelDir)"
    }
}

function Assert-LegacyRuntimeModelDirectory {
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
            $formatHint = ""
            if (Test-Path -LiteralPath (Join-Path $ModelDir "inference.json") -PathType Leaf) {
                $formatHint = "`nThis folder contains the official PP-OCRv5 Paddle 3/PIR files. The bundled PaddleOCR-json runtime cannot load those files directly; packaging must export a legacy Paddle Inference folder first."
            }
            throw (New-OcrValidationError `
                -Message "$Role model directory is missing required runtime file: $filePath$formatHint" `
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

    Assert-LegacyRuntimeModelDirectory `
        -Role "Detection" `
        -ModelDir $detModelDir `
        -ConfigPath $ConfigPath `
        -DetModelDir $detModelDir `
        -ClsModelDir $clsModelDir `
        -RecModelDir $recModelDir `
        -DictionaryPath $dictionaryPath

    if ($clsModelDir) {
        Assert-LegacyRuntimeModelDirectory `
            -Role "Classification" `
            -ModelDir $clsModelDir `
            -ConfigPath $ConfigPath `
            -DetModelDir $detModelDir `
            -ClsModelDir $clsModelDir `
            -RecModelDir $recModelDir `
            -DictionaryPath $dictionaryPath
    }

    Assert-LegacyRuntimeModelDirectory `
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

function Assert-PPOcrV5DefaultConfig {
    param(
        [Parameter(Mandatory = $true)][string]$ConfigPath,
        [Parameter(Mandatory = $true)][string]$ModelsDir
    )

    $settings = Get-OcrConfigSettings -ConfigPath $ConfigPath
    $detModelDir = Resolve-OcrConfigPath -ConfiguredPath $settings["det_model_dir"] -ModelsDir $ModelsDir
    $recModelDir = Resolve-OcrConfigPath -ConfiguredPath $settings["rec_model_dir"] -ModelsDir $ModelsDir
    $dictionaryPath = Resolve-OcrConfigPath -ConfiguredPath $settings["rec_char_dict_path"] -ModelsDir $ModelsDir
    $clsModelDir = ""
    if ($settings.ContainsKey("cls_model_dir") -and ![string]::IsNullOrWhiteSpace($settings["cls_model_dir"])) {
        $clsModelDir = Resolve-OcrConfigPath -ConfiguredPath $settings["cls_model_dir"] -ModelsDir $ModelsDir
    }

    if ((Split-Path -Leaf $detModelDir) -ne "PP-OCRv5_server_det_infer") {
        throw (New-OcrValidationError `
            -Message "Default PP-OCRv5 config must use PP-OCRv5_server_det_infer, but det_model_dir is $detModelDir." `
            -FolderPath $detModelDir `
            -ConfigPath $ConfigPath `
            -DetModelDir $detModelDir `
            -ClsModelDir $clsModelDir `
            -RecModelDir $recModelDir `
            -DictionaryPath $dictionaryPath)
    }

    if ((Split-Path -Leaf $recModelDir) -ne "PP-OCRv5_server_rec_infer") {
        throw (New-OcrValidationError `
            -Message "Default PP-OCRv5 config must use PP-OCRv5_server_rec_infer, but rec_model_dir is $recModelDir." `
            -FolderPath $recModelDir `
            -ConfigPath $ConfigPath `
            -DetModelDir $detModelDir `
            -ClsModelDir $clsModelDir `
            -RecModelDir $recModelDir `
            -DictionaryPath $dictionaryPath)
    }

    if ((Split-Path -Leaf $dictionaryPath) -ne "ppocrv5_dict.txt") {
        throw (New-OcrValidationError `
            -Message "Default PP-OCRv5 config must use ppocrv5_dict.txt, but rec_char_dict_path is $dictionaryPath." `
            -FolderPath (Split-Path -Parent $dictionaryPath) `
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
            $configPath = Join-Path $ModelsDir $configName
            Assert-OcrModelConfig -ConfigPath $configPath -ModelsDir $ModelsDir
            if ($configName -eq "config_ppocrv5.txt") {
                Assert-PPOcrV5DefaultConfig -ConfigPath $configPath -ModelsDir $ModelsDir
            }
        }
        Assert-NoUnusedOcrModelAssets -ModelsDir $ModelsDir
    }
    catch {
        Write-Host "Existing PaddleOCR runtime at $DestinationDir is incomplete and will be rebuilt."
        Write-Host $_.Exception.Message
        return $false
    }

    return $true
}

function Quote-ProcessArgument {
    param(
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$Argument
    )

    if ($Argument.Length -gt 0 -and $Argument -notmatch '[\s"]') {
        return $Argument
    }

    $result = '"'
    $slashes = 0
    foreach ($char in $Argument.ToCharArray()) {
        if ($char -eq '\') {
            ++$slashes
            continue
        }

        if ($char -eq '"') {
            $result += ('\' * ($slashes * 2 + 1))
            $result += '"'
            $slashes = 0
            continue
        }

        if ($slashes -gt 0) {
            $result += ('\' * $slashes)
            $slashes = 0
        }
        $result += $char
    }

    if ($slashes -gt 0) {
        $result += ('\' * ($slashes * 2))
    }
    $result += '"'
    return $result
}

function Join-ProcessArguments {
    param(
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    return (($Arguments | ForEach-Object { Quote-ProcessArgument -Argument $_ }) -join " ")
}

function Invoke-CapturedProcess {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments = @(),
        [string]$WorkingDirectory = "",
        [hashtable]$Environment = @{}
    )

    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $FilePath
    $startInfo.Arguments = Join-ProcessArguments -Arguments $Arguments
    if ($WorkingDirectory) {
        $startInfo.WorkingDirectory = $WorkingDirectory
    }
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    foreach ($key in $Environment.Keys) {
        $startInfo.EnvironmentVariables[$key] = [string]$Environment[$key]
    }

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $startInfo
    [void]$process.Start()
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()

    return [pscustomobject]@{
        ExitCode = $process.ExitCode
        Stdout = $stdout
        Stderr = $stderr
        Command = "$FilePath $($startInfo.Arguments)"
    }
}

function Invoke-CheckedProcess {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments = @(),
        [string]$WorkingDirectory = "",
        [hashtable]$Environment = @{},
        [Parameter(Mandatory = $true)][string]$FailureMessage
    )

    $result = Invoke-CapturedProcess -FilePath $FilePath -Arguments $Arguments -WorkingDirectory $WorkingDirectory -Environment $Environment
    if ($result.ExitCode -ne 0) {
        throw @"
$FailureMessage
Command:
$($result.Command)
Exit code:
$($result.ExitCode)
stdout:
$($result.Stdout.Trim())
stderr:
$($result.Stderr.Trim())
"@
    }

    return $result
}

function Get-PythonExecutable {
    $python = Get-Command python -ErrorAction SilentlyContinue
    if ($python) {
        return $python.Source
    }

    throw "Python was not found on PATH. Windows CI must run actions/setup-python before preparing the PP-OCRv5 OCR runtime."
}

function Expand-PaddleOCRSource {
    param(
        [Parameter(Mandatory = $true)][string]$WorkDir
    )

    $sourceArchivePath = Join-Path $WorkDir $PaddleOCRSourceArchiveName
    $sourceExtractDir = Join-Path $WorkDir "paddleocr-source"
    $sourceDir = Join-Path $sourceExtractDir $PaddleOCRSourceFolderName

    Download-File -Uri $PaddleOCRSourceArchiveUrl -OutFile $sourceArchivePath

    if (!(Test-Path -LiteralPath (Join-Path $sourceDir "tools\export_model.py") -PathType Leaf)) {
        if (Test-Path -LiteralPath $sourceExtractDir) {
            Remove-Item -LiteralPath $sourceExtractDir -Force -Recurse
        }
        New-Item -ItemType Directory -Path $sourceExtractDir -Force | Out-Null
        Expand-Archive -LiteralPath $sourceArchivePath -DestinationPath $sourceExtractDir -Force
    }

    foreach ($path in @(
        (Join-Path $sourceDir "tools\export_model.py"),
        (Join-Path $sourceDir $PPOcrV5DetConfig),
        (Join-Path $sourceDir $PPOcrV5RecConfig),
        (Join-Path $sourceDir "ppocr\utils\dict\ppocrv5_dict.txt")
    )) {
        if (!(Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "PaddleOCR source $PaddleOCRSourceVersion is missing required export file: $path"
        }
    }

    return $sourceDir
}

function Install-PaddleExportDependencies {
    param(
        [Parameter(Mandatory = $true)][string]$PythonExe,
        [Parameter(Mandatory = $true)][string]$PaddleOCRSourceDir
    )

    Write-Host "Installing PaddleOCR export dependencies for PP-OCRv5 legacy model export"
    Invoke-CheckedProcess `
        -FilePath $PythonExe `
        -Arguments @("-m", "pip", "install", "--upgrade", "pip", "setuptools", "wheel") `
        -FailureMessage "Failed to update Python packaging tools for PP-OCRv5 export." | Out-Null

    Invoke-CheckedProcess `
        -FilePath $PythonExe `
        -Arguments @("-m", "pip", "install", "paddlepaddle==$PaddlePaddleVersion") `
        -FailureMessage "Failed to install PaddlePaddle $PaddlePaddleVersion for PP-OCRv5 export." | Out-Null

    Invoke-CheckedProcess `
        -FilePath $PythonExe `
        -Arguments @("-m", "pip", "install", "-r", (Join-Path $PaddleOCRSourceDir "requirements.txt")) `
        -FailureMessage "Failed to install PaddleOCR $PaddleOCRSourceVersion export requirements." | Out-Null
}

function Export-PPOcrV5LegacyModel {
    param(
        [Parameter(Mandatory = $true)][string]$PythonExe,
        [Parameter(Mandatory = $true)][string]$PaddleOCRSourceDir,
        [Parameter(Mandatory = $true)][string]$ConfigRelativePath,
        [Parameter(Mandatory = $true)][string]$PretrainedPath,
        [Parameter(Mandatory = $true)][string]$OutputDir,
        [Parameter(Mandatory = $true)][string]$Role
    )

    if (Test-Path -LiteralPath $OutputDir) {
        Remove-Item -LiteralPath $OutputDir -Force -Recurse
    }
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null

    $pretrainedPrefix = $PretrainedPath
    if ($pretrainedPrefix.EndsWith(".pdparams", [System.StringComparison]::OrdinalIgnoreCase)) {
        $pretrainedPrefix = $pretrainedPrefix.Substring(0, $pretrainedPrefix.Length - ".pdparams".Length)
    }

    $exportScript = Join-Path $PaddleOCRSourceDir "tools\export_model.py"
    $configPath = Join-Path $PaddleOCRSourceDir $ConfigRelativePath
    $dictPath = Join-Path $PaddleOCRSourceDir "ppocr\utils\dict\ppocrv5_dict.txt"
    $pythonPath = $PaddleOCRSourceDir
    if ($env:PYTHONPATH) {
        $pythonPath = "$PaddleOCRSourceDir;$env:PYTHONPATH"
    }

    Write-Host "Exporting PP-OCRv5 $Role model to legacy Paddle Inference format"
    Invoke-CheckedProcess `
        -FilePath $PythonExe `
        -Arguments @(
            $exportScript,
            "-c",
            $configPath,
            "-o",
            "Global.pretrained_model=$pretrainedPrefix",
            "Global.save_inference_dir=$OutputDir",
            "Global.export_with_pir=False",
            "Global.use_gpu=False",
            "Global.character_dict_path=$dictPath"
        ) `
        -WorkingDirectory $PaddleOCRSourceDir `
        -Environment @{
            "FLAGS_enable_pir_api" = "0"
            "PYTHONPATH" = $pythonPath
        } `
        -FailureMessage "Failed to export PP-OCRv5 $Role model to the legacy inference.pdmodel format required by PaddleOCR-json." | Out-Null

    foreach ($fileName in @("inference.pdmodel", "inference.pdiparams")) {
        $filePath = Join-Path $OutputDir $fileName
        if (!(Test-Path -LiteralPath $filePath -PathType Leaf)) {
            throw "PP-OCRv5 $Role export completed but did not produce $filePath. This usually means PaddleOCR exported PIR inference.json instead of the legacy Paddle Inference format."
        }
    }
}

function Get-JsonFromRuntimeOutput {
    param(
        [AllowEmptyString()][string]$Text
    )

    $objectStart = $Text.IndexOf("{")
    $objectEnd = $Text.LastIndexOf("}")
    if ($objectStart -lt 0 -or $objectEnd -lt $objectStart) {
        return ""
    }

    return $Text.Substring($objectStart, $objectEnd - $objectStart + 1)
}

function New-SmokeImage {
    param(
        [Parameter(Mandatory = $true)][string]$Path
    )

    $pngBase64 = "iVBORw0KGgoAAAANSUhEUgAAAIAAAAAwCAYAAADZ9HK+AAAAAXNSR0IArs4c6QAAAARnQU1BAACxjwv8YQUAAAAJcEhZcwAADsMAAA7DAcdvqGQAAAIwSURBVHhe7ZhRcoUgDEVdngtyOe7FrbgTSqc+jAhCAm3Ve89Mft5gdMgh8BgcgYYCgEMBwKEA4FAAcCgAOBQAHAoADgUAhwKAQwHAoQDgUABwKAA4XQVYpsENQyLG2a3bGC25nONcyri6eUw/m4tyzvfRRYBs4eOoFqG+eNOyPXJCL8BPTC6b8oU0ChBPcnryjoKM7nqhLW4KY32kKrzObhRj0itXfFveksDhGxs61tNoEkBOWnmOZWFzq0wKVVqJcmxKKp0A36zzeJHvndgFWKZtsmqK/0FIkHhIX4CrfHoBZD6U84BRAMvkbgRx4iLbcu5dKO4Ylnz7MxTgCrEHa+ufXWVNOVOwA9RgEmBv1bYTc1i14rDVmvOMXgCeASpJFVBDmOiUAN1O4BoBxNiq8e/hfwUQq70155moqLXR7f3PgAKEwGn7ktsI8OdbwOEyqde543nc5hDof+xcjJozgLhHYAdQEFaPZdJ6/w385IvFqRHAA94JbALUTm6KzhdBezey59tz6N79BowCeH7hKjgvRw6R73R20AglxvpAcsAugCfs5VWTJvfbXKuVhSi141LRNAJ4hND9DqL3p0mAuAi5oklRyqtbiuIjVTxZLB/pa1ulAB75nbwKVnDYQ6+iemVFElxEvrZ6AY7vtRxwn0cXAT5kRTC31LwI5RVqEcADthV0FYA8DwoADgUAhwKAQwHAoQDgUABwKAA4FAAcCgAOBQCHAoBDAcChANA49wWZCZaaHAYU1QAAAABJRU5ErkJggg=="
    [System.IO.File]::WriteAllBytes($Path, [Convert]::FromBase64String($pngBase64))
}

function Get-RuntimeFailureKind {
    param(
        [AllowEmptyString()][string]$Stdout,
        [AllowEmptyString()][string]$Stderr
    )

    $combined = "$Stdout`n$Stderr"
    if ($combined -match "Cannot open file|No such file|not found") {
        return "missing files or bad config path"
    }
    if ($combined -match "inference\.json|PIR|pdmodel|unsupported|not support|model format") {
        return "unsupported model format"
    }
    if ($combined -match "Load config|config") {
        return "bad config path"
    }
    return "runtime executable failure"
}

function Test-PPOcrV5RuntimeLoad {
    param(
        [Parameter(Mandatory = $true)][string]$ExePath,
        [Parameter(Mandatory = $true)][string]$ModelsDir,
        [Parameter(Mandatory = $true)][string]$ConfigPath,
        [Parameter(Mandatory = $true)][string]$WorkDir
    )

    $smokeImage = Join-Path $WorkDir "ppocrv5-smoke.png"
    New-SmokeImage -Path $smokeImage

    $result = Invoke-CapturedProcess `
        -FilePath $ExePath `
        -Arguments @(
            "-image_path=$smokeImage",
            "-models_path=$ModelsDir",
            "-config_path=$ConfigPath",
            "-ensure_ascii=false"
        )

    if ($result.ExitCode -ne 0) {
        $kind = Get-RuntimeFailureKind -Stdout $result.Stdout -Stderr $result.Stderr
        throw @"
PaddleOCR-json PP-OCRv5 runtime load check failed: $kind.
Command:
$($result.Command)
Exit code:
$($result.ExitCode)
stdout:
$($result.Stdout.Trim())
stderr:
$($result.Stderr.Trim())
"@
    }

    $jsonText = Get-JsonFromRuntimeOutput -Text $result.Stdout
    if (!$jsonText) {
        throw @"
PaddleOCR-json PP-OCRv5 runtime load check failed: runtime executable failure.
The process exited successfully but stdout did not contain JSON.
Command:
$($result.Command)
stdout:
$($result.Stdout.Trim())
stderr:
$($result.Stderr.Trim())
"@
    }

    try {
        $json = $jsonText | ConvertFrom-Json
    }
    catch {
        throw @"
PaddleOCR-json PP-OCRv5 runtime load check failed: runtime executable failure.
The process returned malformed JSON.
Command:
$($result.Command)
stdout:
$($result.Stdout.Trim())
stderr:
$($result.Stderr.Trim())
JSON error:
$($_.Exception.Message)
"@
    }

    if ($null -ne $json.code -and $json.code -notin @(100, 101)) {
        throw @"
PaddleOCR-json PP-OCRv5 runtime load check failed: runtime executable failure.
The runtime loaded the models but returned OCR code $($json.code).
Command:
$($result.Command)
stdout:
$($result.Stdout.Trim())
stderr:
$($result.Stderr.Trim())
"@
    }

    Write-Host "PaddleOCR-json loaded the PP-OCRv5 detection and recognition models successfully."
}

$DestinationDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($DestinationDir)
$BinDir = Join-Path $DestinationDir "bin"
$ModelsDir = Join-Path $DestinationDir "models"
$LicensesDir = Join-Path $DestinationDir "licenses"
$RuntimeMarker = Join-Path $DestinationDir "OCR_RUNTIME_VERSION.txt"
$WorkDir = Join-Path (Split-Path -Parent $DestinationDir) "_ocr-download"

if (!(Test-Path -LiteralPath $WorkDir)) {
    New-Item -ItemType Directory -Path $WorkDir -Force | Out-Null
}

if (Test-OcrRuntimeComplete -BinDir $BinDir -ModelsDir $ModelsDir -RuntimeMarker $RuntimeMarker -BundleVersion $BundleVersion) {
    Test-PPOcrV5RuntimeLoad `
        -ExePath (Join-Path $BinDir "PaddleOCR-json.exe") `
        -ModelsDir $ModelsDir `
        -ConfigPath (Join-Path $ModelsDir "config_ppocrv5.txt") `
        -WorkDir $WorkDir

    Write-Host "PaddleOCR runtime already exists at $DestinationDir"
    exit 0
}

$ArchivePath = Join-Path $WorkDir $ArchiveName
$ExtractDir = Join-Path $WorkDir "extract"
$OfficialModelsDir = Join-Path $WorkDir "official-ppocrv5"

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
Remove-UnusedOcrModelAssets -ModelsDir $ModelsDir

$DetArchivePath = Join-Path $WorkDir $PPOcrV5DetArchiveName
$RecArchivePath = Join-Path $WorkDir $PPOcrV5RecArchiveName
Download-File -Uri $PPOcrV5DetArchiveUrl -OutFile $DetArchivePath -ExpectedSha256 $PPOcrV5DetArchiveSha256
Download-File -Uri $PPOcrV5RecArchiveUrl -OutFile $RecArchivePath -ExpectedSha256 $PPOcrV5RecArchiveSha256

if (Test-Path -LiteralPath $OfficialModelsDir) {
    Remove-Item -LiteralPath $OfficialModelsDir -Force -Recurse
}
New-Item -ItemType Directory -Path $OfficialModelsDir -Force | Out-Null

tar -xf $DetArchivePath -C $OfficialModelsDir
if (!$?) { Exit $LASTEXITCODE }
tar -xf $RecArchivePath -C $OfficialModelsDir
if (!$?) { Exit $LASTEXITCODE }

$OfficialDetDir = Join-Path $OfficialModelsDir "PP-OCRv5_server_det_infer"
$OfficialRecDir = Join-Path $OfficialModelsDir "PP-OCRv5_server_rec_infer"
Assert-OfficialPPOcrV5ModelDirectory -Role "detection" -ModelDir $OfficialDetDir
Assert-OfficialPPOcrV5ModelDirectory -Role "recognition" -ModelDir $OfficialRecDir

$PythonExe = Get-PythonExecutable
$PaddleOCRSourceDir = Expand-PaddleOCRSource -WorkDir $WorkDir
Install-PaddleExportDependencies -PythonExe $PythonExe -PaddleOCRSourceDir $PaddleOCRSourceDir

$DetPretrainedPath = Join-Path $WorkDir $PPOcrV5DetPretrainedName
$RecPretrainedPath = Join-Path $WorkDir $PPOcrV5RecPretrainedName
Download-File -Uri $PPOcrV5DetPretrainedUrl -OutFile $DetPretrainedPath -ExpectedSha256 $PPOcrV5DetPretrainedSha256
Download-File -Uri $PPOcrV5RecPretrainedUrl -OutFile $RecPretrainedPath -ExpectedBytes $PPOcrV5RecPretrainedExpectedBytes

$DetRuntimeDir = Join-Path $ModelsDir "PP-OCRv5_server_det_infer"
$RecRuntimeDir = Join-Path $ModelsDir "PP-OCRv5_server_rec_infer"
Export-PPOcrV5LegacyModel `
    -PythonExe $PythonExe `
    -PaddleOCRSourceDir $PaddleOCRSourceDir `
    -ConfigRelativePath $PPOcrV5DetConfig `
    -PretrainedPath $DetPretrainedPath `
    -OutputDir $DetRuntimeDir `
    -Role "detection"

Export-PPOcrV5LegacyModel `
    -PythonExe $PythonExe `
    -PaddleOCRSourceDir $PaddleOCRSourceDir `
    -ConfigRelativePath $PPOcrV5RecConfig `
    -PretrainedPath $RecPretrainedPath `
    -OutputDir $RecRuntimeDir `
    -Role "recognition"

Download-File -Uri $PPOcrV5DictUrl -OutFile (Join-Path $ModelsDir "ppocrv5_dict.txt")

@(
    "# Aegisub default OCR model combo:",
    "# PP-OCRv5_server_det + PP-OCRv5_server_rec",
    "# The bundled PaddleOCR-json runtime loads the legacy-exported folders below.",
    "",
    "det_model_dir models/PP-OCRv5_server_det_infer",
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

PaddleOCR-json is built from PaddleOCR C++/Paddle Inference. Aegisub keeps this
sidecar runtime and targets this default model combo:

  Detection:   PP-OCRv5_server_det
  Recognition: PP-OCRv5_server_rec
  Dictionary:  ppocrv5_dict.txt

The official PP-OCRv5 inference archives contain:

  inference.json
  inference.yml
  inference.pdiparams

They do not contain inference.pdmodel. PaddleOCR-json $RuntimeVersion cannot
load those Paddle 3/PIR folders directly, so the Windows packaging step exports
the official PP-OCRv5 pretrained weights with PaddleOCR $PaddleOCRSourceVersion
and Global.export_with_pir=False. The packaged model directories are legacy
Paddle Inference folders containing:

  inference.pdmodel
  inference.pdiparams
  inference.yml

The generated config is:

  ocr/models/config_ppocrv5.txt

It points to:

  det_model_dir models/PP-OCRv5_server_det_infer
  cls_model_dir models/ch_ppocr_mobile_v2.0_cls_infer
  rec_model_dir models/PP-OCRv5_server_rec_infer
  rec_char_dict_path models/ppocrv5_dict.txt

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
        throw "Required OCR file missing after extraction/export: $file"
    }
}

foreach ($configName in $RequiredConfigNames) {
    $configPath = Join-Path $ModelsDir $configName
    Assert-OcrModelConfig -ConfigPath $configPath -ModelsDir $ModelsDir
    if ($configName -eq "config_ppocrv5.txt") {
        Assert-PPOcrV5DefaultConfig -ConfigPath $configPath -ModelsDir $ModelsDir
    }
}
Assert-NoUnusedOcrModelAssets -ModelsDir $ModelsDir

Test-PPOcrV5RuntimeLoad `
    -ExePath (Join-Path $BinDir "PaddleOCR-json.exe") `
    -ModelsDir $ModelsDir `
    -ConfigPath (Join-Path $ModelsDir "config_ppocrv5.txt") `
    -WorkDir $WorkDir

$BundleVersion | Set-Content -LiteralPath $RuntimeMarker -Encoding ASCII

Write-Host "PaddleOCR runtime prepared at $DestinationDir"
