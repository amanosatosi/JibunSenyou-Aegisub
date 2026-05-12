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

#include <string>
#include <utility>
#include <vector>

namespace ocr {

struct OCRLine {
	std::string text;
	double confidence = 0.0;
	std::vector<std::pair<int, int>> box;
};

struct OCRResult {
	bool ok = false;
	int code = 0;
	std::string text;
	std::string diagnostic;
	std::vector<OCRLine> lines;
};

struct OCROptions {
	bool keep_line_breaks = true;
	std::string language = "japanese";
};

std::string NormalizeText(std::vector<OCRLine> const& lines, bool keep_line_breaks);
OCRResult ParsePaddleOCRJson(std::string const& json_text, OCROptions const& options);

} // namespace ocr
