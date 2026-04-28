#pragma once

#include "motion_track_types.h"

#include <string>
#include <vector>

namespace motion_tracking {

struct MotionTrackExportSettings {
	MotionTrackSmoothing smoothing = MotionTrackSmoothing::Medium;
	bool preserve_endpoints = true;
	double position_deadzone = 0.25;
	double scale_deadzone = 0.0015;
	double rotation_deadzone = 0.10;
};

std::vector<MotionTrackFrame> StabilizeMotionTrackFrames(
	MotionTrackResult const& result,
	MotionTrackExportSettings settings = MotionTrackExportSettings());

std::string ExportAfterEffectsKeyframes(
	MotionTrackResult const& result,
	int first_frame = -1,
	MotionTrackExportSettings settings = MotionTrackExportSettings());

} // namespace motion_tracking
