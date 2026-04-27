#pragma once

#include "motion_track_types.h"

namespace motion_tracking {

class MotionTrackEngine {
public:
	static bool IsAvailable();

	MotionTrackStepResult TrackFrame(
		MotionTrackImage const& from,
		MotionTrackImage const& to,
		MotionTrackMarker const& marker,
		int target_frame,
		MotionTrackMode mode,
		bool brightness_normalize) const;
};

} // namespace motion_tracking
