#include "motion_track_export_ae.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace motion_tracking {

namespace {
std::vector<MotionTrackFrame> SortedFrames(MotionTrackResult const& result) {
	auto frames = result.frames;
	std::sort(frames.begin(), frames.end(), [](MotionTrackFrame const& a, MotionTrackFrame const& b) {
		return a.frame < b.frame;
	});
	return frames;
}

int RelativeFrame(int frame, int first_frame) {
	return frame - first_frame + 1;
}

double Distance(double ax, double ay, double bx, double by) {
	double dx = ax - bx;
	double dy = ay - by;
	return std::sqrt(dx * dx + dy * dy);
}

bool ModeHasSize(MotionTrackMode mode) {
	return mode == MotionTrackMode::PositionSize || mode == MotionTrackMode::PositionSizeRotation;
}

bool ModeHasRotation(MotionTrackMode mode) {
	return mode == MotionTrackMode::PositionRotation || mode == MotionTrackMode::PositionSizeRotation;
}

std::set<int> LockedFrames(std::vector<MotionTrackFrame> const& frames, MotionTrackExportSettings const& settings) {
	std::set<int> locked;
	if (!frames.empty()) {
		locked.insert(frames.front().frame);
		locked.insert(frames.back().frame);
	}
	for (auto const& range : settings.locked_ranges) {
		locked.insert(range.first);
		locked.insert(range.second);
	}
	return locked;
}

bool IsScalarSpike(double previous, double current, double next, double expected, double threshold) {
	return std::abs(current - expected) > threshold &&
		std::abs(current - previous) > threshold &&
		std::abs(current - next) > threshold;
}

void ApplyTinyJitterCleanup(std::vector<MotionTrackFrame>& frames, MotionTrackExportSettings const& settings) {
	if (frames.size() < 2)
		return;

	auto locked = LockedFrames(frames, settings);
	double threshold = std::max(0.0, settings.cleanup_threshold);
	double scale_threshold = threshold * 0.01;
	for (size_t i = 1; i < frames.size(); ++i) {
		if (locked.count(frames[i].frame))
			continue;

		if (Distance(frames[i].x, frames[i].y, frames[i - 1].x, frames[i - 1].y) < threshold) {
			frames[i].x = frames[i - 1].x;
			frames[i].y = frames[i - 1].y;
			frames[i].anchor_x = frames[i].x;
			frames[i].anchor_y = frames[i].y;
		}

		if (ModeHasSize(settings.mode)) {
			if (std::abs(frames[i].scale_x - frames[i - 1].scale_x) < scale_threshold)
				frames[i].scale_x = frames[i - 1].scale_x;
			if (std::abs(frames[i].scale_y - frames[i - 1].scale_y) < scale_threshold)
				frames[i].scale_y = frames[i - 1].scale_y;
		}
		if (ModeHasRotation(settings.mode) && std::abs(frames[i].rotation_deg - frames[i - 1].rotation_deg) < threshold)
			frames[i].rotation_deg = frames[i - 1].rotation_deg;
	}
}

void ApplySpikeCleanup(std::vector<MotionTrackFrame>& frames, MotionTrackExportSettings const& settings) {
	if (frames.size() < 3)
		return;

	auto locked = LockedFrames(frames, settings);
	double threshold = std::max(0.0, settings.cleanup_threshold);
	double scale_threshold = threshold * 0.01;
	auto raw = frames;
	for (size_t i = 1; i + 1 < frames.size(); ++i) {
		if (locked.count(raw[i].frame))
			continue;

		int span = std::max(1, raw[i + 1].frame - raw[i - 1].frame);
		double t = (raw[i].frame - raw[i - 1].frame) / static_cast<double>(span);
		double expected_x = raw[i - 1].x + (raw[i + 1].x - raw[i - 1].x) * t;
		double expected_y = raw[i - 1].y + (raw[i + 1].y - raw[i - 1].y) * t;
		double spike_error = Distance(raw[i].x, raw[i].y, expected_x, expected_y);
		double from_prev = Distance(raw[i].x, raw[i].y, raw[i - 1].x, raw[i - 1].y);
		double to_next = Distance(raw[i].x, raw[i].y, raw[i + 1].x, raw[i + 1].y);

		if (spike_error > threshold && from_prev > threshold && to_next > threshold) {
			frames[i].x = expected_x;
			frames[i].y = expected_y;
			frames[i].anchor_x = expected_x;
			frames[i].anchor_y = expected_y;
		}

		if (ModeHasSize(settings.mode)) {
			double expected_scale_x = raw[i - 1].scale_x + (raw[i + 1].scale_x - raw[i - 1].scale_x) * t;
			double expected_scale_y = raw[i - 1].scale_y + (raw[i + 1].scale_y - raw[i - 1].scale_y) * t;
			if (IsScalarSpike(raw[i - 1].scale_x, raw[i].scale_x, raw[i + 1].scale_x, expected_scale_x, scale_threshold))
				frames[i].scale_x = expected_scale_x;
			if (IsScalarSpike(raw[i - 1].scale_y, raw[i].scale_y, raw[i + 1].scale_y, expected_scale_y, scale_threshold))
				frames[i].scale_y = expected_scale_y;
		}
		if (ModeHasRotation(settings.mode)) {
			double expected_rotation = raw[i - 1].rotation_deg + (raw[i + 1].rotation_deg - raw[i - 1].rotation_deg) * t;
			if (IsScalarSpike(raw[i - 1].rotation_deg, raw[i].rotation_deg, raw[i + 1].rotation_deg, expected_rotation, threshold))
				frames[i].rotation_deg = expected_rotation;
		}
	}
}

void LinearizeRange(std::vector<MotionTrackFrame>& frames, size_t first, size_t last, MotionTrackMode mode) {
	if (last <= first + 1)
		return;

	int span = std::max(1, frames[last].frame - frames[first].frame);
	double start_x = frames[first].x;
	double start_y = frames[first].y;
	double end_x = frames[last].x;
	double end_y = frames[last].y;
	double start_scale_x = frames[first].scale_x;
	double start_scale_y = frames[first].scale_y;
	double end_scale_x = frames[last].scale_x;
	double end_scale_y = frames[last].scale_y;
	double start_rotation = frames[first].rotation_deg;
	double end_rotation = frames[last].rotation_deg;

	for (size_t i = first + 1; i < last; ++i) {
		double t = (frames[i].frame - frames[first].frame) / static_cast<double>(span);
		frames[i].x = start_x + (end_x - start_x) * t;
		frames[i].y = start_y + (end_y - start_y) * t;
		frames[i].anchor_x = frames[i].x;
		frames[i].anchor_y = frames[i].y;
		if (ModeHasSize(mode)) {
			frames[i].scale_x = start_scale_x + (end_scale_x - start_scale_x) * t;
			frames[i].scale_y = start_scale_y + (end_scale_y - start_scale_y) * t;
		}
		if (ModeHasRotation(mode))
			frames[i].rotation_deg = start_rotation + (end_rotation - start_rotation) * t;
	}
}

void ApplyLinearCleanup(std::vector<MotionTrackFrame>& frames, MotionTrackExportSettings const& settings) {
	if (frames.size() < 3)
		return;

	std::map<int, size_t> frame_index;
	for (size_t i = 0; i < frames.size(); ++i)
		frame_index[frames[i].frame] = i;

	if (settings.locked_ranges.empty()) {
		LinearizeRange(frames, 0, frames.size() - 1, settings.mode);
		return;
	}

	for (auto const& range : settings.locked_ranges) {
		auto first_it = frame_index.find(std::min(range.first, range.second));
		auto last_it = frame_index.find(std::max(range.first, range.second));
		if (first_it == frame_index.end() || last_it == frame_index.end())
			continue;
		LinearizeRange(frames, first_it->second, last_it->second, settings.mode);
	}
}

std::vector<MotionTrackFrame> CleanupFrames(std::vector<MotionTrackFrame> frames, MotionTrackExportSettings const& settings) {
	switch (settings.cleanup) {
		case MotionTrackCleanup::RemoveTinyJitter:
			ApplyTinyJitterCleanup(frames, settings);
			break;
		case MotionTrackCleanup::RemoveSpikes:
			ApplySpikeCleanup(frames, settings);
			break;
		case MotionTrackCleanup::LinearPerRun:
			ApplyLinearCleanup(frames, settings);
			break;
		default:
			break;
	}
	return frames;
}
}

std::vector<MotionTrackFrame> StabilizeMotionTrackFrames(MotionTrackResult const& result, MotionTrackExportSettings settings) {
	return CleanupFrames(SortedFrames(result), settings);
}

std::string ExportAfterEffectsKeyframes(MotionTrackResult const& result, int first_frame, MotionTrackExportSettings settings) {
	auto frames = StabilizeMotionTrackFrames(result, settings);
	if (first_frame < 0 && !frames.empty())
		first_frame = frames.front().frame;
	if (first_frame < 0)
		first_frame = 0;

	std::ostringstream out;
	out << std::fixed << std::setprecision(3);
	out << "Adobe After Effects 6.0 Keyframe Data\r\n\r\n";
	out << "\tUnits Per Second\t" << result.fps << "\r\n";
	out << "\tSource Width\t" << result.source_width << "\r\n";
	out << "\tSource Height\t" << result.source_height << "\r\n";
	out << "\tSource Pixel Aspect Ratio\t1\r\n";
	out << "\tComp Pixel Aspect Ratio\t1\r\n\r\n";

	out << "Anchor Point\r\n";
	out << "\tFrame\tX pixels\tY pixels\tZ pixels\r\n";
	for (auto const& frame : frames)
		out << "\t" << RelativeFrame(frame.frame, first_frame) << "\t" << frame.anchor_x << "\t" << frame.anchor_y << "\t0\r\n";
	out << "\r\n";

	out << "Position\r\n";
	out << "\tFrame\tX pixels\tY pixels\tZ pixels\r\n";
	for (auto const& frame : frames)
		out << "\t" << RelativeFrame(frame.frame, first_frame) << "\t" << frame.x << "\t" << frame.y << "\t0\r\n";
	out << "\r\n";

	out << "Scale\r\n";
	out << "\tFrame\tX percent\tY percent\tZ percent\r\n";
	for (auto const& frame : frames)
		out << "\t" << RelativeFrame(frame.frame, first_frame) << "\t" << frame.scale_x * 100.0 << "\t" << frame.scale_y * 100.0 << "\t100\r\n";
	out << "\r\n";

	out << "Rotation\r\n";
	out << "\tFrame\tDegrees\r\n";
	for (auto const& frame : frames)
		out << "\t" << RelativeFrame(frame.frame, first_frame) << "\t" << frame.rotation_deg << "\r\n";
	out << "\r\n";

	out << "End of Keyframe Data\r\n";
	return out.str();
}

} // namespace motion_tracking
