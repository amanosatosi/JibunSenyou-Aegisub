#pragma once

#include "motion_track_types.h"

#include <string>

namespace motion_tracking {

struct MotionTrackExportSettings {
	MotionTrackSmoothing smoothing = MotionTrackSmoothing::Medium;
	double position_deadzone = 0.25;
	double scale_deadzone = 0.0015;
	double rotation_deadzone = 0.10;
};

std::string ExportAfterEffectsKeyframes(
	MotionTrackResult const& result,
	int first_frame = -1,
	MotionTrackExportSettings settings = MotionTrackExportSettings());

} // namespace motion_tracking
