#pragma once

#include "motion_track_types.h"

#include <optional>
#include <string>
#include <vector>

namespace motion_tracking {

struct MotionTrackPoint {
	double x = 0.0;
	double y = 0.0;
};

struct MotionTrackSegmentSample {
	int frame = 0;
	MotionTrackMarker marker;
	double confidence = 0.0;
	MotionTrackState state = MotionTrackState::Untracked;
};

struct MotionTrackSegment {
	int start_frame = 0;
	int end_frame = 0;
	int anchor_frame = -1;
	int target_frame = -1;
	int direction = 1;
	MotionTrackMarker tracker_box_at_start;
	std::vector<MotionTrackSegmentSample> tracked_center_by_frame;
	MotionTrackPoint accumulated_offset_at_start;
	bool enabled = true;
	bool end_frame_manual = false;
	std::string name;
};

void UpsertSegmentSample(MotionTrackSegment& segment, MotionTrackSegmentSample sample);
void TrimSegmentToEnd(MotionTrackSegment& segment);
void RecalculateSegmentAccumulatedOffsets(std::vector<MotionTrackSegment>& segments);

std::optional<MotionTrackPoint> GetStitchedOffsetAtFrame(std::vector<MotionTrackSegment> const& segments, int frame);

MotionTrackResult BuildStitchedMotionResult(
	MotionTrackResult const& metadata,
	std::vector<MotionTrackSegment> const& segments);

} // namespace motion_tracking
