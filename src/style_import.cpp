// Copyright (c) 2026

#include "style_import.h"

#include "ass_file.h"
#include "ass_style.h"

bool ImportStyle(AssFile& destination, AssStyle const& source, bool replace_existing) {
	if (auto existing = destination.GetStyle(source.name)) {
		if (!replace_existing)
			return false;
		*existing = source;
	}
	else {
		destination.Styles.push_back(*new AssStyle(source));
	}
	return true;
}
