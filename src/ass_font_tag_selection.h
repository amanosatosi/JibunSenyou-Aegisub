// Copyright (c) 2026
// ASS-aware selection helpers for the subtitle edit control.

#pragma once

#include <string>

struct AssFontNameValueRange {
	int start = -1;
	int end = -1;

	explicit operator bool() const { return start >= 0 && end >= start; }
	bool Contains(int position) const { return position >= start && position < end; }
};

/// Return the byte range of the \fn value containing position. The value is
/// bounded by the next override tag, closing brace, or incomplete-text end.
AssFontNameValueRange FindAssFontNameValueAt(std::string const& text, int position);
