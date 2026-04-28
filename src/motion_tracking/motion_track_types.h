#pragma once

#include <vector>

namespace motion_tracking {

struct MotionTrackMarker {
	double cx = 0.0;
	double cy = 0.0;
	double size = 80.0;
	double search_size = 200.0;
	double rotation_deg = 0.0;
};

enum class MotionTrackMode {
	PositionOnly,
	PositionRotation,
	PositionSize,
	PositionSizeRotation
};

enum class MotionTrackBase {
	PreviousFrame,
	FirstFrame
};

enum class MotionTrackSmoothing {
	Off,
	Light,
	Medium,
	Heavy
};

enum class MotionTrackState {
	Untracked,
	Tracked,
	WeakTracked,
	Predicted,
	Lost
};

struct MotionTrackFrame {
	int frame = 0;
	double x = 0.0;
	double y = 0.0;
	double anchor_x = 0.0;
	double anchor_y = 0.0;
	double scale_x = 1.0;
	double scale_y = 1.0;
	double rotation_deg = 0.0;
	double confidence = 0.0;
	MotionTrackState state = MotionTrackState::Untracked;
};

struct MotionTrackSettings {
	int start_frame = 0;
	int end_frame = 0;
	int square_size = 80;
	int search_size = 200;
	MotionTrackMode mode = MotionTrackMode::PositionSizeRotation;
	MotionTrackBase base = MotionTrackBase::PreviousFrame;
	bool brightness_normalize = true;
	bool prepass = true;
	double correlation_threshold = 0.75;
	MotionTrackSmoothing smoothing = MotionTrackSmoothing::Medium;
};

struct MotionTrackResult {
	double fps = 24.0;
	int source_width = 0;
	int source_height = 0;
	std::vector<MotionTrackFrame> frames;
};

struct MotionTrackImage {
	int width = 0;
	int height = 0;
	int pitch = 0;
	bool flipped = false;
	std::vector<unsigned char> bgra;
};

struct MotionTrackStepResult {
	MotionTrackMarker marker;
	MotionTrackFrame frame;
};

} // namespace motion_tracking
