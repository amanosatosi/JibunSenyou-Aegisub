#pragma once

#include "motion_track_types.h"

#include <string>
#include <utility>
#include <vector>

namespace motion_tracking {

struct MotionTrackExportSettings {
	MotionTrackCleanup cleanup = MotionTrackCleanup::Off;
	double cleanup_threshold = 0.5;
	std::vector<std::pair<int, int>> locked_ranges;
};

std::vector<MotionTrackFrame> StabilizeMotionTrackFrames(
	MotionTrackResult const& result,
	MotionTrackExportSettings settings = MotionTrackExportSettings());

std::string ExportAfterEffectsKeyframes(
	MotionTrackResult const& result,
	int first_frame = -1,
	MotionTrackExportSettings settings = MotionTrackExportSettings());

} // namespace motion_tracking
