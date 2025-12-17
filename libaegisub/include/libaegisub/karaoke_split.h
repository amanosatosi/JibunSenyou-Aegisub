// Copyright (c) 2025
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

#include <cstddef>
#include <cstdint>
#include <utility>

namespace agi {
/// Split a karaoke duration in centiseconds proportionally by character count.
///
/// Uses integer math to implement:
///   D1 = round(D * left_chars / total_chars)
///   D2 = D - D1
///
/// This guarantees D1 + D2 == D.
inline std::pair<int, int> SplitKaraokeDurationCs(int total_cs, size_t left_chars, size_t total_chars) {
	if (total_cs < 0) total_cs = 0;
	if (total_chars == 0) return {0, total_cs};

	if (left_chars > total_chars) left_chars = total_chars;

	int64_t num = static_cast<int64_t>(total_cs) * static_cast<int64_t>(left_chars);
	int64_t den = static_cast<int64_t>(total_chars);
	int64_t d1 = (num + den / 2) / den;

	if (d1 < 0) d1 = 0;
	if (d1 > total_cs) d1 = total_cs;

	return {static_cast<int>(d1), total_cs - static_cast<int>(d1)};
}
}

