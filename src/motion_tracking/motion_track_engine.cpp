#include "motion_track_engine.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#ifdef WITH_OPENCV
#include <opencv2/calib3d.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>
#endif

namespace motion_tracking {

namespace {
constexpr double pi = 3.14159265358979323846;

bool HasRotation(MotionTrackMode mode) {
	return mode == MotionTrackMode::PositionRotation || mode == MotionTrackMode::PositionSizeRotation;
}

bool HasSize(MotionTrackMode mode) {
	return mode == MotionTrackMode::PositionSize || mode == MotionTrackMode::PositionSizeRotation;
}

MotionTrackFrame MakeFrame(int frame, MotionTrackMarker const& marker, double confidence, MotionTrackState state) {
	return MotionTrackFrame{
		frame,
		marker.cx,
		marker.cy,
		marker.cx,
		marker.cy,
		1.0,
		1.0,
		marker.rotation_deg,
		confidence,
		state
	};
}

MotionTrackStepResult MakeResult(int frame, MotionTrackMarker marker, double confidence, MotionTrackState state) {
	marker.search_size = std::max(marker.search_size, marker.size);
	return {marker, MakeFrame(frame, marker, confidence, state)};
}

#ifdef WITH_OPENCV
cv::Rect ClampRect(double cx, double cy, double size, int width, int height) {
	int left = static_cast<int>(std::floor(cx - size / 2.0));
	int top = static_cast<int>(std::floor(cy - size / 2.0));
	int right = static_cast<int>(std::ceil(cx + size / 2.0));
	int bottom = static_cast<int>(std::ceil(cy + size / 2.0));

	left = std::clamp(left, 0, width);
	top = std::clamp(top, 0, height);
	right = std::clamp(right, left, width);
	bottom = std::clamp(bottom, top, height);
	return cv::Rect(left, top, right - left, bottom - top);
}

bool Contains(cv::Rect const& rect, cv::Point2f const& point) {
	return point.x >= rect.x && point.y >= rect.y &&
		point.x < rect.x + rect.width && point.y < rect.y + rect.height;
}

cv::Mat ToGray(MotionTrackImage const& image, bool normalize_brightness) {
	if (image.width <= 0 || image.height <= 0 || image.pitch <= 0 || image.bgra.empty())
		return {};

	cv::Mat bgra(image.height, image.width, CV_8UC4, const_cast<unsigned char *>(image.bgra.data()), static_cast<size_t>(image.pitch));
	cv::Mat oriented;
	if (image.flipped)
		cv::flip(bgra, oriented, 0);
	else
		oriented = bgra;

	cv::Mat gray;
	cv::cvtColor(oriented, gray, cv::COLOR_BGRA2GRAY);
	if (normalize_brightness)
		cv::equalizeHist(gray, gray);
	return gray;
}

MotionTrackStepResult PredictFromPoints(
	int target_frame,
	MotionTrackMarker marker,
	std::vector<cv::Point2f> const& from_points,
	std::vector<cv::Point2f> const& to_points) {

	if (from_points.empty() || to_points.empty())
		return MakeResult(target_frame, marker, 0.0, MotionTrackState::Lost);

	double dx = 0.0;
	double dy = 0.0;
	for (size_t i = 0; i < from_points.size(); ++i) {
		dx += to_points[i].x - from_points[i].x;
		dy += to_points[i].y - from_points[i].y;
	}
	dx /= from_points.size();
	dy /= from_points.size();

	marker.cx += dx;
	marker.cy += dy;
	double confidence = std::min(0.35, static_cast<double>(from_points.size()) / 40.0);
	return MakeResult(target_frame, marker, confidence, MotionTrackState::Predicted);
}
#endif
}

bool MotionTrackEngine::IsAvailable() {
#ifdef WITH_OPENCV
	return true;
#else
	return false;
#endif
}

MotionTrackStepResult MotionTrackEngine::TrackFrame(
	MotionTrackImage const& from,
	MotionTrackImage const& to,
	MotionTrackMarker const& marker,
	int target_frame,
	MotionTrackMode mode,
	bool brightness_normalize) const {

#ifndef WITH_OPENCV
	(void)from;
	(void)to;
	(void)mode;
	(void)brightness_normalize;
	return MakeResult(target_frame, marker, 0.0, MotionTrackState::Lost);
#else
	cv::Mat prev_gray = ToGray(from, brightness_normalize);
	cv::Mat next_gray = ToGray(to, brightness_normalize);
	if (prev_gray.empty() || next_gray.empty())
		return MakeResult(target_frame, marker, 0.0, MotionTrackState::Lost);

	double square_size = std::max(4.0, marker.size);
	double search_size = std::max(square_size, marker.search_size);
	cv::Rect pattern_rect = ClampRect(marker.cx, marker.cy, square_size, prev_gray.cols, prev_gray.rows);
	cv::Rect search_rect = ClampRect(marker.cx, marker.cy, search_size, next_gray.cols, next_gray.rows);
	if (pattern_rect.width < 4 || pattern_rect.height < 4 || search_rect.width < 4 || search_rect.height < 4)
		return MakeResult(target_frame, marker, 0.0, MotionTrackState::Lost);

	cv::Mat mask = cv::Mat::zeros(prev_gray.size(), CV_8UC1);
	mask(pattern_rect).setTo(255);

	std::vector<cv::Point2f> points;
	cv::goodFeaturesToTrack(prev_gray, points, 80, 0.01, 4.0, mask, 3, false, 0.04);
	if (points.size() < 2)
		return MakeResult(target_frame, marker, 0.0, MotionTrackState::Lost);

	std::vector<cv::Point2f> next_points;
	std::vector<unsigned char> status;
	std::vector<float> err;
	cv::calcOpticalFlowPyrLK(prev_gray, next_gray, points, next_points, status, err, cv::Size(21, 21), 3);

	std::vector<cv::Point2f> good_from;
	std::vector<cv::Point2f> good_to;
	for (size_t i = 0; i < points.size(); ++i) {
		if (!status[i])
			continue;
		if (!Contains(search_rect, next_points[i]))
			continue;
		good_from.push_back(points[i]);
		good_to.push_back(next_points[i]);
	}

	if (good_from.size() < 2)
		return PredictFromPoints(target_frame, marker, good_from, good_to);

	cv::Mat inliers;
	cv::Mat transform = cv::estimateAffinePartial2D(good_from, good_to, inliers, cv::RANSAC, 3.0, 2000, 0.99, 10);
	if (transform.empty() || transform.cols != 3 || transform.rows != 2)
		return PredictFromPoints(target_frame, marker, good_from, good_to);

	double inlier_count = inliers.empty() ? static_cast<double>(good_from.size()) : cv::countNonZero(inliers);
	double inlier_ratio = good_from.empty() ? 0.0 : inlier_count / static_cast<double>(good_from.size());

	double a = transform.at<double>(0, 0);
	double b = transform.at<double>(1, 0);
	double scale = std::sqrt(a * a + b * b);
	if (!std::isfinite(scale) || scale <= 0.0)
		scale = 1.0;
	double rotation_delta = std::atan2(b, a) * 180.0 / pi;

	MotionTrackMarker next_marker = marker;
	next_marker.cx = transform.at<double>(0, 0) * marker.cx + transform.at<double>(0, 1) * marker.cy + transform.at<double>(0, 2);
	next_marker.cy = transform.at<double>(1, 0) * marker.cx + transform.at<double>(1, 1) * marker.cy + transform.at<double>(1, 2);

	if (HasSize(mode))
		next_marker.size = std::max(4.0, marker.size * scale);
	if (HasRotation(mode))
		next_marker.rotation_deg = marker.rotation_deg + rotation_delta;
	next_marker.search_size = std::max(next_marker.search_size, next_marker.size);

	double point_ratio = std::min(1.0, static_cast<double>(good_from.size()) / std::max<size_t>(1, points.size()));
	double confidence = std::clamp(point_ratio * inlier_ratio, 0.0, 1.0);
	auto state = confidence < 0.35 ? MotionTrackState::WeakTracked : MotionTrackState::Tracked;
	return MakeResult(target_frame, next_marker, confidence, state);
#endif
}

} // namespace motion_tracking
