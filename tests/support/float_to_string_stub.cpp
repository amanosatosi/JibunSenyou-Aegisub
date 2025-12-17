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

// Test-only stub to satisfy linking Aegisub sources into the gtest runner.
// The application normally gets this from `src/utils.cpp`, but pulling that
// into tests would also require `config::opt` from `src/main.cpp`.

#include "../../src/utils.h"

#include <iomanip>
#include <sstream>

std::string float_to_string(double val, int precision) {
	std::ostringstream oss;
	oss.setf(std::ios::fixed);
	oss << std::setprecision(precision) << val;
	auto s = oss.str();

	size_t pos = s.find_last_not_of('0');
	if (pos == std::string::npos)
		return "0";

	auto dot = s.find('.');
	if (dot != std::string::npos) {
		if (pos == dot)
			--pos;
		s.erase(pos + 1);
	}

	return s;
}

