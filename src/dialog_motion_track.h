#pragma once

#include "motion_tracking/motion_track_export_ae.h"
#include "motion_tracking/motion_track_segments.h"

#include <libaegisub/signal.h>

#include <chrono>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include <wx/dialog.h>
#include <wx/image.h>
#include <wx/timer.h>

class MotionTrackFrameCache;
class MotionTrackFrameBar;
class MotionTrackGraphPanel;
class MotionTrackPreviewPanel;
class PersistLocation;
struct VideoFrame;
class wxButton;
class wxCheckBox;
class wxChoice;
class wxKeyEvent;
class wxSpinCtrl;
class wxSpinCtrlDouble;
class wxStaticText;
class wxTimerEvent;
namespace agi { struct Context; }

struct MotionTrackTrailMarker {
	int frame = 0;
	motion_tracking::MotionTrackMarker marker;
	motion_tracking::MotionTrackState state = motion_tracking::MotionTrackState::Untracked;
};

class DialogMotionTrack final : public wxDialog {
	agi::Context *context = nullptr;

	motion_tracking::MotionTrackSettings settings;
	motion_tracking::MotionTrackResult result;
	std::vector<motion_tracking::MotionTrackSegment> segments;
	std::map<int, motion_tracking::MotionTrackMarker> markers;
	std::set<int> handoff_marks;

	int current_frame = 0;
	int preview_frame = -1;
	int base_frame = -1;
	int active_segment = -1;
	double initial_marker_size = 80.0;
	bool show_track_trail = true;
	int track_trail_past = 10;
	int track_trail_future = 10;
	bool playing = false;
	int playback_start_frame = 0;
	int playback_start_ms = 0;
	std::chrono::steady_clock::time_point playback_start_time;

	wxImage preview_image;
	std::unique_ptr<MotionTrackFrameCache> frame_cache;

	wxStaticText *range_label = nullptr;
	wxStaticText *current_label = nullptr;
	wxStaticText *segment_label = nullptr;
	wxStaticText *cache_status_label = nullptr;
	wxSpinCtrl *square_ctrl = nullptr;
	wxSpinCtrl *search_ctrl = nullptr;
	wxSpinCtrlDouble *threshold_ctrl = nullptr;
	wxSpinCtrlDouble *cleanup_threshold_ctrl = nullptr;
	wxCheckBox *normalize_check = nullptr;
	wxCheckBox *trail_check = nullptr;
	wxChoice *mode_choice = nullptr;
	wxChoice *cleanup_choice = nullptr;
	wxButton *play_button = nullptr;
	wxButton *track_to_start = nullptr;
	wxButton *track_previous = nullptr;
	wxButton *track_next = nullptr;
	wxButton *track_to_end = nullptr;
	wxButton *mark_handoff_button = nullptr;
	wxButton *clear_handoff_button = nullptr;
	MotionTrackFrameBar *frame_bar = nullptr;
	MotionTrackPreviewPanel *preview = nullptr;
	MotionTrackGraphPanel *graph = nullptr;
	wxTimer cache_timer;
	wxTimer playback_timer;

	std::vector<agi::signal::Connection> connections;
	std::unique_ptr<PersistLocation> persist;

	void CalculateSelectedFrameRange();
	void CreateControls();
	void BindControls();
	void StartFrameCache();
	void StopFrameCache();
	void UpdateSettingsFromControls();
	void UpdateTrailControls();
	void UpdateLabels();
	void UpdateCacheStatus();
	void UpdatePanels();
	void RefreshPreview();
	void LoadCurrentFrame();
	void OnSeek(int frame);
	void OnCacheTimer(wxTimerEvent &);
	void OnPlaybackTimer(wxTimerEvent &);
	void OnCharHook(wxKeyEvent &);
	bool FocusIsTextInput() const;
	void ShowFrame(int frame);
	void StepFrame(int delta);
	void TogglePlayback();
	void StartPlayback();
	void StopPlayback();
	void UpdatePlaybackButton();
	std::shared_ptr<VideoFrame> GetCachedFrame(int frame) const;
	void TrackOne(int target_frame);
	void TrackRange(int target_frame);
	int ResolveHandoffTarget(int requested_target) const;
	int FindSegmentForFrame(int frame) const;
	int FindSegmentForTracking(int target_frame);
	int StartTrackRunHere(int target_frame);
	void MarkHandoffFrame();
	void ClearHandoffMark();
	void RecalculateMotion();
	void RebuildMarkersFromSegments();
	void StoreSegmentFrame(int segment_index, int frame, motion_tracking::MotionTrackMarker const& marker, double confidence, motion_tracking::MotionTrackState state);
	void CopyData();
	void SaveData();
	void ClearData();
	void StoreFrame(int frame, motion_tracking::MotionTrackMarker const& marker, double confidence, motion_tracking::MotionTrackState state);
	motion_tracking::MotionTrackFrame MakeFrame(int frame, motion_tracking::MotionTrackMarker const& marker, double confidence, motion_tracking::MotionTrackState state) const;
	motion_tracking::MotionTrackImage GetTrackImage(int frame) const;
	motion_tracking::MotionTrackMarker MarkerForFrame(int frame) const;

public:
	DialogMotionTrack(agi::Context *context);
	~DialogMotionTrack();

	int GetStartFrame() const { return settings.start_frame; }
	int GetEndFrame() const { return settings.end_frame; }
	int GetCurrentFrame() const { return current_frame; }
	int GetPreviewFrame() const { return preview_frame; }
	wxImage const& GetPreviewImage() const { return preview_image; }
	motion_tracking::MotionTrackResult const& GetResult() const { return result; }
	motion_tracking::MotionTrackExportSettings GetExportSettings() const;
	std::vector<MotionTrackTrailMarker> GetTrackTrailMarkers() const;
	std::vector<int> GetHandoffMarks() const;
	bool GetShowTrackTrail() const { return show_track_trail; }
	int GetTrackTrailPast() const { return track_trail_past; }
	int GetTrackTrailFuture() const { return track_trail_future; }
	bool HandleNavigationKey(int key_code);
	bool HasCurrentMarker() const;
	int GetCurrentSegmentVisualState() const;
	motion_tracking::MotionTrackMarker GetCurrentMarker() const;
	void SetCurrentMarker(motion_tracking::MotionTrackMarker marker);
	void PlaceCurrentMarker(double x, double y);
	void DeleteCurrentMarker();
	void JumpToFrame(int frame);
};
