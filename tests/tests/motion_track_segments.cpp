#include <main.h>

#include "motion_tracking/motion_track_segments.h"

using namespace motion_tracking;

namespace {
MotionTrackMarker Marker(double x, double y) {
	MotionTrackMarker marker;
	marker.cx = x;
	marker.cy = y;
	marker.size = 40.0;
	marker.search_size = 120.0;
	return marker;
}

MotionTrackSegment Segment(int start_frame, int end_frame, double start_x, double start_y) {
	MotionTrackSegment segment;
	segment.start_frame = start_frame;
	segment.end_frame = end_frame;
	segment.tracker_box_at_start = Marker(start_x, start_y);
	segment.name = "Segment";
	UpsertSegmentSample(segment, {start_frame, segment.tracker_box_at_start, 1.0, MotionTrackState::Untracked});
	return segment;
}

void AddSample(MotionTrackSegment& segment, int frame, double x, double y) {
	UpsertSegmentSample(segment, {frame, Marker(x, y), 1.0, MotionTrackState::Tracked});
}

MotionTrackResult Metadata() {
	MotionTrackResult result;
	result.fps = 24.0;
	result.source_width = 1920;
	result.source_height = 1080;
	return result;
}
}

TEST(motion_track_segments, one_segment) {
	auto first = Segment(0, 100, 100.0, 100.0);
	AddSample(first, 100, 100.0, 300.0);
	std::vector<MotionTrackSegment> segments{first};
	RecalculateSegmentAccumulatedOffsets(segments);

	auto offset = GetStitchedOffsetAtFrame(segments, 100);
	ASSERT_TRUE(offset);
	EXPECT_DOUBLE_EQ(0.0, offset->x);
	EXPECT_DOUBLE_EQ(200.0, offset->y);

	auto result = BuildStitchedMotionResult(Metadata(), segments);
	ASSERT_EQ(2u, result.frames.size());
	EXPECT_DOUBLE_EQ(100.0, result.frames.back().x);
	EXPECT_DOUBLE_EQ(300.0, result.frames.back().y);
}

TEST(motion_track_segments, two_connected_segments) {
	auto first = Segment(0, 100, 100.0, 100.0);
	AddSample(first, 100, 100.0, 300.0);
	auto second = Segment(100, 200, 500.0, 80.0);
	AddSample(second, 200, 500.0, 180.0);
	std::vector<MotionTrackSegment> segments{first, second};
	RecalculateSegmentAccumulatedOffsets(segments);

	EXPECT_DOUBLE_EQ(0.0, segments[1].accumulated_offset_at_start.x);
	EXPECT_DOUBLE_EQ(200.0, segments[1].accumulated_offset_at_start.y);
	auto offset = GetStitchedOffsetAtFrame(segments, 200);
	ASSERT_TRUE(offset);
	EXPECT_DOUBLE_EQ(0.0, offset->x);
	EXPECT_DOUBLE_EQ(300.0, offset->y);
}

TEST(motion_track_segments, three_connected_segments) {
	auto first = Segment(0, 100, 100.0, 100.0);
	AddSample(first, 100, 100.0, 300.0);
	auto second = Segment(100, 200, 500.0, 80.0);
	AddSample(second, 200, 500.0, 180.0);
	auto third = Segment(200, 250, 50.0, 700.0);
	AddSample(third, 250, 50.0, 750.0);
	std::vector<MotionTrackSegment> segments{first, second, third};
	RecalculateSegmentAccumulatedOffsets(segments);

	auto offset = GetStitchedOffsetAtFrame(segments, 250);
	ASSERT_TRUE(offset);
	EXPECT_DOUBLE_EQ(0.0, offset->x);
	EXPECT_DOUBLE_EQ(350.0, offset->y);
}

TEST(motion_track_segments, handoff_screen_position_does_not_affect_motion) {
	auto first = Segment(0, 100, 100.0, 100.0);
	AddSample(first, 100, 100.0, 300.0);
	auto second = Segment(100, 200, 900.0, 20.0);
	AddSample(second, 200, 900.0, 120.0);
	std::vector<MotionTrackSegment> segments{first, second};
	RecalculateSegmentAccumulatedOffsets(segments);

	auto result = BuildStitchedMotionResult(Metadata(), segments);
	ASSERT_EQ(3u, result.frames.size());
	EXPECT_EQ(200, result.frames.back().frame);
	EXPECT_DOUBLE_EQ(100.0, result.frames.back().x);
	EXPECT_DOUBLE_EQ(400.0, result.frames.back().y);
}

TEST(motion_track_segments, boundary_edit_recalculates_later_offsets) {
	auto first = Segment(0, 50, 100.0, 100.0);
	AddSample(first, 50, 100.0, 200.0);
	AddSample(first, 100, 100.0, 300.0);
	first.end_frame = 50;
	first.end_frame_manual = true;
	TrimSegmentToEnd(first);

	auto second = Segment(100, 200, 500.0, 80.0);
	AddSample(second, 200, 500.0, 180.0);
	std::vector<MotionTrackSegment> segments{first, second};
	RecalculateSegmentAccumulatedOffsets(segments);

	EXPECT_DOUBLE_EQ(100.0, segments[1].accumulated_offset_at_start.y);
	auto offset = GetStitchedOffsetAtFrame(segments, 200);
	ASSERT_TRUE(offset);
	EXPECT_DOUBLE_EQ(200.0, offset->y);
}

TEST(motion_track_segments, disabled_segments_are_skipped) {
	auto first = Segment(0, 100, 100.0, 100.0);
	AddSample(first, 100, 100.0, 300.0);
	auto disabled = Segment(100, 200, 500.0, 80.0);
	AddSample(disabled, 200, 500.0, 180.0);
	disabled.enabled = false;
	auto third = Segment(200, 250, 20.0, 20.0);
	AddSample(third, 250, 20.0, 70.0);
	std::vector<MotionTrackSegment> segments{first, disabled, third};
	RecalculateSegmentAccumulatedOffsets(segments);

	EXPECT_DOUBLE_EQ(200.0, segments[2].accumulated_offset_at_start.y);
	auto offset = GetStitchedOffsetAtFrame(segments, 250);
	ASSERT_TRUE(offset);
	EXPECT_DOUBLE_EQ(250.0, offset->y);
}
