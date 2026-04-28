#include "motion_track_engine.h"

#include <algorithm>
#include <cmath>

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

double NormalizeAngleDelta(double delta) {
	while (delta > 180.0)
		delta -= 360.0;
	while (delta < -180.0)
		delta += 360.0;
	return delta;
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
struct TemplateMatchResult {
	bool valid = false;
	bool position_clamped = false;
	double confidence = 0.0;
	cv::Rect pattern_rect;
	cv::Rect search_rect;
	MotionTrackMarker marker;
};

struct AffineRefinement {
	bool valid = false;
	double size = 0.0;
	double rotation_deg = 0.0;
	double confidence = 0.0;
};

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

cv::Mat ToGray(MotionTrackImage const& image) {
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
	return gray;
}

double PatchStdDev(cv::Mat const& patch) {
	cv::Scalar mean;
	cv::Scalar stddev;
	cv::meanStdDev(patch, mean, stddev);
	return stddev[0];
}

cv::Mat PrepareMatchPatch(cv::Mat const& patch, bool normalize) {
	cv::Mat out;
	patch.convertTo(out, CV_32F);
	if (!normalize)
		return out;

	cv::Scalar mean;
	cv::Scalar stddev;
	cv::meanStdDev(out, mean, stddev);
	if (stddev[0] > 1e-6)
		out = (out - mean[0]) / stddev[0];
	else
		out.setTo(0.0);
	return out;
}

TemplateMatchResult RunTemplatePrepass(
	cv::Mat const& prev_gray,
	cv::Mat const& next_gray,
	MotionTrackMarker const& pattern_marker,
	MotionTrackMarker const& search_marker,
	MotionTrackSettings const& settings) {

	TemplateMatchResult match;
	double square_size = std::max(4.0, pattern_marker.size);
	double search_size = std::max(square_size, search_marker.search_size);

	match.pattern_rect = ClampRect(pattern_marker.cx, pattern_marker.cy, square_size, prev_gray.cols, prev_gray.rows);
	match.search_rect = ClampRect(search_marker.cx, search_marker.cy, search_size, next_gray.cols, next_gray.rows);
	if (match.pattern_rect.width < 4 || match.pattern_rect.height < 4 ||
		match.search_rect.width < match.pattern_rect.width ||
		match.search_rect.height < match.pattern_rect.height)
		return match;

	double texture = PatchStdDev(prev_gray(match.pattern_rect));
	cv::Mat pattern_patch = PrepareMatchPatch(prev_gray(match.pattern_rect), settings.brightness_normalize);
	cv::Mat search_patch = PrepareMatchPatch(next_gray(match.search_rect), settings.brightness_normalize);

	cv::Mat response;
	cv::matchTemplate(search_patch, pattern_patch, response, cv::TM_CCOEFF_NORMED);
	double max_value = 0.0;
	cv::Point max_loc;
	cv::minMaxLoc(response, nullptr, &max_value, nullptr, &max_loc);
	if (!std::isfinite(max_value))
		max_value = 0.0;

	match.valid = true;
	match.confidence = std::clamp(max_value, 0.0, 1.0);
	if (texture < 1.0)
		match.confidence = std::min(match.confidence, 0.35);

	double pattern_offset_x = std::clamp(pattern_marker.cx - match.pattern_rect.x, 0.0, static_cast<double>(match.pattern_rect.width));
	double pattern_offset_y = std::clamp(pattern_marker.cy - match.pattern_rect.y, 0.0, static_cast<double>(match.pattern_rect.height));
	double matched_x = match.search_rect.x + max_loc.x + pattern_offset_x;
	double matched_y = match.search_rect.y + max_loc.y + pattern_offset_y;

	double dx = matched_x - search_marker.cx;
	double dy = matched_y - search_marker.cy;
	double jump = std::sqrt(dx * dx + dy * dy);
	double max_jump = std::max(search_marker.search_size, search_marker.size) / 2.0;
	if (jump > max_jump && jump > 0.0) {
		double scale = max_jump / jump;
		matched_x = search_marker.cx + dx * scale;
		matched_y = search_marker.cy + dy * scale;
		match.position_clamped = true;
	}

	match.marker = search_marker;
	match.marker.cx = std::clamp(matched_x, 0.0, static_cast<double>(std::max(0, next_gray.cols - 1)));
	match.marker.cy = std::clamp(matched_y, 0.0, static_cast<double>(std::max(0, next_gray.rows - 1)));
	match.marker.search_size = std::max(match.marker.search_size, match.marker.size);
	return match;
}

AffineRefinement RefineAffine(
	cv::Mat const& prev_gray,
	cv::Mat const& next_gray,
	TemplateMatchResult const& match,
	MotionTrackMarker const& pattern_marker) {

	AffineRefinement refinement;
	cv::Mat mask = cv::Mat::zeros(prev_gray.size(), CV_8UC1);
	mask(match.pattern_rect).setTo(255);

	std::vector<cv::Point2f> points;
	cv::goodFeaturesToTrack(prev_gray, points, 80, 0.01, 4.0, mask, 3, false, 0.04);
	if (points.size() < 3)
		return refinement;

	cv::Point2f prepass_delta(
		static_cast<float>(match.marker.cx - pattern_marker.cx),
		static_cast<float>(match.marker.cy - pattern_marker.cy));
	std::vector<cv::Point2f> next_points;
	next_points.reserve(points.size());
	for (auto const& point : points)
		next_points.push_back(point + prepass_delta);

	std::vector<unsigned char> status;
	std::vector<float> err;
	cv::calcOpticalFlowPyrLK(
		prev_gray,
		next_gray,
		points,
		next_points,
		status,
		err,
		cv::Size(21, 21),
		3,
		cv::TermCriteria(cv::TermCriteria::COUNT | cv::TermCriteria::EPS, 30, 0.01),
		cv::OPTFLOW_USE_INITIAL_FLOW);

	double refine_size = std::max(match.marker.size * 1.75, pattern_marker.size);
	cv::Rect refine_rect = ClampRect(match.marker.cx, match.marker.cy, refine_size, next_gray.cols, next_gray.rows);
	double max_delta_error = std::max(6.0, pattern_marker.size * 0.50);

	std::vector<cv::Point2f> good_from;
	std::vector<cv::Point2f> good_to;
	for (size_t i = 0; i < points.size(); ++i) {
		if (!status[i])
			continue;
		if (!Contains(refine_rect, next_points[i]))
			continue;

		double delta_error_x = (next_points[i].x - points[i].x) - prepass_delta.x;
		double delta_error_y = (next_points[i].y - points[i].y) - prepass_delta.y;
		if (std::sqrt(delta_error_x * delta_error_x + delta_error_y * delta_error_y) > max_delta_error)
			continue;

		good_from.push_back(points[i]);
		good_to.push_back(next_points[i]);
	}

	if (good_from.size() < 3)
		return refinement;

	cv::Mat inliers;
	cv::Mat transform = cv::estimateAffinePartial2D(good_from, good_to, inliers, cv::RANSAC, 3.0, 2000, 0.99, 10);
	if (transform.empty() || transform.cols != 3 || transform.rows != 2)
		return refinement;

	double inlier_count = inliers.empty() ? static_cast<double>(good_from.size()) : cv::countNonZero(inliers);
	double inlier_ratio = good_from.empty() ? 0.0 : inlier_count / static_cast<double>(good_from.size());
	if (inlier_ratio < 0.35)
		return refinement;

	double a = transform.at<double>(0, 0);
	double b = transform.at<double>(1, 0);
	double scale = std::sqrt(a * a + b * b);
	if (!std::isfinite(scale) || scale <= 0.0)
		return refinement;

	refinement.valid = true;
	refinement.size = std::max(4.0, pattern_marker.size * scale);
	refinement.rotation_deg = pattern_marker.rotation_deg + std::atan2(b, a) * 180.0 / pi;
	refinement.confidence = std::clamp(inlier_ratio * static_cast<double>(good_from.size()) / static_cast<double>(points.size()), 0.0, 1.0);
	return refinement;
}

MotionTrackStepResult PredictFromPrepass(
	int target_frame,
	MotionTrackMarker marker,
	double confidence) {

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
	MotionTrackMarker const& pattern_marker,
	MotionTrackMarker const& search_marker,
	int target_frame,
	MotionTrackSettings const& settings) const {

#ifndef WITH_OPENCV
	(void)from;
	(void)to;
	(void)pattern_marker;
	(void)settings;
	return MakeResult(target_frame, search_marker, 0.0, MotionTrackState::Lost);
#else
	cv::Mat prev_gray = ToGray(from);
	cv::Mat next_gray = ToGray(to);
	if (prev_gray.empty() || next_gray.empty())
		return MakeResult(target_frame, search_marker, 0.0, MotionTrackState::Lost);

	TemplateMatchResult match = RunTemplatePrepass(prev_gray, next_gray, pattern_marker, search_marker, settings);
	if (!match.valid)
		return MakeResult(target_frame, search_marker, 0.0, MotionTrackState::Lost);

	double high_threshold = std::clamp(settings.correlation_threshold, 0.0, 1.0);
	double medium_threshold = high_threshold * 0.70;
	double low_threshold = std::max(0.05, high_threshold * 0.35);

	if (match.confidence < low_threshold)
		return MakeResult(target_frame, search_marker, match.confidence, MotionTrackState::Lost);

	if (match.confidence < medium_threshold || match.position_clamped)
		return PredictFromPrepass(target_frame, match.marker, match.confidence);

	if (match.confidence < high_threshold)
		return MakeResult(target_frame, match.marker, match.confidence, MotionTrackState::WeakTracked);

	MotionTrackMarker next_marker = match.marker;
	MotionTrackState state = MotionTrackState::Tracked;
	bool needs_affine = HasSize(settings.mode) || HasRotation(settings.mode);
	AffineRefinement refinement = RefineAffine(prev_gray, next_gray, match, pattern_marker);
	if (refinement.valid) {
		if (HasSize(settings.mode)) {
			double scale_change = refinement.size / std::max(4.0, search_marker.size);
			if (std::isfinite(scale_change))
				next_marker.size = std::max(4.0, search_marker.size * std::clamp(scale_change, 0.95, 1.05));
		}
		if (HasRotation(settings.mode)) {
			double rotation_change = NormalizeAngleDelta(refinement.rotation_deg - search_marker.rotation_deg);
			next_marker.rotation_deg = search_marker.rotation_deg + std::clamp(rotation_change, -5.0, 5.0);
		}
	}
	else if (needs_affine)
		state = MotionTrackState::WeakTracked;

	next_marker.search_size = std::max(next_marker.search_size, next_marker.size);
	return MakeResult(target_frame, next_marker, match.confidence, state);
#endif
}

} // namespace motion_tracking
