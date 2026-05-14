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
    config_ppocrv5.txt
    config_korean.txt
    PP-OCRv5_mobile_det_infer/
    PP-OCRv5_server_rec_infer/
    ppocrv5_dict.txt
    bundled PaddleOCR-json Korean model folders
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
this default model combo for Japanese, English, Simplified Chinese, and
Traditional Chinese:

- Detection: `PP-OCRv5_mobile_det`
- Recognition: `PP-OCRv5_server_rec`
- Dictionary: `ppocrv5_dict.txt`

The generated config is `models/config_ppocrv5.txt`. Korean remains exposed
through the Korean config bundled by PaddleOCR-json.

The current PaddleOCR-json executable still attempts to open
`inference.pdmodel` from every configured detection, classification, and
recognition model folder. The official PP-OCRv5 archives currently hosted by
Paddle contain `inference.json`, `inference.yml`, and `inference.pdiparams`, but
do not contain `inference.pdmodel`. Packaging validation therefore fails unless
the selected PP-OCRv5 model folders also contain `inference.pdmodel`.

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
2. If changing PP-OCRv5 model archives, use PaddleOCR-json-compatible Paddle
   Inference folders that contain `inference.pdmodel` and
   `inference.pdiparams`.
3. Update the cache key in `.github/workflows/ci.yml`.
4. Verify the prepared OCR folder contains `OCR_RUNTIME_VERSION.txt`,
   `PaddleOCR-json.exe`,
   `models/config_ppocrv5.txt`,
   `models/config_korean.txt`,
   `models/PP-OCRv5_mobile_det_infer/inference.pdmodel`,
   `models/PP-OCRv5_mobile_det_infer/inference.pdiparams`,
   `models/PP-OCRv5_server_rec_infer/inference.pdmodel`,
   `models/PP-OCRv5_server_rec_infer/inference.pdiparams`, and
   `models/ppocrv5_dict.txt`.
   The downloader validates the model directories and dictionaries referenced
   by each config.
5. Update this document with the new versions and hashes.
6. Let GitHub Actions build the Windows installer and portable artifacts.
