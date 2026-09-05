// Copyright (c) 2026

#pragma once

#include <libaegisub/fs_fwd.h>

namespace agi { class Path; }
struct ProjectProperties;

namespace project {
	/// Store only the currently loaded video's project-relative path.
	void UpdateVideoRelativePath(ProjectProperties& properties, agi::Path const& path_helper,
		agi::fs::path const& video_file);
}
