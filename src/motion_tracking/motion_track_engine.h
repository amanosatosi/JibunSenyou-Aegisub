#pragma once

#include "motion_track_types.h"

namespace motion_tracking {

class MotionTrackEngine {
public:
	static bool IsAvailable();

	MotionTrackStepResult TrackFrame(
		MotionTrackImage const& from,
		MotionTrackImage const& to,
		MotionTrackMarker const& pattern_marker,
		MotionTrackMarker const& search_marker,
		int target_frame,
		MotionTrackSettings const& settings) const;
};

} // namespace motion_tracking
