#pragma once

#include "motion_tracking/motion_track_types.h"

#include <libaegisub/signal.h>

#include <map>
#include <memory>
#include <vector>

#include <wx/dialog.h>
#include <wx/image.h>

class MotionTrackFrameBar;
class MotionTrackGraphPanel;
class MotionTrackPreviewPanel;
class PersistLocation;
class wxButton;
class wxCheckBox;
class wxChoice;
class wxSpinCtrl;
class wxSpinCtrlDouble;
class wxStaticText;
namespace agi { struct Context; }

class DialogMotionTrack final : public wxDialog {
	agi::Context *context = nullptr;

	motion_tracking::MotionTrackSettings settings;
	motion_tracking::MotionTrackResult result;
	std::map<int, motion_tracking::MotionTrackMarker> markers;

	int current_frame = 0;
	int base_frame = -1;
	double initial_marker_size = 80.0;

	wxImage preview_image;

	wxStaticText *range_label = nullptr;
	wxStaticText *current_label = nullptr;
	wxSpinCtrl *square_ctrl = nullptr;
	wxSpinCtrl *search_ctrl = nullptr;
	wxSpinCtrlDouble *threshold_ctrl = nullptr;
	wxCheckBox *normalize_check = nullptr;
	wxChoice *base_choice = nullptr;
	wxChoice *mode_choice = nullptr;
	wxChoice *smoothing_choice = nullptr;
	wxButton *track_to_start = nullptr;
	wxButton *track_previous = nullptr;
	wxButton *track_next = nullptr;
	wxButton *track_to_end = nullptr;
	MotionTrackFrameBar *frame_bar = nullptr;
	MotionTrackPreviewPanel *preview = nullptr;
	MotionTrackGraphPanel *graph = nullptr;

	std::vector<agi::signal::Connection> connections;
	std::unique_ptr<PersistLocation> persist;

	void CalculateSelectedFrameRange();
	void CreateControls();
	void BindControls();
	void UpdateSettingsFromControls();
	void UpdateLabels();
	void UpdatePanels();
	void LoadCurrentFrame();
	void OnSeek(int frame);
	void TrackOne(int target_frame);
	void TrackRange(int target_frame);
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
	wxImage const& GetPreviewImage() const { return preview_image; }
	motion_tracking::MotionTrackResult const& GetResult() const { return result; }
	bool HasCurrentMarker() const;
	motion_tracking::MotionTrackMarker GetCurrentMarker() const;
	void SetCurrentMarker(motion_tracking::MotionTrackMarker marker);
	void PlaceCurrentMarker(double x, double y);
	void DeleteCurrentMarker();
	void JumpToFrame(int frame);
};
