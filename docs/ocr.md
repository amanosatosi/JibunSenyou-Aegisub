# Image to Text OCR

Aegisub Windows release artifacts include offline OCR support through
PaddleOCR-json. Users do not need Python, PaddlePaddle, PaddleOCR, model files,
or extra DLL setup.

## How to use

1. Open Aegisub.
2. Open a video, or prepare an image file.
3. Choose `Video > Image to Text (OCR)...`.
4. Select `Current video frame` or `Image file`.
5. Click `Recognize`.
6. Use `Insert`, `Copy`, or `Replace Line...`.

`Replace Line...` always asks for confirmation before replacing the current
subtitle line text.

## Runtime layout

Windows portable zips and installers contain:

```text
ocr/
  OCR_RUNTIME_VERSION.txt
  bin/
    PaddleOCR-json.exe
    runtime DLLs
  models/
    config_japan.txt
    config_en.txt
    config_chinese.txt
    config_chinese_cht.txt
    config_korean.txt
    bundled PaddleOCR-json model folders and dictionaries
  licenses/
    THIRD_PARTY_OCR.txt
    copied upstream license/readme files when present
```

The OCR dialog uses this folder next to `aegisub.exe`. If any required file is
deleted, Aegisub reports the exact missing path.

## Bundled runtime

The current Windows bundle uses:

- PaddleOCR-json `v1.4.1-dev.1`
- Download:
  `https://github.com/hiroi-sora/PaddleOCR-json/releases/download/v1.4.1-dev/PaddleOCR-json_v1.4.1_dev.1_windows_x86-64_cpu_mkl.7z`
- SHA256:
  `46c3c82e889e5ed0c8a066ed3a089cd200d1e482823601bab23f5e41d137700f`

PaddleOCR-json is a native PaddleOCR C++/Paddle Inference runtime. Aegisub uses
the model presets bundled in the `v1.4.1-dev.1` release because those presets
match the runtime's expected Paddle Inference model layout:

- Japanese: `models/config_japan.txt`
- English: `models/config_en.txt`
- Simplified Chinese: `models/config_chinese.txt`
- Traditional Chinese: `models/config_chinese_cht.txt`
- Korean: `models/config_korean.txt`

Each selected detection, classification, and recognition model folder must
contain both `inference.pdmodel` and `inference.pdiparams`, and the selected
recognition dictionary must exist.

PP-OCRv5 is not packaged with this PaddleOCR-json runtime. PaddleOCR-json
`v1.4.1-dev.1` documents support for PP-OCR V2 through V4 model folders. The
official PP-OCRv5 HuggingFace folders used by the previous downloader contain
`inference.json`, `inference.yml`, and `inference.pdiparams`, but do not contain
`inference.pdmodel`. PaddleOCR-json still attempts to open
`inference.pdmodel`, so those PP-OCRv5 folders fail to load in this integration.

This approach was chosen over linking Paddle Inference directly into Aegisub
because it keeps the Aegisub build small and avoids adding Paddle's C++ ABI,
DLL, and model-loading surface to the main application. The runtime remains
offline and bundled in the release artifacts.

## Build and packaging

Windows CI explicitly passes:

```text
-Dpaddleocr=enabled
```

The Meson option is:

```text
option('paddleocr', type: 'feature', value: 'auto')
```

On Windows, `auto` and `enabled` compile the OCR UI/runtime checks. On
non-Windows, `enabled` fails during configure because the bundled runtime is
currently Windows-only; `auto` leaves bundled OCR disabled while keeping normal
Linux builds intact.

The pinned runtime is downloaded by:

```powershell
tools\ocr\download_paddleocr_windows.ps1 -DestinationDir build\installer-deps\ocr
```

The installer and portable packaging scripts copy `build\installer-deps\ocr`
into the artifact as `ocr` next to `aegisub.exe`.

## Updating OCR runtime versions

1. Update `$ReleaseTag`, `$RuntimeVersion`, `$ArchiveName`, `$ArchiveUrl`, and
   `$ArchiveSha256` in `tools/ocr/download_paddleocr_windows.ps1`.
2. If changing models, use PaddleOCR-json-compatible Paddle Inference folders
   that contain `inference.pdmodel` and `inference.pdiparams`.
3. Update the cache key in `.github/workflows/ci.yml`.
4. Verify the prepared OCR folder contains `OCR_RUNTIME_VERSION.txt`,
   `PaddleOCR-json.exe`,
   `models/config_japan.txt`,
   `models/config_en.txt`,
   `models/config_chinese.txt`,
   `models/config_chinese_cht.txt`, and
   `models/config_korean.txt`.
   The downloader validates the model directories and dictionaries referenced
   by each config.
5. Update this document with the new versions and hashes.
6. Let GitHub Actions build the Windows installer and portable artifacts.
