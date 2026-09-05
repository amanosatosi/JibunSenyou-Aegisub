// Copyright (c) 2026

#include "project_properties.h"

#include "ass_file.h"

#include <libaegisub/path.h>

namespace project {
	void UpdateVideoRelativePath(ProjectProperties& properties, agi::Path const& path_helper,
		agi::fs::path const& video_file) {
		properties.video_file = path_helper.MakeRelative(video_file, "?script").generic_string();
	}
}
