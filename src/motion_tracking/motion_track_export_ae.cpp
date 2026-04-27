#include "motion_track_export_ae.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

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
}

std::string ExportAfterEffectsKeyframes(MotionTrackResult const& result, int first_frame) {
	auto frames = SortedFrames(result);
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
