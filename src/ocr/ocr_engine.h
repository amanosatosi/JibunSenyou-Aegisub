// Copyright (c) 2026, Aegisub Project
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

#pragma once

#include "ocr_result.h"

#include <libaegisub/fs_fwd.h>

#include <wx/string.h>

namespace ocr {

class OCREngine {
	agi::fs::path runtime_dir;
	agi::fs::path executable;
	agi::fs::path models_dir;

	agi::fs::path ConfigPath(std::string const& language) const;

public:
	OCREngine();

	bool IsAvailable(OCROptions const& options) const;
	wxString GetDiagnostic(OCROptions const& options) const;
	OCRResult RecognizeImage(agi::fs::path const& image_path, OCROptions const& options) const;
};

} // namespace ocr
