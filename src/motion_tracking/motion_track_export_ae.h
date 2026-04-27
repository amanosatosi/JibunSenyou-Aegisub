#pragma once

#include "motion_track_types.h"

#include <string>

namespace motion_tracking {

std::string ExportAfterEffectsKeyframes(MotionTrackResult const& result, int first_frame = -1);

} // namespace motion_tracking
