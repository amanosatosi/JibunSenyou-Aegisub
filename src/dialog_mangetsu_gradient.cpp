// Copyright (c) 2026
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

/// @file dialog_mangetsu_gradient.cpp
/// @brief Dialog editor for Mangetsu true-gradient tags

#include "ass_dialogue.h"
#include "ass_file.h"
#include "compat.h"
#include "dialogs.h"
#include "gradient_placement_session.h"
#include "include/aegisub/context.h"
#include "mangetsu_gradient_placement.h"
#include "selection_controller.h"
#include "subs_controller.h"
#include "video_display.h"
#include "visual_tool_gradient_placement.h"

#include <libaegisub/format.h>
#include <libaegisub/color.h>
#include <libaegisub/make_unique.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <memory>
#include <vector>

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/cursor.h>
#include <wx/dcbuffer.h>
#include <wx/dialog.h>
#include <wx/intl.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/tglbtn.h>
#include <wx/timer.h>

#include <libaegisub/signal.h>

namespace {

// Keep the add-stop marker easy to tune without changing the interaction code.
constexpr int STOP_PLACEMENT_Y_SNAP_THRESHOLD = 40;
constexpr int STOP_PLACEMENT_MARKER_SIZE = 10;

static std::string trim_copy(std::string str) {
	auto not_space = [](unsigned char c) { return !std::isspace(c); };
	str.erase(str.begin(), std::find_if(str.begin(), str.end(), not_space));
	str.erase(std::find_if(str.rbegin(), str.rend(), not_space).base(), str.end());
	return str;
}

static bool ends_with_percent(std::string const& str) {
	return !str.empty() && str.back() == '%';
}

static double clamp_percent(double pos) {
	return std::max(0.0, std::min(100.0, pos));
}

static int clamp_alpha(int alpha) {
	return std::max(0, std::min(255, alpha));
}

static int matching_paren(std::string const& text, int open);

static wxColour to_wx_color(agi::Color const& color) {
	return wxColour(color.r, color.g, color.b);
}

static agi::Color lerp_color(agi::Color a, agi::Color b, double t) {
	t = std::max(0.0, std::min(1.0, t));
	return agi::Color(
		static_cast<unsigned char>(std::lround(a.r + (b.r - a.r) * t)),
		static_cast<unsigned char>(std::lround(a.g + (b.g - a.g) * t)),
		static_cast<unsigned char>(std::lround(a.b + (b.b - a.b) * t)),
		static_cast<unsigned char>(std::lround(a.a + (b.a - a.a) * t)));
}

static bool has_layer_tag(std::string const& text, int layer) {
	std::string prefix = "\\" + std::to_string(layer) + "b";
	static const char *suffixes[] = {"s", "sx", "sy", "c", "a", "vc", "va", "grd", "ga"};
	for (auto suffix : suffixes) {
		if (text.find(prefix + suffix) != std::string::npos)
			return true;
	}
	return false;
}

enum class TargetGroup {
	Main,
	Border
};

enum class ChannelMode {
	Color,
	Alpha
};

enum class LoadDecision {
	Load,
	Keep,
	Cancel
};

struct GradientStop {
	double pos = 0.0;
	agi::Color color{0, 0, 0};
	int alpha = 0;
};

struct TagRef {
	std::string name;
	std::string alias;
	std::string status_name;
};

struct TagRange {
	int start = -1;
	int end = -1;
	std::string name;
	std::string value;

	explicit operator bool() const { return start >= 0 && end >= start; }
};

class DialogMangetsuGradient;

class GradientStopBar final : public wxPanel {
	DialogMangetsuGradient *dialog;
	bool dragging = false;
	bool add_placement = false;
	bool add_pressing = false;
	bool marker_visible = false;
	wxPoint marker_pos;

	void OnPaint(wxPaintEvent&);
	void OnMouse(wxMouseEvent& event);
	void OnKeyDown(wxKeyEvent& event);
	void OnMouseCaptureLost(wxMouseCaptureLostEvent&);
	wxPoint SnapMarkerY(wxPoint pos) const;
	bool InPlacementArea(wxPoint pos) const;
	void UpdateMarker(wxPoint pos, bool force = false);
	void RestoreCursor();

public:
	GradientStopBar(wxWindow *parent, DialogMangetsuGradient *dialog);
	void BeginAddPlacement();
	void CancelAddPlacement();
	bool IsAdding() const { return add_placement; }
};

class DialogMangetsuGradient final : public wxDialog {
	friend class GradientStopBar;

	agi::Context *context = nullptr;
	AssDialogue *active_line = nullptr;

	TargetGroup group = TargetGroup::Main;
	ChannelMode mode = ChannelMode::Color;
	int main_index = 1;
	int border_index = 1;
	int angle = 0;
	int selected_stop = 0;
	bool dirty = false;
	bool existing = false;
	bool updating_controls = false;
	bool preview_pending = false;
	bool tag_span_valid = false;
	bool lock_placement = false;
	bool placement_capture_active = false;
	int commit_id = -1;
	int tag_span_start = -1;
	int tag_span_end = -1;

	std::string original_text;
	std::string loaded_value;
	std::vector<GradientStop> stops;
	std::vector<int> border_layers{1};
	mangetsu::PlacementRect placement_rect;
	std::shared_ptr<GradientPlacementSession> placement_session;
	agi::signal::Connection active_line_connection;
	agi::signal::Connection file_commit_connection;
	wxString preview_message;
	wxString placement_status;

	wxTimer preview_timer;
	wxSpinCtrl *angle_ctrl = nullptr;
	wxChoice *main_choice = nullptr;
	wxChoice *border_choice = nullptr;
	wxChoice *box_choice = nullptr;
	wxToggleButton *color_button = nullptr;
	wxToggleButton *alpha_button = nullptr;
	wxToggleButton *lock_placement_button = nullptr;
	wxStaticText *status_label = nullptr;
	wxButton *add_button = nullptr;
	wxButton *remove_button = nullptr;
	GradientStopBar *stop_bar = nullptr;
	wxString add_stop_status;

	TagRef CurrentTag() const;
	TagRef PlacementTag() const;
	TagRef OutputTag() const;
	std::string CurrentGroupName() const;
	std::string CurrentTargetName() const;
	std::string CurrentModeName() const;
	std::string CurrentStatus() const;

	void BuildControls();
	void RefreshAvailableTargets();
	void RefreshControls();
	void RefreshLightControls();
	bool RefreshFromLine(bool allow_prompt);
	LoadDecision ConfirmLoadExisting(std::string const& target_label);
	void ResetDefaultGradient();
	bool LoadTagValue(std::string const& value, bool placement = false);
	std::string FormatAttachedTagValue() const;
	std::string FormatTagValue() const;
	bool PlacementSupported() const;
	bool PlacementTransformUnsupported() const;
	bool PlacementTagActive() const;
	bool HasVideo() const;
	bool IsLockedLine() const;
	void RefreshPlacementControl();
	void StartPlacementSession();
	void EndPlacementSession(bool restore_tool = true);
	void OnPlacementAccepted(mangetsu::PlacementRect const& rect);
	void OnPlacementInvalid();
	void OnPlacementCancelled();
	void OnPlacementToolDeactivated();
	void OnActiveLineChanged(AssDialogue *line);
	void OnFileCommit(int type, AssDialogue const* line);
	void OnPlacementToggle(wxCommandEvent&);
	void UpdateDirtyState();
	void ApplyCurrent(bool mark_dirty = true);
	void ClearCurrent();
	void ReverseStops();
	void StartAddStopPlacement();
	void CancelAddStopPlacement(bool show_status = true);
	void FinishAddStopPlacement(double pos);
	void AddStop(double pos);
	void RemoveSelectedStop();
	void FlatZone();
	void EditSelectedStop();
	GradientStop SampleAt(double pos) const;
	void SortStops();

	TagRange FindCurrentTag(AssDialogue const *line) const;
	static TagRange FindTag(std::string const& text, std::string const& tag, std::string const& alias = "");
	static std::string ReplaceOrInsertTag(std::string const& text, TagRef const& tag, std::string const& value);
	static std::vector<std::string> TokenizeTagValue(std::string const& value);
	static bool ParseAssColor(std::string const& text, agi::Color& color);
	static bool ParseAssAlpha(std::string const& text, int& alpha);
	static std::string FormatAssAlpha(int alpha);
	void InvalidateTagSpan();
	bool ReplaceCurrentTagInLine(std::string const& value);
	bool RemoveCurrentTagFromLine();

	int HitTestStop(wxPoint pos) const;
	double PosFromMouse(wxPoint pos) const;
	wxRect BarRect() const;
	wxRect StartSwatchRect() const;
	wxRect EndSwatchRect() const;

	void OnAngleChanged(wxCommandEvent&);
	void OnQuickAngle(int new_angle);
	void OnMainChoice(wxCommandEvent&);
	void OnBorderChoice(wxCommandEvent&);
	void OnMode(ChannelMode new_mode);
	void OnApply(wxCommandEvent&);
	void OnOK(wxCommandEvent&);
	void OnCancel(wxCommandEvent&);
	void CommitPreview(wxString const& message);
	void MarkGradientChanged(bool immediate = false, wxString const& message = wxString());
	void SchedulePreview(wxString const& message, bool immediate);
	void FlushPreview();
	void OnPreviewTimer(wxTimerEvent&);
	void OnDialogKeyDown(wxKeyEvent& event);
	void FinishDialog(int result);

public:
	DialogMangetsuGradient(wxWindow *parent, agi::Context *context);
	~DialogMangetsuGradient();
};

GradientStopBar::GradientStopBar(wxWindow *parent, DialogMangetsuGradient *dialog)
: wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(660, 120), wxBORDER_NONE)
, dialog(dialog)
{
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetMinSize(wxSize(560, 110));
	SetFocus();
	Bind(wxEVT_PAINT, &GradientStopBar::OnPaint, this);
	Bind(wxEVT_LEFT_DOWN, &GradientStopBar::OnMouse, this);
	Bind(wxEVT_LEFT_DCLICK, &GradientStopBar::OnMouse, this);
	Bind(wxEVT_LEFT_UP, &GradientStopBar::OnMouse, this);
	Bind(wxEVT_MOTION, &GradientStopBar::OnMouse, this);
	Bind(wxEVT_RIGHT_DOWN, &GradientStopBar::OnMouse, this);
	Bind(wxEVT_CHAR_HOOK, &GradientStopBar::OnKeyDown, this);
	Bind(wxEVT_MOUSE_CAPTURE_LOST, &GradientStopBar::OnMouseCaptureLost, this);
}

void GradientStopBar::OnPaint(wxPaintEvent&) {
	wxAutoBufferedPaintDC dc(this);
	dc.SetBackground(wxBrush(GetBackgroundColour()));
	dc.Clear();

	wxRect start = dialog->StartSwatchRect();
	wxRect end = dialog->EndSwatchRect();
	wxRect bar = dialog->BarRect();

	auto draw_checker = [&](wxRect rect) {
		int cell = 6;
		for (int y = rect.GetTop(); y <= rect.GetBottom(); y += cell) {
			for (int x = rect.GetLeft(); x <= rect.GetRight(); x += cell) {
				bool light = ((x / cell) + (y / cell)) % 2 == 0;
				dc.SetPen(*wxTRANSPARENT_PEN);
				dc.SetBrush(wxBrush(light ? wxColour(230, 230, 230) : wxColour(170, 170, 170)));
				dc.DrawRectangle(x, y, cell, cell);
			}
		}
	};

	auto stop_colour = [&](GradientStop const& stop) {
		if (dialog->mode == ChannelMode::Color)
			return to_wx_color(stop.color);
		int v = 255 - clamp_alpha(stop.alpha);
		return wxColour(v, v, v);
	};

	if (dialog->mode == ChannelMode::Alpha)
		draw_checker(bar);
	for (int x = 0; x < bar.GetWidth(); ++x) {
		double pos = bar.GetWidth() <= 1 ? 0.0 : 100.0 * x / (bar.GetWidth() - 1);
		dc.SetPen(wxPen(stop_colour(dialog->SampleAt(pos))));
		dc.DrawLine(bar.GetX() + x, bar.GetY(), bar.GetX() + x, bar.GetBottom());
	}
	dc.SetBrush(*wxTRANSPARENT_BRUSH);
	dc.SetPen(wxPen(wxColour(20, 20, 20), 1));
	dc.DrawRectangle(bar);

	auto draw_swatch = [&](wxRect rect, GradientStop const& stop, bool selected) {
		if (dialog->mode == ChannelMode::Alpha)
			draw_checker(rect);
		dc.SetBrush(wxBrush(stop_colour(stop)));
		dc.SetPen(wxPen(wxColour(0, 0, 0), 1));
		dc.DrawRectangle(rect);
		if (selected) {
			dc.SetBrush(*wxTRANSPARENT_BRUSH);
			dc.SetPen(wxPen(wxColour(0, 120, 215), 3));
			dc.DrawRectangle(rect.Deflate(2, 2));
		}
	};

	if (!dialog->stops.empty()) {
		draw_swatch(start, dialog->stops.front(), dialog->selected_stop == 0);
		draw_swatch(end, dialog->stops.back(), dialog->selected_stop == static_cast<int>(dialog->stops.size()) - 1);
	}

	for (size_t i = 0; i < dialog->stops.size(); ++i) {
		double t = dialog->stops[i].pos / 100.0;
		int x = bar.GetX() + static_cast<int>(std::lround(t * bar.GetWidth()));
		wxRect swatch(x - 13, bar.GetY() - 34, 26, 24);
		dc.SetPen(wxPen(wxColour(0, 0, 0), 2));
		dc.DrawLine(x, bar.GetY() - 8, x, bar.GetBottom() + 8);
		dc.SetBrush(wxBrush(stop_colour(dialog->stops[i])));
		dc.SetPen(wxPen(wxColour(0, 0, 0), 1));
		dc.DrawRectangle(swatch);
		if (static_cast<int>(i) == dialog->selected_stop) {
			dc.SetBrush(*wxTRANSPARENT_BRUSH);
			dc.SetPen(wxPen(wxColour(0, 120, 215), 3));
			dc.DrawRectangle(swatch.Deflate(1, 1));
		}
	}

	if (marker_visible && (add_placement || dragging)) {
		// Draw an under-stroke so the small blue placement tip remains legible
		// over both light and dark gradient colours.
		wxRect marker(marker_pos.x - STOP_PLACEMENT_MARKER_SIZE / 2,
			marker_pos.y - STOP_PLACEMENT_MARKER_SIZE / 2,
			STOP_PLACEMENT_MARKER_SIZE, STOP_PLACEMENT_MARKER_SIZE);
		dc.SetBrush(*wxTRANSPARENT_BRUSH);
		dc.SetPen(wxPen(wxColour(15, 35, 55), 4));
		dc.DrawRectangle(marker);
		dc.SetPen(wxPen(wxColour(55, 155, 255), 2));
		dc.DrawRectangle(marker);

		if (add_placement) {
			dc.SetPen(wxPen(wxColour(55, 155, 255), 1, wxPENSTYLE_DOT));
			dc.DrawLine(marker_pos.x, bar.GetTop(), marker_pos.x, bar.GetBottom());
			wxString percent = wxString::Format("%.1f%%", dialog->PosFromMouse(marker_pos));
			dc.SetTextForeground(wxColour(35, 125, 220));
			dc.DrawText(percent, marker_pos.x + 8, std::max(0, marker_pos.y - 18));
		}
	}
}

void GradientStopBar::OnMouse(wxMouseEvent& event) {
	SetFocus();
	wxPoint pos = event.GetPosition();

	if (event.Leaving() && !add_pressing && !dragging) {
		marker_visible = false;
		RestoreCursor();
		Refresh(false);
		return;
	}

	if (add_placement) {
		if (event.RightDown()) {
			dialog->CancelAddStopPlacement();
			return;
		}

		if (event.Moving() || event.Dragging() || event.Entering())
			UpdateMarker(pos, add_pressing);

		if (event.LeftDown()) {
			if (!InPlacementArea(pos)) {
				dialog->add_stop_status = _("Move over the gradient strip to place the new stop.");
				dialog->RefreshLightControls();
				return;
			}
			add_pressing = true;
			UpdateMarker(pos, true);
			if (!HasCapture())
				CaptureMouse();
			return;
		}

		if (event.LeftUp() && add_pressing) {
			add_pressing = false;
			UpdateMarker(pos, true);
			if (HasCapture())
				ReleaseMouse();
			dialog->FinishAddStopPlacement(dialog->PosFromMouse(pos));
			return;
		}
		return;
	}

	int hit = dialog->HitTestStop(pos);

	if (event.RightDown() && hit >= 0) {
		dialog->selected_stop = hit;
		dialog->RefreshLightControls();
		return;
	}

	if (event.LeftDClick() && hit >= 0) {
		dialog->selected_stop = hit;
		dialog->EditSelectedStop();
		return;
	}

	if (event.LeftDown()) {
		if (hit >= 0) {
			dialog->selected_stop = hit;
			dragging = dialog->selected_stop > 0 && dialog->selected_stop < static_cast<int>(dialog->stops.size()) - 1;
			if (dragging)
				CaptureMouse();
			dialog->RefreshLightControls();
			return;
		}
		if (event.CmdDown() || event.ControlDown()) {
			dialog->AddStop(dialog->PosFromMouse(pos));
			return;
		}
	}

	if (event.Dragging() && event.LeftIsDown() && dragging) {
		UpdateMarker(pos, true);
		dialog->stops[dialog->selected_stop].pos = dialog->PosFromMouse(pos);
		dialog->SortStops();
		dialog->MarkGradientChanged(false);
		return;
	}

	if (event.LeftUp() && dragging) {
		dragging = false;
		marker_visible = false;
		if (HasCapture())
			ReleaseMouse();
		dialog->FlushPreview();
		dialog->RefreshLightControls();
	}
}

void GradientStopBar::OnKeyDown(wxKeyEvent& event) {
	if (event.GetKeyCode() == WXK_ESCAPE && add_placement) {
		dialog->CancelAddStopPlacement();
		return;
	}
	if (event.GetKeyCode() == WXK_DELETE) {
		dialog->RemoveSelectedStop();
		return;
	}
	event.Skip();
}

void GradientStopBar::OnMouseCaptureLost(wxMouseCaptureLostEvent&) {
	dragging = false;
	add_pressing = false;
	marker_visible = false;
	RestoreCursor();
	if (add_placement)
		dialog->CancelAddStopPlacement();
	else
		Refresh(false);
}

wxPoint GradientStopBar::SnapMarkerY(wxPoint pos) const {
	wxRect const bar = dialog->BarRect();
	int const top_distance = std::abs(pos.y - bar.GetTop());
	int const bottom_distance = std::abs(pos.y - bar.GetBottom());
	if (std::min(top_distance, bottom_distance) <= STOP_PLACEMENT_Y_SNAP_THRESHOLD)
		pos.y = top_distance <= bottom_distance ? bar.GetTop() : bar.GetBottom();
	return pos;
}

bool GradientStopBar::InPlacementArea(wxPoint pos) const {
	wxRect area = dialog->BarRect();
	area.Inflate(0, STOP_PLACEMENT_Y_SNAP_THRESHOLD);
	return area.Contains(pos);
}

void GradientStopBar::UpdateMarker(wxPoint pos, bool force) {
	marker_visible = force || InPlacementArea(pos);
	if (marker_visible) {
		marker_pos = SnapMarkerY(pos); // Deliberately leaves X completely untouched.
		if (add_placement)
			SetCursor(wxCursor(wxCURSOR_BLANK));
	}
	else
		RestoreCursor();
	Refresh(false);
}

void GradientStopBar::RestoreCursor() {
	SetCursor(wxNullCursor);
}

void GradientStopBar::BeginAddPlacement() {
	if (add_placement)
		return;
	add_placement = true;
	add_pressing = false;
	marker_visible = false;
	SetFocus();
	Refresh(false);
}

void GradientStopBar::CancelAddPlacement() {
	add_placement = false;
	add_pressing = false;
	marker_visible = false;
	if (HasCapture())
		ReleaseMouse();
	RestoreCursor();
	Refresh(false);
}

DialogMangetsuGradient::DialogMangetsuGradient(wxWindow *parent, agi::Context *context)
: wxDialog(parent, wxID_ANY, _("Mangetsu Gradient Editor"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
, context(context)
{
	active_line = context && context->selectionController ? context->selectionController->GetActiveLine() : nullptr;
	if (active_line)
		original_text = active_line->Text.get();

	ResetDefaultGradient();
	BuildControls();
	RefreshAvailableTargets();
	RefreshFromLine(false);
	RefreshControls();
	if (context && context->selectionController)
		active_line_connection = context->selectionController->AddActiveLineListener(&DialogMangetsuGradient::OnActiveLineChanged, this);
	if (context && context->ass)
		file_commit_connection = context->ass->AddCommitListener(&DialogMangetsuGradient::OnFileCommit, this);
	if (lock_placement && placement_rect.valid)
		StartPlacementSession();
}

DialogMangetsuGradient::~DialogMangetsuGradient() {
	if (stop_bar)
		stop_bar->CancelAddPlacement();
	EndPlacementSession();
}

void DialogMangetsuGradient::BuildControls() {
	auto *root = new wxBoxSizer(wxVERTICAL);
	auto *top = new wxBoxSizer(wxHORIZONTAL);

	auto *angle_label = new wxStaticText(this, wxID_ANY, _("angle"));
	angle_ctrl = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 0, 359, 0);
	top->Add(angle_label, wxSizerFlags().Center().Border(wxRIGHT, 4));
	top->Add(angle_ctrl, wxSizerFlags().Center().Border(wxRIGHT, 8));

	struct QuickAngle {
		const char *label;
		int angle;
	};
	static const QuickAngle quick_angles[] = {
		{"\xE2\x86\x92", 0},
		{"\xE2\x86\x93", 90},
		{"\xE2\x86\x91", 270},
		{"\xE2\x86\x96", 225},
		{"\xE2\x86\x97", 315},
		{"\xE2\x86\x99", 135},
		{"\xE2\x86\x98", 45}
	};

	for (auto const& quick : quick_angles) {
		auto *button = new wxButton(this, wxID_ANY, wxString::FromUTF8(quick.label), wxDefaultPosition, wxSize(34, -1));
		button->Bind(wxEVT_BUTTON, [=](wxCommandEvent&) { OnQuickAngle(quick.angle); });
		top->Add(button, wxSizerFlags().Center().Border(wxRIGHT, 2));
	}

	lock_placement_button = new wxToggleButton(this, wxID_ANY, _("Lock Placement"));
	lock_placement_button->SetToolTip(_("Lock the gradient to a fixed area of the video.\nEnable this, then drag the area in the video preview.\nOutside the area, Mangetsu uses the line's normal primary color."));
	top->Add(lock_placement_button, wxSizerFlags().Center().Border(wxRIGHT, 8));

	top->AddSpacer(12);
	main_choice = new wxChoice(this, wxID_ANY);
	border_choice = new wxChoice(this, wxID_ANY);
	box_choice = new wxChoice(this, wxID_ANY);
	box_choice->Append(_("No box gradients"));
	box_choice->SetSelection(0);
	box_choice->Enable(false);
	box_choice->SetToolTip(_("Box gradient tags are not supported by this branch."));

	top->Add(main_choice, wxSizerFlags().Center().Border(wxRIGHT, 4));
	top->Add(border_choice, wxSizerFlags().Center().Border(wxRIGHT, 4));
	top->Add(box_choice, wxSizerFlags().Center().Border(wxRIGHT, 8));

	color_button = new wxToggleButton(this, wxID_ANY, _("Color"));
	alpha_button = new wxToggleButton(this, wxID_ANY, _("Alpha"));
	top->Add(color_button, wxSizerFlags().Center().Border(wxRIGHT, 2));
	top->Add(alpha_button, wxSizerFlags().Center());

	stop_bar = new GradientStopBar(this, this);
	status_label = new wxStaticText(this, wxID_ANY, wxEmptyString);

	auto *ops = new wxBoxSizer(wxHORIZONTAL);
	add_button = new wxButton(this, wxID_ANY, _("+ Stop"));
	add_button->SetToolTip(_("Choose a position for a new stop on the gradient strip."));
	remove_button = new wxButton(this, wxID_ANY, _("Remove"));
	auto *reverse_button = new wxButton(this, wxID_ANY, _("Reverse"));
	auto *flat_button = new wxButton(this, wxID_ANY, _("Flat Zone"));
	auto *clear_button = new wxButton(this, wxID_ANY, _("Clear"));
	ops->Add(add_button, wxSizerFlags().Border(wxRIGHT, 4));
	ops->Add(remove_button, wxSizerFlags().Border(wxRIGHT, 4));
	ops->Add(reverse_button, wxSizerFlags().Border(wxRIGHT, 4));
	ops->Add(flat_button, wxSizerFlags().Border(wxRIGHT, 4));
	ops->Add(clear_button, wxSizerFlags().Border(wxRIGHT, 4));

	auto *buttons = new wxBoxSizer(wxHORIZONTAL);
	auto *ok = new wxButton(this, wxID_OK, _("OK"));
	auto *apply = new wxButton(this, wxID_APPLY, _("Apply"));
	auto *cancel = new wxButton(this, wxID_CANCEL, _("Cancel"));
	buttons->AddStretchSpacer(1);
	buttons->Add(ok, wxSizerFlags().Border(wxRIGHT, 4));
	buttons->Add(apply, wxSizerFlags().Border(wxRIGHT, 4));
	buttons->Add(cancel);

	root->Add(top, wxSizerFlags().Expand().Border(wxALL, 8));
	root->Add(stop_bar, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, 8));
	root->Add(status_label, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, 8));
	root->Add(ops, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, 8));
	root->Add(buttons, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, 8));
	SetSizerAndFit(root);
	SetMinSize(GetSize());

	angle_ctrl->Bind(wxEVT_SPINCTRL, &DialogMangetsuGradient::OnAngleChanged, this);
	angle_ctrl->Bind(wxEVT_TEXT, &DialogMangetsuGradient::OnAngleChanged, this);
	main_choice->Bind(wxEVT_CHOICE, &DialogMangetsuGradient::OnMainChoice, this);
	border_choice->Bind(wxEVT_CHOICE, &DialogMangetsuGradient::OnBorderChoice, this);
	color_button->Bind(wxEVT_TOGGLEBUTTON, [=](wxCommandEvent&) { OnMode(ChannelMode::Color); });
	alpha_button->Bind(wxEVT_TOGGLEBUTTON, [=](wxCommandEvent&) { OnMode(ChannelMode::Alpha); });
	lock_placement_button->Bind(wxEVT_TOGGLEBUTTON, &DialogMangetsuGradient::OnPlacementToggle, this);
	add_button->Bind(wxEVT_BUTTON, [=](wxCommandEvent&) { StartAddStopPlacement(); });
	remove_button->Bind(wxEVT_BUTTON, [=](wxCommandEvent&) {
		if (stop_bar->IsAdding()) CancelAddStopPlacement(false);
		RemoveSelectedStop();
	});
	reverse_button->Bind(wxEVT_BUTTON, [=](wxCommandEvent&) {
		if (stop_bar->IsAdding()) CancelAddStopPlacement(false);
		ReverseStops();
	});
	flat_button->Bind(wxEVT_BUTTON, [=](wxCommandEvent&) {
		if (stop_bar->IsAdding()) CancelAddStopPlacement(false);
		FlatZone();
	});
	clear_button->Bind(wxEVT_BUTTON, [=](wxCommandEvent&) {
		if (stop_bar->IsAdding()) CancelAddStopPlacement(false);
		ClearCurrent();
	});
	ok->Bind(wxEVT_BUTTON, &DialogMangetsuGradient::OnOK, this);
	apply->Bind(wxEVT_BUTTON, &DialogMangetsuGradient::OnApply, this);
	cancel->Bind(wxEVT_BUTTON, &DialogMangetsuGradient::OnCancel, this);
	preview_timer.Bind(wxEVT_TIMER, &DialogMangetsuGradient::OnPreviewTimer, this);
	Bind(wxEVT_CHAR_HOOK, &DialogMangetsuGradient::OnDialogKeyDown, this);
	Bind(wxEVT_CLOSE_WINDOW, [=](wxCloseEvent&) {
		wxCommandEvent evt;
		OnCancel(evt);
	});
}

void DialogMangetsuGradient::RefreshAvailableTargets() {
	border_layers.clear();
	border_layers.push_back(1);
	std::string text = active_line ? active_line->Text.get() : std::string();
	for (int layer = 2; layer <= 10; ++layer) {
		if (has_layer_tag(text, layer))
			border_layers.push_back(layer);
	}
}

void DialogMangetsuGradient::RefreshControls() {
	updating_controls = true;

	angle_ctrl->SetValue(angle);

	auto mark = [&](std::string label, TagRef const& tag) {
		(void)tag;
		return FindCurrentTag(active_line) ? label + " *" : label;
	};

	main_choice->Clear();
	for (int idx : {1, 2, 3, 4, 5}) {
		std::string label;
		switch (idx) {
			case 1: label = "Primary"; break;
			case 2: label = "Secondary"; break;
			case 3: label = "Border (1)"; break;
			case 4: label = "Shadow"; break;
			case 5: label = "Fifth"; break;
		}
		TargetGroup saved_group = group;
		int saved_main = main_index;
		group = TargetGroup::Main;
		main_index = idx;
		main_choice->Append(to_wx(mark(label, CurrentTag())));
		group = saved_group;
		main_index = saved_main;
	}
	main_choice->SetSelection(std::max(0, main_index - 1));

	border_choice->Clear();
	int border_selection = 0;
	for (size_t i = 0; i < border_layers.size(); ++i) {
		int saved_border = border_index;
		TargetGroup saved_group = group;
		group = TargetGroup::Border;
		border_index = border_layers[i];
		border_choice->Append(to_wx(mark(agi::format("Border %d", border_layers[i]), CurrentTag())));
		group = saved_group;
		border_index = saved_border;
		if (border_layers[i] == border_index)
			border_selection = static_cast<int>(i);
	}
	border_choice->SetSelection(border_selection);

	TargetGroup saved_group = group;
	ChannelMode saved_mode = mode;
	mode = ChannelMode::Color;
	color_button->SetLabel(to_wx(FindCurrentTag(active_line) ? "Color *" : "Color"));
	mode = ChannelMode::Alpha;
	alpha_button->SetLabel(to_wx(FindCurrentTag(active_line) ? "Alpha *" : "Alpha"));
	group = saved_group;
	mode = saved_mode;

	color_button->SetValue(mode == ChannelMode::Color);
	alpha_button->SetValue(mode == ChannelMode::Alpha);
	status_label->SetLabel(to_wx(CurrentStatus()));
	remove_button->Enable(selected_stop > 0 && selected_stop < static_cast<int>(stops.size()) - 1);
	RefreshPlacementControl();

	updating_controls = false;
	if (stop_bar)
		stop_bar->Refresh(false);
	Layout();
}

void DialogMangetsuGradient::RefreshLightControls() {
	updating_controls = true;
	if (angle_ctrl && angle_ctrl->GetValue() != angle)
		angle_ctrl->SetValue(angle);
	if (color_button) {
		if (mode == ChannelMode::Color)
			color_button->SetLabel(existing ? _("Color *") : _("Color"));
		color_button->SetValue(mode == ChannelMode::Color);
	}
	if (alpha_button) {
		if (mode == ChannelMode::Alpha)
			alpha_button->SetLabel(existing ? _("Alpha *") : _("Alpha"));
		alpha_button->SetValue(mode == ChannelMode::Alpha);
	}
	if (status_label)
		status_label->SetLabel(to_wx(CurrentStatus()));
	RefreshPlacementControl();
	if (remove_button)
		remove_button->Enable(selected_stop > 0 && selected_stop < static_cast<int>(stops.size()) - 1);
	updating_controls = false;

	if (stop_bar)
		stop_bar->Refresh(false);
}

bool DialogMangetsuGradient::RefreshFromLine(bool allow_prompt) {
	if (!active_line)
		return true;

	InvalidateTagSpan();
	TagRange found = FindCurrentTag(active_line);
	if (found) {
		tag_span_valid = true;
		tag_span_start = found.start;
		tag_span_end = found.end;
		if (dirty && allow_prompt) {
			LoadDecision decision = ConfirmLoadExisting(CurrentTargetName() + " " + CurrentModeName());
			if (decision == LoadDecision::Cancel) {
				RefreshControls();
				return false;
			}
			if (decision == LoadDecision::Keep) {
				existing = true;
				ApplyCurrent(false);
				return true;
			}
		}
		if (LoadTagValue(found.value, mangetsu::IsPlacementGradientTagName(found.name))) {
			loaded_value = FormatTagValue();
			dirty = false;
			existing = true;
			RefreshControls();
			return true;
		}
	}

	existing = false;
	lock_placement = false;
	InvalidateTagSpan();
	if (!dirty) {
		ResetDefaultGradient();
		placement_rect = {};
		loaded_value = FormatTagValue();
	}
	RefreshControls();
	return true;
}

LoadDecision DialogMangetsuGradient::ConfirmLoadExisting(std::string const& target_label) {
	wxMessageDialog dlg(this,
		to_wx("Existing gradient found for " + target_label + ".\nLoad existing gradient or keep current edit?"),
		_("Mangetsu Gradient Editor"),
		wxYES_NO | wxCANCEL | wxICON_QUESTION);
	dlg.SetYesNoCancelLabels(_("Load Existing"), _("Keep Current Edit"), _("Cancel"));
	int ret = dlg.ShowModal();
	if (ret == wxID_CANCEL)
		return LoadDecision::Cancel;
	return ret == wxID_YES ? LoadDecision::Load : LoadDecision::Keep;
}

TagRef DialogMangetsuGradient::CurrentTag() const {
	if (group == TargetGroup::Main) {
		if (mode == ChannelMode::Color)
			return {"\\" + std::to_string(main_index) + "grd", main_index == 3 ? "\\1bgrd" : "", "\\" + std::to_string(main_index) + "grd"};
		return {"\\" + std::to_string(main_index) + "gra", main_index == 3 ? "\\1bga" : "", "\\" + std::to_string(main_index) + "gra"};
	}

	if (mode == ChannelMode::Color)
		return {"\\" + std::to_string(border_index) + "bgrd", border_index == 1 ? "\\3grd" : "", "\\" + std::to_string(border_index) + "bgrd"};
	return {"\\" + std::to_string(border_index) + "bga", border_index == 1 ? "\\3gra" : "", "\\" + std::to_string(border_index) + "bga"};
}

TagRef DialogMangetsuGradient::PlacementTag() const {
	return {"\\pgrd", "\\1pgrd", "\\pgrd"};
}

TagRef DialogMangetsuGradient::OutputTag() const {
	return PlacementTagActive() ? PlacementTag() : CurrentTag();
}

bool DialogMangetsuGradient::PlacementSupported() const {
	// Keep this small capability table explicit: Mangetsu currently has only
	// primary RGB fixed-frame gradients, but adding a renderer channel later is
	// a one-entry change instead of a rewrite of the dialog.
	struct Capability { TargetGroup group; ChannelMode mode; int main; };
	static Capability const capabilities[] = {
		{TargetGroup::Main, ChannelMode::Color, 1},
	};
	for (auto const& capability : capabilities) {
		if (group == capability.group && mode == capability.mode && main_index == capability.main)
			return true;
	}
	return false;
}

bool DialogMangetsuGradient::PlacementTransformUnsupported() const {
	if (!PlacementSupported() || !active_line || FindCurrentTag(active_line))
		return false;
	TagRef attached = CurrentTag();
	TagRef placed = PlacementTag();
	auto has_tag = [&](std::string const& text, TagRef const& tag) {
		return text.find(tag.name) != std::string::npos ||
			(!tag.alias.empty() && text.find(tag.alias) != std::string::npos);
	};
	std::string const& text = active_line->Text.get();
	for (int pos = 0; (pos = static_cast<int>(text.find("\\t(", pos))) >= 0; ++pos) {
		int const open = pos + 2;
		int const close = matching_paren(text, open);
		if (close < 0)
			continue;
		std::string const transform = text.substr(open + 1, close - open - 1);
		if (has_tag(transform, attached) || has_tag(transform, placed))
			return true;
		pos = close;
	}
	return false;
}

bool DialogMangetsuGradient::PlacementTagActive() const {
	return PlacementSupported() && lock_placement && placement_rect.valid;
}

bool DialogMangetsuGradient::HasVideo() const {
	return context && context->videoDisplay && context->videoDisplay->HasVideo();
}

bool DialogMangetsuGradient::IsLockedLine() const {
	return active_line && trim_copy(active_line->Effect.get()) == "LOCK";
}

std::string DialogMangetsuGradient::CurrentGroupName() const {
	return group == TargetGroup::Main ? "Main" : "Border";
}

std::string DialogMangetsuGradient::CurrentTargetName() const {
	if (group == TargetGroup::Border)
		return agi::format("Border %d", border_index);

	switch (main_index) {
		case 1: return "Primary";
		case 2: return "Secondary";
		case 3: return "Border (1)";
		case 4: return "Shadow";
		case 5: return "Fifth";
		default: return "Main";
	}
}

std::string DialogMangetsuGradient::CurrentModeName() const {
	return mode == ChannelMode::Color ? "Color" : "Alpha";
}

std::string DialogMangetsuGradient::CurrentStatus() const {
	TagRef tag = OutputTag();
	std::string status = "Editing: " + CurrentGroupName() + " / " + CurrentTargetName() + " / " + CurrentModeName() +
		" -> " + tag.status_name + "\nExisting: " + (existing ? "yes" : "no");
	if (!placement_status.empty())
		status += "\n" + from_wx(placement_status);
	if (!add_stop_status.empty())
		status += "\n" + from_wx(add_stop_status);
	return status;
}

void DialogMangetsuGradient::ResetDefaultGradient() {
	angle = 0;
	selected_stop = 0;
	GradientStop start;
	start.pos = 0.0;
	start.color = agi::Color(0, 0, 0);
	start.alpha = 0x00;
	GradientStop end;
	end.pos = 100.0;
	end.color = agi::Color(255, 255, 255);
	end.alpha = 0xFF;
	stops = {start, end};
}

bool DialogMangetsuGradient::LoadTagValue(std::string const& value, bool placement) {
	std::string attached_value = value;
	if (placement) {
		if (!mangetsu::ParsePlacementGradientValue(value, placement_rect, attached_value))
			return false;
		lock_placement = true;
	}
	else {
		placement_rect = {};
		lock_placement = false;
	}

	auto tokens = TokenizeTagValue(attached_value);
	if (tokens.size() < 3)
		return false;

	try {
		angle = std::lround(std::stod(tokens[0]));
	}
	catch (...) {
		return false;
	}
	angle %= 360;
	if (angle < 0)
		angle += 360;

	std::vector<GradientStop> parsed;
	for (size_t i = 1; i < tokens.size(); ++i) {
		double pos = parsed.empty() ? 0.0 : 100.0;
		std::string token = tokens[i];
		if (ends_with_percent(token)) {
			if (i + 1 >= tokens.size())
				return false;
			try {
				pos = std::stod(token.substr(0, token.size() - 1));
			}
			catch (...) {
				return false;
			}
			token = tokens[++i];
		}

		GradientStop stop;
		stop.pos = clamp_percent(pos);
		if (mode == ChannelMode::Color) {
			if (!ParseAssColor(token, stop.color))
				return false;
			stop.alpha = 0;
		}
		else {
			if (!ParseAssAlpha(token, stop.alpha))
				return false;
			stop.color = agi::Color(255, 255, 255);
		}
		parsed.push_back(stop);
	}

	if (parsed.size() < 2)
		return false;
	parsed.front().pos = 0.0;
	parsed.back().pos = 100.0;
	stops = std::move(parsed);
	SortStops();
	selected_stop = 0;
	return true;
}

std::string DialogMangetsuGradient::FormatTagValue() const {
	std::string attached = FormatAttachedTagValue();
	return PlacementTagActive() ? mangetsu::FormatPlacementGradientValue(placement_rect, attached) : attached;
}

std::string DialogMangetsuGradient::FormatAttachedTagValue() const {
	std::string value = "(" + std::to_string(angle);
	for (size_t i = 0; i < stops.size(); ++i) {
		value += ",";
		if (i != 0 && i + 1 != stops.size())
			value += agi::format("%g%%,", stops[i].pos);
		value += mode == ChannelMode::Color ? stops[i].color.GetAssOverrideFormatted() : FormatAssAlpha(stops[i].alpha);
	}
	value += ")";
	return value;
}

void DialogMangetsuGradient::RefreshPlacementControl() {
	if (!lock_placement_button)
		return;

	bool const supported = PlacementSupported();
	if (!supported || PlacementTransformUnsupported()) {
		lock_placement = false;
		lock_placement_button->SetValue(false);
		lock_placement_button->Enable(false);
		lock_placement_button->SetToolTip(supported ?
			_("The current Gradient Editor does not edit gradients inside transforms.") :
			_("Placement gradients currently support only the primary fill color."));
		return;
	}

	lock_placement_button->SetValue(lock_placement);
	bool const interactive = HasVideo();
	lock_placement_button->Enable(interactive || placement_rect.valid);
	if (!interactive && !placement_rect.valid)
		lock_placement_button->SetToolTip(_("A loaded video is required to choose a fixed gradient area."));
	else
		lock_placement_button->SetToolTip(_("Lock the gradient to a fixed area of the video.\nEnable this, then drag the area in the video preview.\nOutside the area, Mangetsu uses the line's normal primary color."));
}

void DialogMangetsuGradient::StartPlacementSession() {
	if (placement_capture_active || !lock_placement || !PlacementSupported())
		return;
	if (IsLockedLine()) {
		placement_status = _("The active line is locked; placement editing is unavailable.");
		RefreshLightControls();
		return;
	}
	if (!HasVideo()) {
		placement_status = _("Load a video to choose or replace the fixed gradient area.");
		RefreshLightControls();
		return;
	}

	auto *display = context->videoDisplay;
	auto previous = display->TakeTool();
	if (!previous) {
		placement_status = _("The video tool is not ready yet. Try again after the video preview has rendered.");
		RefreshLightControls();
		return;
	}

	auto session = std::make_shared<GradientPlacementSession>();
	session->previous_tool = std::move(previous);
	session->original_line = active_line;
	session->rectangle = placement_rect;
	session->pending_gradient_value = FormatTagValue();
	session->accepted = [this](mangetsu::PlacementRect const& rect) { OnPlacementAccepted(rect); };
	session->invalid_drag = [this] { OnPlacementInvalid(); };
	session->cancelled = [this] { OnPlacementCancelled(); };
	session->status = [this](char const* text) {
		placement_status = to_wx(text);
		RefreshLightControls();
	};
	session->deactivated = [this] { OnPlacementToolDeactivated(); };
	placement_session = session;
	placement_capture_active = true;
	display->SetTool(agi::make_unique<VisualToolGradientPlacement>(display, context, session));
	placement_status = placement_rect.valid ? _("Drag on the video to replace the fixed gradient area.") :
		_("Drag on the video to choose the fixed gradient area.");
	RefreshLightControls();
	display->Render();
}

void DialogMangetsuGradient::EndPlacementSession(bool restore_tool) {
	auto session = placement_session;
	placement_session.reset();
	placement_capture_active = false;
	if (!session)
		return;

	session->ending = true;
	session->accepted = nullptr;
	session->invalid_drag = nullptr;
	session->cancelled = nullptr;
	session->status = nullptr;
	session->deactivated = nullptr;

	auto *display = context ? context->videoDisplay : nullptr;
	if (restore_tool && display && display->ToolIsType(typeid(VisualToolGradientPlacement))) {
		auto placement_tool = display->TakeTool();
		// Destroying the temporary tool restores the normal cursor. Do it before
		// reattaching the retained tool so tools such as the crosshair can set
		// their own cursor again.
		placement_tool.reset();
		if (session->previous_tool)
			display->SetTool(std::move(session->previous_tool));
	}
	else
		session->previous_tool.reset();
}

void DialogMangetsuGradient::OnPlacementAccepted(mangetsu::PlacementRect const& rect) {
	if (!lock_placement || !PlacementSupported())
		return;
	placement_rect = rect;
	if (placement_session)
		placement_session->rectangle = rect;
	if (placement_session)
		placement_session->pending_gradient_value = FormatTagValue();
	placement_status = _("Fixed gradient area captured.");
	MarkGradientChanged(true, _("set gradient placement"));
}

void DialogMangetsuGradient::OnPlacementInvalid() {
	placement_status = _("Drag a non-zero rectangle in the visible video area.");
	RefreshLightControls();
}

void DialogMangetsuGradient::OnPlacementCancelled() {
	// The tool can be on its own event stack while this callback fires. Queue
	// restoration until it returns so it is never destroyed from inside its own
	// mouse/key handler.
	CallAfter([this] {
		EndPlacementSession();
		placement_status = _("Placement selection cancelled.");
		RefreshLightControls();
	});
}

void DialogMangetsuGradient::OnPlacementToolDeactivated() {
	if (!placement_session || placement_session->ending)
		return;
	placement_session->previous_tool.reset();
	placement_session.reset();
	placement_capture_active = false;
	placement_status = _("Placement selection ended because the video tool changed or the video was closed.");
	RefreshLightControls();
}

void DialogMangetsuGradient::OnActiveLineChanged(AssDialogue *line) {
	if (line == active_line)
		return;
	CancelAddStopPlacement(false);
	if (preview_pending) {
		preview_timer.Stop();
		preview_pending = false;
	}
	CallAfter([this] {
		EndPlacementSession();
		placement_status = _("Placement selection ended because the active subtitle line changed.");
		RefreshLightControls();
	});
}

void DialogMangetsuGradient::OnFileCommit(int type, AssDialogue const*) {
	if (type != AssFile::COMMIT_NEW)
		return;
	if (preview_pending) {
		preview_timer.Stop();
		preview_pending = false;
	}
	CallAfter([this] {
		EndPlacementSession();
		// The line pointer and original undo snapshot no longer belong to this
		// subtitle file. Close rather than attempting an undo into the new file.
		Destroy();
	});
}

void DialogMangetsuGradient::OnPlacementToggle(wxCommandEvent&) {
	if (updating_controls || !lock_placement_button)
		return;
	bool const requested_lock = lock_placement_button->GetValue();
	CancelAddStopPlacement(false);

	bool const was_placed = PlacementTagActive();
	if (!requested_lock) {
		EndPlacementSession();
		lock_placement = false;
		placement_status = _("Gradient placement unlocked.");
		if (was_placed || dirty)
			MarkGradientChanged(true, _("unlock gradient placement"));
		else
			RefreshLightControls();
		return;
	}

	if (!PlacementSupported() || PlacementTransformUnsupported()) {
		lock_placement = false;
		placement_status = PlacementTransformUnsupported() ?
			_("Placement gradients inside transforms are not editable in this dialog.") : wxString();
		RefreshLightControls();
		return;
	}
	if (IsLockedLine()) {
		lock_placement = false;
		placement_status = _("The active line is locked; placement editing is unavailable.");
		RefreshLightControls();
		return;
	}
	if (!HasVideo() && !placement_rect.valid) {
		lock_placement = false;
		placement_status = _("Load a video before choosing a fixed gradient area.");
		RefreshLightControls();
		return;
	}

	lock_placement = true;
	if (placement_rect.valid)
		MarkGradientChanged(true, _("lock gradient placement"));
	else
		RefreshLightControls();
	StartPlacementSession();
}

void DialogMangetsuGradient::UpdateDirtyState() {
	dirty = FormatTagValue() != loaded_value;
}

void DialogMangetsuGradient::CommitPreview(wxString const& message) {
	if (!context || !context->ass || !active_line)
		return;
	commit_id = context->ass->Commit(message, AssFile::COMMIT_DIAG_TEXT, commit_id, active_line);
	if (context->videoDisplay)
		context->videoDisplay->Render();
}

void DialogMangetsuGradient::MarkGradientChanged(bool immediate, wxString const& message) {
	UpdateDirtyState();
	if (placement_session)
		placement_session->pending_gradient_value = FormatTagValue();
	RefreshLightControls();
	SchedulePreview(message.IsEmpty() ? _("set gradient") : message, immediate);
}

void DialogMangetsuGradient::SchedulePreview(wxString const& message, bool immediate) {
	preview_message = message;
	if (immediate) {
		FlushPreview();
		return;
	}

	if (!preview_pending) {
		preview_pending = true;
		preview_timer.Start(33, wxTIMER_ONE_SHOT);
	}
}

void DialogMangetsuGradient::FlushPreview() {
	if (preview_pending) {
		preview_timer.Stop();
		preview_pending = false;
	}

	if (!active_line)
		return;
	// Waiting for the first placement drag is deliberately non-mutating. The
	// controls may still be adjusted, but no ordinary fallback tag is inserted
	// until a valid rectangle exists.
	if (lock_placement && !placement_rect.valid)
		return;

	if (!ReplaceCurrentTagInLine(FormatTagValue()))
		return;

	existing = true;
	CommitPreview(preview_message.IsEmpty() ? _("set gradient") : preview_message);
	RefreshLightControls();
}

void DialogMangetsuGradient::OnPreviewTimer(wxTimerEvent&) {
	preview_pending = false;
	FlushPreview();
}

void DialogMangetsuGradient::OnDialogKeyDown(wxKeyEvent& event) {
	if (event.GetKeyCode() == WXK_ESCAPE && stop_bar && stop_bar->IsAdding()) {
		CancelAddStopPlacement();
		return;
	}
	event.Skip();
}

void DialogMangetsuGradient::ApplyCurrent(bool mark_dirty) {
	if (!active_line)
		return;
	if (lock_placement && !placement_rect.valid)
		return;
	if (mark_dirty)
		UpdateDirtyState();
	if (!ReplaceCurrentTagInLine(FormatTagValue())) {
		RefreshControls();
		return;
	}
	existing = true;
	CommitPreview(_("set gradient"));
	RefreshAvailableTargets();
	RefreshControls();
}

void DialogMangetsuGradient::ClearCurrent() {
	if (lock_placement && !placement_rect.valid) {
		placement_status = _("Drag a fixed gradient area before changing placement mode.");
		RefreshLightControls();
		return;
	}
	FlushPreview();
	if (!active_line || !FindCurrentTag(active_line))
		return;
	dirty = true;
	if (!RemoveCurrentTagFromLine())
		return;
	existing = false;
	CommitPreview(_("clear gradient"));
	RefreshControls();
}

void DialogMangetsuGradient::ReverseStops() {
	angle = (angle + 180) % 360;
	int old_selected = selected_stop;
	for (auto& stop : stops)
		stop.pos = 100.0 - stop.pos;
	std::reverse(stops.begin(), stops.end());
	SortStops();
	selected_stop = std::max(0, static_cast<int>(stops.size()) - 1 - old_selected);
	MarkGradientChanged(true);
}

void DialogMangetsuGradient::StartAddStopPlacement() {
	if (!stop_bar || !add_button)
		return;
	if (stop_bar->IsAdding()) {
		CancelAddStopPlacement();
		return;
	}

	stop_bar->BeginAddPlacement();
	add_button->SetLabel(_("Cancel Add"));
	add_stop_status = _("Add stop: click or drag on the gradient strip; Esc or right-click cancels.");
	RefreshLightControls();
	Layout();
}

void DialogMangetsuGradient::CancelAddStopPlacement(bool show_status) {
	if (stop_bar)
		stop_bar->CancelAddPlacement();
	if (add_button)
		add_button->SetLabel(_("+ Stop"));
	add_stop_status = show_status ? _("Stop placement cancelled.") : wxString();
	RefreshLightControls();
	Layout();
}

void DialogMangetsuGradient::FinishAddStopPlacement(double pos) {
	if (stop_bar)
		stop_bar->CancelAddPlacement();
	if (add_button)
		add_button->SetLabel(_("+ Stop"));
	add_stop_status = wxString::Format(_("Stop placed at %.1f%%."), clamp_percent(pos));
	AddStop(pos);
	Layout();
}

void DialogMangetsuGradient::AddStop(double pos) {
	GradientStop stop = SampleAt(pos);
	stop.pos = clamp_percent(pos);
	stops.push_back(stop);
	SortStops();
	// SortStops is stable, so the newly appended stop is the last matching
	// position even when the user deliberately places it over another stop.
	for (size_t i = stops.size(); i-- > 0;) {
		if (std::abs(stops[i].pos - stop.pos) < 0.01) {
			selected_stop = static_cast<int>(i);
			break;
		}
	}
	MarkGradientChanged(true);
}

void DialogMangetsuGradient::RemoveSelectedStop() {
	if (selected_stop <= 0 || selected_stop >= static_cast<int>(stops.size()) - 1)
		return;
	stops.erase(stops.begin() + selected_stop);
	selected_stop = std::min<int>(selected_stop, static_cast<int>(stops.size()) - 1);
	MarkGradientChanged(true);
}

void DialogMangetsuGradient::FlatZone() {
	if (selected_stop < 0 || selected_stop >= static_cast<int>(stops.size()))
		return;

	GradientStop dup = stops[selected_stop];
	if (selected_stop == 0)
		dup.pos = std::min(99.0, stops[selected_stop].pos + 5.0);
	else if (selected_stop == static_cast<int>(stops.size()) - 1)
		dup.pos = std::max(1.0, stops[selected_stop].pos - 5.0);
	else
		dup.pos = std::min(99.0, stops[selected_stop].pos + 3.0);
	stops.push_back(dup);
	SortStops();
	MarkGradientChanged(true);
}

void DialogMangetsuGradient::EditSelectedStop() {
	if (selected_stop < 0 || selected_stop >= static_cast<int>(stops.size()))
		return;

	if (mode == ChannelMode::Color) {
		agi::Color selected = stops[selected_stop].color;
		bool ok = GetColorFromUser(this, selected, false, [&](agi::Color new_color) {
			stops[selected_stop].color = new_color;
			MarkGradientChanged(false);
		});
		if (!ok) {
			stops[selected_stop].color = selected;
			MarkGradientChanged(true);
		}
		else
			FlushPreview();
	}
	else {
		agi::Color selected(255, 255, 255, static_cast<unsigned char>(stops[selected_stop].alpha));
		bool ok = GetColorFromUser(this, selected, true, [&](agi::Color new_color) {
			stops[selected_stop].alpha = new_color.a;
			MarkGradientChanged(false);
		});
		if (!ok) {
			stops[selected_stop].alpha = selected.a;
			MarkGradientChanged(true);
		}
		else
			FlushPreview();
	}
}

GradientStop DialogMangetsuGradient::SampleAt(double pos) const {
	if (stops.empty())
		return GradientStop();
	pos = clamp_percent(pos);

	for (size_t i = 1; i < stops.size(); ++i) {
		if (pos <= stops[i].pos) {
			GradientStop left = stops[i - 1];
			GradientStop right = stops[i];
			double span = std::max(0.0001, right.pos - left.pos);
			double t = (pos - left.pos) / span;
			GradientStop out;
			out.pos = pos;
			out.color = lerp_color(left.color, right.color, t);
			out.alpha = clamp_alpha(std::lround(left.alpha + (right.alpha - left.alpha) * t));
			return out;
		}
	}
	GradientStop out = stops.back();
	out.pos = pos;
	return out;
}

void DialogMangetsuGradient::SortStops() {
	std::stable_sort(stops.begin(), stops.end(), [](GradientStop const& a, GradientStop const& b) {
		return a.pos < b.pos;
	});
	if (!stops.empty()) {
		stops.front().pos = 0.0;
		stops.back().pos = 100.0;
	}
}

TagRange DialogMangetsuGradient::FindCurrentTag(AssDialogue const *line) const {
	if (!line)
		return {};
	TagRef tag = CurrentTag();
	TagRange attached = FindTag(line->Text.get(), tag.name, tag.alias);
	if (!PlacementSupported())
		return attached;
	TagRef placed_tag = PlacementTag();
	TagRange placed = FindTag(line->Text.get(), placed_tag.name, placed_tag.alias);
	// Mangetsu uses the last valid primary source. Keep the editor targeting
	// rule aligned when an old attached and a placed tag coexist in one block.
	return placed && (!attached || placed.start > attached.start) ? placed : attached;
}

static int matching_paren(std::string const& text, int open) {
	int depth = 0;
	for (int i = open; i < static_cast<int>(text.size()); ++i) {
		if (text[i] == '(')
			++depth;
		else if (text[i] == ')') {
			--depth;
			if (depth == 0)
				return i;
		}
	}
	return -1;
}

TagRange DialogMangetsuGradient::FindTag(std::string const& text, std::string const& tag, std::string const& alias) {
	bool in_block = false;
	TagRange last_found;
	for (int i = 0; i < static_cast<int>(text.size()); ++i) {
		if (!in_block) {
			if (text[i] == '{')
				in_block = true;
			continue;
		}
		if (text[i] == '}') {
			in_block = false;
			continue;
		}
		if (text[i] != '\\')
			continue;

		int name_end = i + 1;
		while (name_end < static_cast<int>(text.size()) &&
			(std::isdigit(static_cast<unsigned char>(text[name_end])) || std::isalpha(static_cast<unsigned char>(text[name_end]))))
			++name_end;
		if (name_end == i + 1)
			continue;

		std::string name = text.substr(i, name_end - i);
		while (name_end < static_cast<int>(text.size()) && std::isspace(static_cast<unsigned char>(text[name_end])))
			++name_end;

		if (name == "\\t" && name_end < static_cast<int>(text.size()) && text[name_end] == '(') {
			int close = matching_paren(text, name_end);
			if (close >= 0)
				i = close;
			continue;
		}

		if (name != tag && (alias.empty() || name != alias))
			continue;

		int value_start = name_end;
		int value_end = value_start;
		if (value_start < static_cast<int>(text.size()) && text[value_start] == '(') {
			int close = matching_paren(text, value_start);
			if (close < 0)
				continue;
			value_end = close + 1;
		}
		else {
			while (value_end < static_cast<int>(text.size()) && text[value_end] != '\\' && text[value_end] != '}')
				++value_end;
		}

		// Edit the effective static tag: the last matching tag outside \t(...).
		last_found.start = i;
		last_found.end = value_end;
		last_found.name = name;
		last_found.value = text.substr(value_start, value_end - value_start);
		i = value_end - 1;
	}
	return last_found;
}

std::string DialogMangetsuGradient::ReplaceOrInsertTag(std::string const& text, TagRef const& tag, std::string const& value) {
	TagRange found = FindTag(text, tag.name, tag.alias);
	std::string replacement = tag.name + value;
	if (found) {
		std::string out = text;
		out.replace(found.start, found.end - found.start, replacement);
		return out;
	}

	if (!text.empty() && text[0] == '{') {
		int close = static_cast<int>(text.find('}'));
		if (close >= 0) {
			std::string out = text;
			out.insert(1, replacement);
			return out;
		}
	}

	return "{" + replacement + "}" + text;
}

void DialogMangetsuGradient::InvalidateTagSpan() {
	tag_span_valid = false;
	tag_span_start = -1;
	tag_span_end = -1;
}

bool DialogMangetsuGradient::ReplaceCurrentTagInLine(std::string const& value) {
	if (!active_line)
		return false;

	TagRef output = OutputTag();
	TagRef attached = CurrentTag();
	TagRef placed = PlacementTag();
	std::string replacement = output.name + value;
	std::string text = active_line->Text.get();

	auto span_matches = [&]() {
		if (!tag_span_valid || tag_span_start < 0 || tag_span_end < tag_span_start || tag_span_end > static_cast<int>(text.size()))
			return false;
		auto matches = [&](TagRef const& tag) {
			return text.compare(tag_span_start, tag.name.size(), tag.name) == 0 ||
				(!tag.alias.empty() && text.compare(tag_span_start, tag.alias.size(), tag.alias) == 0);
		};
		return matches(attached) || (PlacementSupported() && matches(placed));
	};

	if (span_matches()) {
		if (text.compare(tag_span_start, tag_span_end - tag_span_start, replacement) == 0)
			return false;
		text.replace(tag_span_start, tag_span_end - tag_span_start, replacement);
		tag_span_end = tag_span_start + static_cast<int>(replacement.size());
		active_line->Text = text;
		return true;
	}

	TagRange found = FindCurrentTag(active_line);
	if (found) {
		text.replace(found.start, found.end - found.start, replacement);
		tag_span_valid = true;
		tag_span_start = found.start;
		tag_span_end = found.start + static_cast<int>(replacement.size());
		active_line->Text = text;
		return true;
	}

	if (!text.empty() && text[0] == '{') {
		int close = static_cast<int>(text.find('}'));
		if (close >= 0) {
			text.insert(1, replacement);
			tag_span_valid = true;
			tag_span_start = 1;
			tag_span_end = 1 + static_cast<int>(replacement.size());
			active_line->Text = text;
			return true;
		}
	}

	text = "{" + replacement + "}" + text;
	tag_span_valid = true;
	tag_span_start = 1;
	tag_span_end = 1 + static_cast<int>(replacement.size());
	active_line->Text = text;
	return true;
}

bool DialogMangetsuGradient::RemoveCurrentTagFromLine() {
	if (!active_line)
		return false;

	TagRef attached = CurrentTag();
	TagRef placed = PlacementTag();
	std::string text = active_line->Text.get();

	auto span_matches = [&]() {
		if (!tag_span_valid || tag_span_start < 0 || tag_span_end < tag_span_start || tag_span_end > static_cast<int>(text.size()))
			return false;
		auto matches = [&](TagRef const& tag) {
			return text.compare(tag_span_start, tag.name.size(), tag.name) == 0 ||
				(!tag.alias.empty() && text.compare(tag_span_start, tag.alias.size(), tag.alias) == 0);
		};
		return matches(attached) || (PlacementSupported() && matches(placed));
	};

	int start = -1;
	int end = -1;
	if (span_matches()) {
		start = tag_span_start;
		end = tag_span_end;
	}
	else {
		TagRange found = FindCurrentTag(active_line);
		if (!found)
			return false;
		start = found.start;
		end = found.end;
	}

	text.erase(start, end - start);
	active_line->Text = text;
	InvalidateTagSpan();
	return true;
}

std::vector<std::string> DialogMangetsuGradient::TokenizeTagValue(std::string const& value) {
	std::vector<std::string> tokens;
	if (value.size() < 2 || value.front() != '(' || value.back() != ')')
		return tokens;

	int depth = 0;
	size_t start = 1;
	for (size_t i = 1; i + 1 < value.size(); ++i) {
		if (value[i] == '(')
			++depth;
		else if (value[i] == ')')
			--depth;
		else if (value[i] == ',' && depth == 0) {
			tokens.push_back(trim_copy(value.substr(start, i - start)));
			start = i + 1;
		}
	}
	tokens.push_back(trim_copy(value.substr(start, value.size() - 1 - start)));
	return tokens;
}

bool DialogMangetsuGradient::ParseAssColor(std::string const& text, agi::Color& color) {
	std::string s = trim_copy(text);
	size_t pos = s.find("&H");
	if (pos == std::string::npos)
		pos = s.find("&h");
	if (pos == std::string::npos)
		return false;
	pos += 2;
	size_t end = pos;
	while (end < s.size() && std::isxdigit(static_cast<unsigned char>(s[end])))
		++end;
	if (end == pos)
		return false;
	unsigned value = static_cast<unsigned>(std::stoul(s.substr(pos, end - pos), nullptr, 16));
	color = agi::Color(value & 0xFF, (value >> 8) & 0xFF, (value >> 16) & 0xFF);
	return true;
}

bool DialogMangetsuGradient::ParseAssAlpha(std::string const& text, int& alpha) {
	std::string s = trim_copy(text);
	size_t pos = s.find("&H");
	if (pos == std::string::npos)
		pos = s.find("&h");
	if (pos == std::string::npos)
		return false;
	pos += 2;
	size_t end = pos;
	while (end < s.size() && std::isxdigit(static_cast<unsigned char>(s[end])))
		++end;
	if (end == pos)
		return false;
	alpha = clamp_alpha(static_cast<int>(std::stoul(s.substr(pos, end - pos), nullptr, 16)));
	return true;
}

std::string DialogMangetsuGradient::FormatAssAlpha(int alpha) {
	return agi::format("&H%02X&", clamp_alpha(alpha));
}

wxRect DialogMangetsuGradient::BarRect() const {
	wxSize size = stop_bar->GetClientSize();
	return wxRect(82, 58, std::max(120, size.GetWidth() - 164), 34);
}

wxRect DialogMangetsuGradient::StartSwatchRect() const {
	return wxRect(18, 48, 44, 54);
}

wxRect DialogMangetsuGradient::EndSwatchRect() const {
	wxSize size = stop_bar->GetClientSize();
	return wxRect(size.GetWidth() - 62, 48, 44, 54);
}

int DialogMangetsuGradient::HitTestStop(wxPoint pos) const {
	wxRect bar = BarRect();
	for (int i = static_cast<int>(stops.size()) - 1; i >= 0; --i) {
		int x = bar.GetX() + static_cast<int>(std::lround(bar.GetWidth() * stops[i].pos / 100.0));
		if (std::abs(pos.x - x) <= 10 && pos.y >= bar.GetY() - 42 && pos.y <= bar.GetBottom() + 12)
			return i;
	}
	return -1;
}

double DialogMangetsuGradient::PosFromMouse(wxPoint pos) const {
	wxRect bar = BarRect();
	if (bar.GetWidth() <= 0)
		return 0.0;
	return clamp_percent(100.0 * (pos.x - bar.GetX()) / bar.GetWidth());
}

void DialogMangetsuGradient::OnAngleChanged(wxCommandEvent&) {
	if (updating_controls)
		return;
	angle = angle_ctrl->GetValue() % 360;
	MarkGradientChanged(false);
}

void DialogMangetsuGradient::OnQuickAngle(int new_angle) {
	angle = ((new_angle % 360) + 360) % 360;
	MarkGradientChanged(true);
}

void DialogMangetsuGradient::OnMainChoice(wxCommandEvent&) {
	if (updating_controls)
		return;
	CancelAddStopPlacement(false);
	EndPlacementSession();
	FlushPreview();
	TargetGroup old_group = group;
	int old_main = main_index;
	group = TargetGroup::Main;
	main_index = main_choice->GetSelection() + 1;
	InvalidateTagSpan();
	if (!RefreshFromLine(true)) {
		group = old_group;
		main_index = old_main;
		InvalidateTagSpan();
		RefreshControls();
	}
	else if (lock_placement && placement_rect.valid)
		StartPlacementSession();
}

void DialogMangetsuGradient::OnBorderChoice(wxCommandEvent&) {
	if (updating_controls)
		return;
	CancelAddStopPlacement(false);
	EndPlacementSession();
	FlushPreview();
	int sel = border_choice->GetSelection();
	if (sel >= 0 && sel < static_cast<int>(border_layers.size())) {
		TargetGroup old_group = group;
		int old_border = border_index;
		group = TargetGroup::Border;
		border_index = border_layers[sel];
		InvalidateTagSpan();
		if (!RefreshFromLine(true)) {
			group = old_group;
			border_index = old_border;
			InvalidateTagSpan();
			RefreshControls();
		}
		else if (lock_placement && placement_rect.valid)
			StartPlacementSession();
	}
}

void DialogMangetsuGradient::OnMode(ChannelMode new_mode) {
	if (updating_controls)
		return;
	CancelAddStopPlacement(false);
	EndPlacementSession();
	FlushPreview();
	ChannelMode old_mode = mode;
	mode = new_mode;
	InvalidateTagSpan();
	if (!RefreshFromLine(true)) {
		mode = old_mode;
		InvalidateTagSpan();
		RefreshControls();
	}
	else if (lock_placement && placement_rect.valid)
		StartPlacementSession();
}

void DialogMangetsuGradient::OnApply(wxCommandEvent&) {
	if (stop_bar && stop_bar->IsAdding())
		CancelAddStopPlacement(false);
	if (lock_placement && !placement_rect.valid) {
		placement_status = _("Drag a fixed gradient area before applying placement mode.");
		RefreshLightControls();
		return;
	}
	FlushPreview();
	commit_id = -1;
	if (active_line)
		original_text = active_line->Text.get();
	loaded_value = FormatTagValue();
	dirty = false;
	RefreshFromLine(false);
}

void DialogMangetsuGradient::OnOK(wxCommandEvent&) {
	CancelAddStopPlacement(false);
	FlushPreview();
	EndPlacementSession();
	commit_id = -1;
	FinishDialog(wxID_OK);
}

void DialogMangetsuGradient::OnCancel(wxCommandEvent&) {
	CancelAddStopPlacement(false);
	EndPlacementSession();
	if (preview_pending) {
		preview_timer.Stop();
		preview_pending = false;
	}
	if (commit_id != -1 && context && context->subsController) {
		context->subsController->Undo();
		commit_id = -1;
	}
	else if (active_line && active_line->Text.get() != original_text && context && context->ass) {
		active_line->Text = original_text;
		context->ass->Commit(_("cancel gradient"), AssFile::COMMIT_DIAG_TEXT, -1, active_line);
	}
	if (context && context->videoDisplay)
		context->videoDisplay->Render();
	FinishDialog(wxID_CANCEL);
}

void DialogMangetsuGradient::FinishDialog(int result) {
	if (IsModal())
		EndModal(result);
	else
		Destroy();
}

}

void ShowMangetsuGradientDialog(agi::Context *c) {
	if (!c || !c->selectionController || !c->selectionController->GetActiveLine())
		return;

	auto *dlg = new DialogMangetsuGradient(c->parent, c);
	dlg->Show();
}
