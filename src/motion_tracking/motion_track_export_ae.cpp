#include "motion_track_export_ae.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>
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

double Median(std::vector<double> values) {
	if (values.empty())
		return 0.0;
	auto mid = values.begin() + values.size() / 2;
	std::nth_element(values.begin(), mid, values.end());
	return *mid;
}

double RotationDelta(double current, double previous) {
	double delta = current - previous;
	while (delta > 180.0)
		delta -= 360.0;
	while (delta < -180.0)
		delta += 360.0;
	return delta;
}

int SmoothingRadius(MotionTrackSmoothing smoothing) {
	switch (smoothing) {
		case MotionTrackSmoothing::Light: return 1;
		case MotionTrackSmoothing::Medium: return 2;
		case MotionTrackSmoothing::Heavy: return 3;
		default: return 0;
	}
}

double EmaAlpha(MotionTrackSmoothing smoothing) {
	switch (smoothing) {
		case MotionTrackSmoothing::Light: return 0.60;
		case MotionTrackSmoothing::Medium: return 0.45;
		case MotionTrackSmoothing::Heavy: return 0.30;
		default: return 1.0;
	}
}

std::vector<double> MedianSmooth(std::vector<double> const& values, int radius) {
	if (radius <= 0 || values.size() < 3)
		return values;

	std::vector<double> out(values.size());
	for (size_t i = 0; i < values.size(); ++i) {
		size_t first = i > static_cast<size_t>(radius) ? i - radius : 0;
		size_t last = std::min(values.size() - 1, i + static_cast<size_t>(radius));
		std::vector<double> window;
		window.reserve(last - first + 1);
		for (size_t j = first; j <= last; ++j)
			window.push_back(values[j]);
		out[i] = Median(std::move(window));
	}
	return out;
}

std::vector<double> EmaSmooth(std::vector<double> values, double alpha) {
	if (values.size() < 2 || alpha >= 1.0)
		return values;

	for (size_t i = 1; i < values.size(); ++i)
		values[i] = alpha * values[i] + (1.0 - alpha) * values[i - 1];
	return values;
}

void ApplyDeadzones(
	std::vector<double>& x,
	std::vector<double>& y,
	std::vector<double>& scale_x,
	std::vector<double>& scale_y,
	std::vector<double>& rotation,
	MotionTrackExportSettings const& settings) {

	for (size_t i = 1; i < x.size(); ++i) {
		double dx = x[i] - x[i - 1];
		double dy = y[i] - y[i - 1];
		if (std::sqrt(dx * dx + dy * dy) < settings.position_deadzone) {
			x[i] = x[i - 1];
			y[i] = y[i - 1];
		}

		if (std::abs(scale_x[i] - scale_x[i - 1]) < settings.scale_deadzone)
			scale_x[i] = scale_x[i - 1];
		if (std::abs(scale_y[i] - scale_y[i - 1]) < settings.scale_deadzone)
			scale_y[i] = scale_y[i - 1];

		if (std::abs(rotation[i] - rotation[i - 1]) < settings.rotation_deadzone)
			rotation[i] = rotation[i - 1];
	}
}

std::vector<MotionTrackFrame> StabilizeFrames(std::vector<MotionTrackFrame> frames, MotionTrackExportSettings const& settings) {
	if (frames.size() < 2)
		return frames;

	for (size_t i = 1; i < frames.size(); ++i)
		frames[i].rotation_deg = frames[i - 1].rotation_deg + RotationDelta(frames[i].rotation_deg, frames[i - 1].rotation_deg);

	if (settings.smoothing == MotionTrackSmoothing::Off)
		return frames;

	std::vector<double> x;
	std::vector<double> y;
	std::vector<double> scale_x;
	std::vector<double> scale_y;
	std::vector<double> rotation;
	x.reserve(frames.size());
	y.reserve(frames.size());
	scale_x.reserve(frames.size());
	scale_y.reserve(frames.size());
	rotation.reserve(frames.size());
	for (auto const& frame : frames) {
		x.push_back(frame.x);
		y.push_back(frame.y);
		scale_x.push_back(frame.scale_x);
		scale_y.push_back(frame.scale_y);
		rotation.push_back(frame.rotation_deg);
	}

	int radius = SmoothingRadius(settings.smoothing);
	double alpha = EmaAlpha(settings.smoothing);
	x = EmaSmooth(MedianSmooth(x, radius), alpha);
	y = EmaSmooth(MedianSmooth(y, radius), alpha);
	scale_x = EmaSmooth(MedianSmooth(scale_x, radius), alpha);
	scale_y = EmaSmooth(MedianSmooth(scale_y, radius), alpha);
	rotation = EmaSmooth(MedianSmooth(rotation, radius), alpha);
	ApplyDeadzones(x, y, scale_x, scale_y, rotation, settings);

	for (size_t i = 0; i < frames.size(); ++i) {
		frames[i].x = x[i];
		frames[i].y = y[i];
		frames[i].anchor_x = x[i];
		frames[i].anchor_y = y[i];
		frames[i].scale_x = scale_x[i];
		frames[i].scale_y = scale_y[i];
		frames[i].rotation_deg = rotation[i];
	}
	return frames;
}
}

std::string ExportAfterEffectsKeyframes(MotionTrackResult const& result, int first_frame, MotionTrackExportSettings settings) {
	auto frames = StabilizeFrames(SortedFrames(result), settings);
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
