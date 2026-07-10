// Copyright (c) 2026

#include "ass_font_tag_selection.h"

AssFontNameValueRange FindAssFontNameValueAt(std::string const& text, int position) {
	if (position < 0 || position >= static_cast<int>(text.size()))
		return {};

	bool in_override = false;
	for (int i = 0; i < static_cast<int>(text.size()); ++i) {
		if (!in_override) {
			if (text[i] == '{')
				in_override = true;
			continue;
		}

		if (text[i] == '}') {
			in_override = false;
			continue;
		}

		if (text[i] != '\\' || i + 2 >= static_cast<int>(text.size()) ||
			text[i + 1] != 'f' || text[i + 2] != 'n')
			continue;

		AssFontNameValueRange range;
		range.start = i + 3;
		range.end = range.start;
		while (range.end < static_cast<int>(text.size()) &&
			text[range.end] != '\\' && text[range.end] != '}')
			++range.end;

		if (range.Contains(position))
			return range;

		// Let the next iteration process a following tag or closing brace.
		i = range.end - 1;
	}

	return {};
}
