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
  bin/
    PaddleOCR-json.exe
    runtime DLLs
  models/
    config_*.txt
    PaddleOCR model folders
  licenses/
    THIRD_PARTY_OCR.txt
    copied upstream license/readme files when present
```

The OCR dialog uses this folder next to `aegisub.exe`. If any required file is
deleted, Aegisub reports the exact missing path.

## Bundled runtime

The current Windows bundle uses:

- PaddleOCR-json `v1.4.1`
- Download:
  `https://github.com/hiroi-sora/PaddleOCR-json/releases/download/v1.4.1/PaddleOCR-json_v1.4.1_windows_x64.7z`
- SHA256:
  `c0912a70acb1f8f18fafe1f438a2935292a6ec7e2859156fa48a33e91358d71d`

PaddleOCR-json is a native PaddleOCR C++/Paddle Inference runtime. The upstream
release notes state that this package includes Simplified Chinese, Traditional
Chinese, English, Japanese, Korean, and Russian model sets. Aegisub exposes the
Japanese, English, Simplified Chinese, Traditional Chinese, and Korean configs.

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

1. Update `$Version`, `$ArchiveName`, `$ArchiveUrl`, and `$ArchiveSha256` in
   `tools/ocr/download_paddleocr_windows.ps1`.
2. Update the cache key in `.github/workflows/ci.yml`.
3. Verify the extracted archive still contains `PaddleOCR-json.exe`,
   `models/config_en.txt`, `models/config_japan.txt`, and
   `models/config_chinese.txt`.
4. Update this document with the new version and hash.
5. Let GitHub Actions build the Windows installer and portable artifacts.
