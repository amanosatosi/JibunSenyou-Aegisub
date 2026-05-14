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
      inference.pdmodel
      inference.pdiparams
      inference.yml
    PP-OCRv5_server_rec_infer/
      inference.pdmodel
      inference.pdiparams
      inference.yml
    ppocrv5_dict.txt
    bundled PaddleOCR-json Korean model folders
  licenses/
    THIRD_PARTY_OCR.txt
    copied upstream license/readme files when present
```

The OCR dialog uses this folder next to `aegisub.exe`. If any required file is
deleted, Aegisub reports the exact missing path and the files present in that
folder.

## Bundled runtime

The current Windows bundle uses:

- PaddleOCR-json `v1.4.1-dev.1`
- Download:
  `https://github.com/hiroi-sora/PaddleOCR-json/releases/download/v1.4.1-dev/PaddleOCR-json_v1.4.1_dev.1_windows_x86-64_cpu_mkl.7z`
- SHA256:
  `46c3c82e889e5ed0c8a066ed3a089cd200d1e482823601bab23f5e41d137700f`

PaddleOCR-json remains the only OCR runtime. Aegisub calls the external
sidecar, captures stdout and stderr separately, and parses the same Image2Text
JSON result contract.

The default model combo is:

- Detection: `PP-OCRv5_mobile_det`
- Recognition: `PP-OCRv5_server_rec`
- Dictionary: `ppocrv5_dict.txt`
- Config: `models/config_ppocrv5.txt`

Korean remains exposed through the Korean config bundled by PaddleOCR-json.

## PP-OCRv5 model format

The official PP-OCRv5 inference archives used as source inputs are:

- `PP-OCRv5_mobile_det_infer.tar`
- `PP-OCRv5_server_rec_infer.tar`

Those archives contain:

- `inference.json`
- `inference.yml`
- `inference.pdiparams`

They do not contain `inference.pdmodel`. That is expected for the official
Paddle 3/PIR PP-OCRv5 archives.

PaddleOCR-json `v1.4.1-dev.1` does not load those JSON/PIR folders directly. It
still expects legacy Paddle Inference model folders with `inference.pdmodel`
and `inference.pdiparams`. The Windows packaging script therefore keeps the
selected PP-OCRv5 combo, downloads the official PP-OCRv5 source archives for
validation, then exports the official PP-OCRv5 pretrained weights with
PaddleOCR `v3.0.0` using:

```text
Global.export_with_pir=False
FLAGS_enable_pir_api=0
```

The packaged folders are the legacy-exported PP-OCRv5 folders that
PaddleOCR-json can load. This is a packaging/export step only; it does not make
Python PaddleOCR the runtime backend.

## Config

`models/config_ppocrv5.txt` is generated with one setting per line:

```text
det_model_dir models/PP-OCRv5_mobile_det_infer
cls_model_dir models/ch_ppocr_mobile_v2.0_cls_infer
rec_model_dir models/PP-OCRv5_server_rec_infer
rec_char_dict_path models/ppocrv5_dict.txt
```

The detection and recognition paths point to the legacy-exported PP-OCRv5 model
directories. The classifier remains the bundled PaddleOCR-json compatible
classifier.

## Build and packaging

Windows CI explicitly passes:

```text
-Dpaddleocr=enabled
```

The pinned runtime and models are prepared by:

```powershell
tools\ocr\download_paddleocr_windows.ps1 -DestinationDir build\installer-deps\ocr
```

The script validates the official PP-OCRv5 source archives as JSON/PIR model
folders, exports legacy runtime folders, validates the final config paths, and
runs a PaddleOCR-json smoke check against `models/config_ppocrv5.txt`. Packaging
fails clearly if the issue is missing files, unsupported model format, a bad
config path, or runtime executable failure.

The installer and portable packaging scripts copy `build\installer-deps\ocr`
into the artifact as `ocr` next to `aegisub.exe`.

## Verifying PP-OCRv5 in an artifact

Check these files in the portable zip or installer output:

```text
ocr/OCR_RUNTIME_VERSION.txt
ocr/bin/PaddleOCR-json.exe
ocr/models/config_ppocrv5.txt
ocr/models/PP-OCRv5_mobile_det_infer/inference.pdmodel
ocr/models/PP-OCRv5_mobile_det_infer/inference.pdiparams
ocr/models/PP-OCRv5_server_rec_infer/inference.pdmodel
ocr/models/PP-OCRv5_server_rec_infer/inference.pdiparams
ocr/models/ppocrv5_dict.txt
```

`OCR_RUNTIME_VERSION.txt` should include
`ppocrv5-mobile-det-server-rec-legacy-export`. The config should reference
`PP-OCRv5_mobile_det_infer`, `PP-OCRv5_server_rec_infer`, and
`ppocrv5_dict.txt`.

If model loading fails at runtime, Aegisub reports a model/runtime error instead
of a JSON parse error. The debug details include the full PaddleOCR-json stdout
and stderr so missing model files, bad config paths, and runtime loader errors
can be diagnosed.

## Updating OCR runtime versions

1. Update `$ReleaseTag`, `$RuntimeVersion`, `$ArchiveName`, `$ArchiveUrl`, and
   `$ArchiveSha256` in `tools/ocr/download_paddleocr_windows.ps1`.
2. If changing the PP-OCRv5 source model combo, update the official inference
   archive URLs, pretrained weight URLs, PaddleOCR export config paths, and
   validation expectations in the same script.
3. Keep `PaddleOCR-json.exe` as the runtime backend unless the OCR architecture
   is intentionally redesigned.
4. Update the cache key in `.github/workflows/ci.yml`.
5. Let GitHub Actions build the Windows installer and portable artifacts.
