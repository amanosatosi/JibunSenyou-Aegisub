// Copyright (c) 2014, Thomas Goyne <plorkyeran@aegisub.org>
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#include "ass_file.h"

#include "ass_info.h"

#include <boost/algorithm/string/predicate.hpp>
#include <cstdlib>

std::string AssFile::GetScriptInfo(std::string const& key) const {
	for (auto const& info : Info) {
		if (boost::iequals(key, info.Key()))
			return info.Value();
	}

	return "";
}

int AssFile::GetScriptInfoAsInt(std::string const& key) const {
	return atoi(GetScriptInfo(key).c_str());
}

void AssFile::SetScriptInfo(std::string const& key, std::string const& value) {
	for (auto it = Info.begin(); it != Info.end(); ++it) {
		if (boost::iequals(key, it->Key())) {
			if (value.empty())
				Info.erase(it);
			else
				it->SetValue(value);
			return;
		}
	}

	if (!value.empty())
		Info.emplace_back(key, value);
}

void AssFile::GetResolution(int &sw, int &sh) const {
	sw = GetScriptInfoAsInt("PlayResX");
	sh = GetScriptInfoAsInt("PlayResY");

	// Gabest logic: default is 384x288, assume 1280x1024 if either height or
	// width are that, otherwise assume 4:3 if only height or width are set.
	if (sw == 0 && sh == 0) {
		sw = 384;
		sh = 288;
	}
	else if (sw == 0)
		sw = sh == 1024 ? 1280 : sh * 4 / 3;
	else if (sh == 0)
		sh = sw == 1280 ? 1024 : sw * 3 / 4;
}

void AssFile::GetLayoutResolution(int &lw, int &lh) const {
	lw = GetScriptInfoAsInt("LayoutResX");
	lh = GetScriptInfoAsInt("LayoutResY");
}
