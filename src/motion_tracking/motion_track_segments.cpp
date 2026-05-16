#include "motion_track_segments.h"

#include <algorithm>
#include <map>

namespace motion_tracking {

namespace {
MotionTrackPoint Center(MotionTrackMarker const& marker) {
	return {marker.cx, marker.cy};
}

int AnchorFrame(MotionTrackSegment const& segment) {
	return segment.anchor_frame >= 0 ? segment.anchor_frame : segment.start_frame;
}

int TargetFrame(MotionTrackSegment const& segment) {
	return segment.target_frame >= 0 ? segment.target_frame : segment.end_frame;
}

int Direction(MotionTrackSegment const& segment) {
	if (segment.direction < 0)
		return -1;
	if (segment.direction > 0)
		return 1;
	return TargetFrame(segment) < AnchorFrame(segment) ? -1 : 1;
}

void UpdateChronologicalBounds(MotionTrackSegment& segment) {
	int anchor = AnchorFrame(segment);
	int target = TargetFrame(segment);
	segment.start_frame = std::min(anchor, target);
	segment.end_frame = std::max(anchor, target);
	segment.direction = target < anchor ? -1 : 1;
}

MotionTrackPoint LocalMotion(MotionTrackSegment const& segment, MotionTrackMarker const& marker) {
	return {
		marker.cx - segment.tracker_box_at_start.cx,
		marker.cy - segment.tracker_box_at_start.cy
	};
}

MotionTrackPoint Add(MotionTrackPoint a, MotionTrackPoint b) {
	return {a.x + b.x, a.y + b.y};
}

bool OwnsFrame(MotionTrackSegment const& segment, int frame) {
	return segment.enabled && frame >= segment.start_frame && frame <= segment.end_frame;
}

std::vector<size_t> SortedEnabledSegmentIndices(std::vector<MotionTrackSegment> const& segments) {
	std::vector<size_t> indices;
	for (size_t i = 0; i < segments.size(); ++i) {
		if (segments[i].enabled)
			indices.push_back(i);
	}
	return indices;
}

MotionTrackSegmentSample const* FindSample(MotionTrackSegment const& segment, int frame) {
	auto it = std::find_if(segment.tracked_center_by_frame.begin(), segment.tracked_center_by_frame.end(), [=](auto const& sample) {
		return sample.frame == frame && sample.state != MotionTrackState::Lost;
	});
	return it == segment.tracked_center_by_frame.end() ? nullptr : &*it;
}

MotionTrackSegmentSample const* LastUsableSample(MotionTrackSegment const& segment) {
	MotionTrackSegmentSample const* best = nullptr;
	int anchor = AnchorFrame(segment);
	int direction = Direction(segment);
	for (auto const& sample : segment.tracked_center_by_frame) {
		if (sample.state == MotionTrackState::Lost)
			continue;
		if (sample.frame < segment.start_frame || sample.frame > segment.end_frame)
			continue;
		if (!best || (sample.frame - anchor) * direction > (best->frame - anchor) * direction)
			best = &sample;
	}
	return best;
}

MotionTrackPoint SegmentOffsetAtSample(MotionTrackSegment const& segment, MotionTrackSegmentSample const& sample) {
	return Add(segment.accumulated_offset_at_start, LocalMotion(segment, sample.marker));
}
}

void UpsertSegmentSample(MotionTrackSegment& segment, MotionTrackSegmentSample sample) {
	if (segment.anchor_frame < 0) {
		segment.anchor_frame = sample.frame;
		segment.target_frame = sample.frame;
		segment.tracker_box_at_start = sample.marker;
		UpdateChronologicalBounds(segment);
	}

	auto it = std::find_if(segment.tracked_center_by_frame.begin(), segment.tracked_center_by_frame.end(), [=](auto const& existing) {
		return existing.frame == sample.frame;
	});
	if (it == segment.tracked_center_by_frame.end())
		segment.tracked_center_by_frame.push_back(sample);
	else
		*it = sample;

	std::sort(segment.tracked_center_by_frame.begin(), segment.tracked_center_by_frame.end(), [](auto const& a, auto const& b) {
		return a.frame < b.frame;
	});

	if (sample.frame == AnchorFrame(segment))
		segment.tracker_box_at_start = sample.marker;
	if (!segment.end_frame_manual && sample.frame != AnchorFrame(segment)) {
		if (Direction(segment) < 0) {
			if (segment.target_frame < 0 || sample.frame < segment.target_frame)
				segment.target_frame = sample.frame;
		}
		else if (sample.frame > TargetFrame(segment)) {
			segment.target_frame = sample.frame;
		}
	}
	UpdateChronologicalBounds(segment);
}

void TrimSegmentToEnd(MotionTrackSegment& segment) {
	segment.tracked_center_by_frame.erase(
		std::remove_if(segment.tracked_center_by_frame.begin(), segment.tracked_center_by_frame.end(), [&](auto const& sample) {
			return sample.frame < segment.start_frame || sample.frame > segment.end_frame;
		}),
		segment.tracked_center_by_frame.end());
}

void RecalculateSegmentAccumulatedOffsets(std::vector<MotionTrackSegment>& segments) {
	auto indices = SortedEnabledSegmentIndices(segments);
	MotionTrackPoint next_accumulated_offset;
	std::vector<size_t> processed_indices;

	for (size_t index : indices) {
		auto& segment = segments[index];
		std::optional<MotionTrackPoint> anchor_offset;
		int anchor = AnchorFrame(segment);
		for (size_t previous_index : processed_indices) {
			auto const& previous = segments[previous_index];
			if (!OwnsFrame(previous, anchor))
				continue;
			if (auto sample = FindSample(previous, anchor))
				anchor_offset = SegmentOffsetAtSample(previous, *sample);
		}

		segment.accumulated_offset_at_start = anchor_offset.value_or(next_accumulated_offset);

		if (auto sample = LastUsableSample(segment))
			next_accumulated_offset = SegmentOffsetAtSample(segment, *sample);
		processed_indices.push_back(index);
	}
}

std::optional<MotionTrackPoint> GetStitchedOffsetAtFrame(std::vector<MotionTrackSegment> const& segments, int frame) {
	std::optional<MotionTrackPoint> offset;
	for (size_t index : SortedEnabledSegmentIndices(segments)) {
		auto const& segment = segments[index];
		if (!OwnsFrame(segment, frame))
			continue;
		auto sample = FindSample(segment, frame);
		if (!sample)
			continue;
		offset = SegmentOffsetAtSample(segment, *sample);
	}
	return offset;
}

MotionTrackResult BuildStitchedMotionResult(MotionTrackResult const& metadata, std::vector<MotionTrackSegment> const& source_segments) {
	MotionTrackResult result = metadata;
	result.frames.clear();

	auto segments = source_segments;
	RecalculateSegmentAccumulatedOffsets(segments);
	auto indices = SortedEnabledSegmentIndices(segments);
	if (indices.empty())
		return result;

	auto const& origin_marker = segments[indices.front()].tracker_box_at_start;
	MotionTrackPoint output_origin = Center(origin_marker);
	double origin_size = std::max(4.0, origin_marker.size);

	std::map<int, MotionTrackFrame> frames_by_number;
	for (size_t index : indices) {
		auto const& segment = segments[index];
		for (auto const& sample : segment.tracked_center_by_frame) {
			if (sample.state == MotionTrackState::Lost)
				continue;
			if (sample.frame < segment.start_frame || sample.frame > segment.end_frame)
				continue;

			auto offset = SegmentOffsetAtSample(segment, sample);
			double x = output_origin.x + offset.x;
			double y = output_origin.y + offset.y;
			double scale = sample.marker.size / origin_size;
			frames_by_number[sample.frame] = MotionTrackFrame{
				sample.frame,
				x,
				y,
				x,
				y,
				scale,
				scale,
				sample.marker.rotation_deg,
				sample.confidence,
				sample.state
			};
		}
	}

	result.frames.reserve(frames_by_number.size());
	for (auto const& frame : frames_by_number)
		result.frames.push_back(frame.second);
	return result;
}

} // namespace motion_tracking
