// Copyright (c) 2026

// Test-only AssFile lifetime/commit support. Script-info and effective
// resolution methods come from the production ass_file_script_info.cpp.

#include "../../src/ass_file.h"

#include "../../src/ass_attachment.h"
#include "../../src/ass_dialogue.h"
#include "../../src/ass_info.h"
#include "../../src/ass_style.h"

#include <boost/algorithm/string/predicate.hpp>

AssFile::AssFile() { }

AssFile::~AssFile() {
	Styles.clear_and_dispose([](AssStyle *style) { delete style; });
	Events.clear_and_dispose([](AssDialogue *line) { delete line; });
}

AssStyle *AssFile::GetStyle(std::string const& name) {
	for (auto& style : Styles) {
		if (boost::iequals(style.name, name))
			return &style;
	}
	return nullptr;
}

int AssFile::Commit(wxString const&, int, int amend_id, AssDialogue *) {
	return amend_id;
}
