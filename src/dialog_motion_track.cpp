#include "dialog_motion_track.h"

#include "ass_dialogue.h"
#include "async_video_provider.h"
#include "compat.h"
#include "format.h"
#include "include/aegisub/context.h"
#include "libresrc/libresrc.h"
#include "motion_tracking/motion_track_engine.h"
#include "motion_tracking/motion_track_export_ae.h"
#include "persist_location.h"
#include "project.h"
#include "selection_controller.h"
#include "utils.h"
#include "video_controller.h"
#include "video_frame.h"

#include <libaegisub/ass/time.h>
#include <libaegisub/exception.h>
#include <libaegisub/make_unique.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <string>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dcbuffer.h>
#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/utils.h>

namespace {
constexpr double pi = 3.14159265358979323846;

double DegToRad(double deg) {
	return deg * pi / 180.0;
}

wxPoint RotatePoint(double x, double y, double angle) {
	double c = std::cos(angle);
	double s = std::sin(angle);
	return wxPoint(static_cast<int>(std::lround(x * c - y * s)), static_cast<int>(std::lround(x * s + y * c)));
}

bool ModeHasSize(motion_tracking::MotionTrackMode mode) {
	return mode == motion_tracking::MotionTrackMode::PositionSize || mode == motion_tracking::MotionTrackMode::PositionSizeRotation;
}

bool ModeHasRotation(motion_tracking::MotionTrackMode mode) {
	return mode == motion_tracking::MotionTrackMode::PositionRotation || mode == motion_tracking::MotionTrackMode::PositionSizeRotation;
}
}

class MotionTrackFrameBar final : public wxPanel {
	DialogMotionTrack *dialog = nullptr;
	bool dragging = false;

	int FrameFromX(int x) const {
		int start = dialog->GetStartFrame();
		int end = dialog->GetEndFrame();
		if (end <= start)
			return start;

		int width = GetClientSize().GetWidth();
		int pad = 16;
		double t = (x - pad) / static_cast<double>(std::max(1, width - pad * 2));
		t = std::clamp(t, 0.0, 1.0);
		return static_cast<int>(std::lround(start + t * (end - start)));
	}

	void OnPaint(wxPaintEvent &) {
		wxAutoBufferedPaintDC dc(this);
		dc.SetBackground(wxBrush(GetBackgroundColour()));
		dc.Clear();

		wxSize size = GetClientSize();
		int pad = 16;
		int y = size.y / 2;
		int left = pad;
		int right = std::max(left + 1, size.x - pad);

		dc.SetPen(wxPen(wxColour(120, 120, 120), 2));
		dc.DrawLine(left, y, right, y);

		int start = dialog->GetStartFrame();
		int end = dialog->GetEndFrame();
		int current = dialog->GetCurrentFrame();
		double t = end > start ? (current - start) / static_cast<double>(end - start) : 0.0;
		t = std::clamp(t, 0.0, 1.0);
		int x = static_cast<int>(std::lround(left + t * (right - left)));

		wxPoint arrow[] = {
			wxPoint(x, y - 9),
			wxPoint(x - 6, y - 1),
			wxPoint(x + 6, y - 1)
		};
		dc.SetBrush(wxBrush(wxColour(65, 116, 180)));
		dc.SetPen(wxPen(wxColour(45, 80, 130), 1));
		dc.DrawPolygon(3, arrow);
		dc.DrawLine(x, y, x, y + 9);
	}

	void OnMouse(wxMouseEvent &evt) {
		if (evt.LeftDown()) {
			dragging = true;
			CaptureMouse();
			dialog->JumpToFrame(FrameFromX(evt.GetX()));
		}
		else if (evt.LeftUp()) {
			dragging = false;
			if (HasCapture())
				ReleaseMouse();
		}
		else if (dragging && evt.Dragging()) {
			dialog->JumpToFrame(FrameFromX(evt.GetX()));
		}
		else {
			evt.Skip();
		}
	}

public:
	MotionTrackFrameBar(wxWindow *parent, DialogMotionTrack *dialog)
	: wxPanel(parent, -1, wxDefaultPosition, wxSize(-1, 28), wxBORDER_SIMPLE)
	, dialog(dialog) {
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		Bind(wxEVT_PAINT, &MotionTrackFrameBar::OnPaint, this);
		Bind(wxEVT_LEFT_DOWN, &MotionTrackFrameBar::OnMouse, this);
		Bind(wxEVT_LEFT_UP, &MotionTrackFrameBar::OnMouse, this);
		Bind(wxEVT_MOTION, &MotionTrackFrameBar::OnMouse, this);
	}
};

class MotionTrackPreviewPanel final : public wxPanel {
	enum class DragMode {
		NoDrag,
		Pan,
		Move,
		Handle
	};

	DialogMotionTrack *dialog = nullptr;
	bool fit_mode = true;
	bool fast_render = false;
	double scale = 1.0;
	wxPoint2DDouble offset{0.0, 0.0};
	wxPoint2DDouble last_video_pos{0.0, 0.0};
	wxPoint last_mouse_pos;
	DragMode drag_mode = DragMode::NoDrag;
	wxBitmap cached_view;
	wxRect cached_source_rect;
	wxPoint cached_draw_pos;
	wxSize cached_draw_size;
	int cached_frame = -1;
	bool cached_fast_render = false;

	wxPoint2DDouble ImageToScreen(double x, double y) const {
		return {offset.m_x + x * scale, offset.m_y + y * scale};
	}

	wxPoint2DDouble ScreenToImage(wxPoint const& point) const {
		return {(point.x - offset.m_x) / scale, (point.y - offset.m_y) / scale};
	}

	void UpdateFitTransform() {
		auto const& image = dialog->GetPreviewImage();
		if (!image.IsOk())
			return;

		wxSize client = GetClientSize();
		scale = std::min(
			client.x / static_cast<double>(std::max(1, image.GetWidth())),
			client.y / static_cast<double>(std::max(1, image.GetHeight())));
		if (!std::isfinite(scale) || scale <= 0.0)
			scale = 1.0;
		offset.m_x = (client.x - image.GetWidth() * scale) / 2.0;
		offset.m_y = (client.y - image.GetHeight() * scale) / 2.0;
	}

	void DrawMarker(wxDC &dc) {
		if (!dialog->HasCurrentMarker())
			return;

		auto marker = dialog->GetCurrentMarker();
		double angle = DegToRad(marker.rotation_deg);
		double half = marker.size / 2.0;
		double search_half = std::max(marker.search_size, marker.size) / 2.0;
		auto center = ImageToScreen(marker.cx, marker.cy);

		auto draw_square = [&](double h, wxPen pen) {
			wxPoint corners[4];
			wxPoint rel[] = {
				RotatePoint(-h * scale, -h * scale, angle),
				RotatePoint( h * scale, -h * scale, angle),
				RotatePoint( h * scale,  h * scale, angle),
				RotatePoint(-h * scale,  h * scale, angle)
			};
			for (int i = 0; i < 4; ++i)
				corners[i] = wxPoint(static_cast<int>(std::lround(center.m_x)) + rel[i].x, static_cast<int>(std::lround(center.m_y)) + rel[i].y);
			dc.SetPen(pen);
			dc.SetBrush(*wxTRANSPARENT_BRUSH);
			dc.DrawPolygon(4, corners);
		};

		draw_square(search_half, wxPen(wxColour(255, 170, 60), 1, wxPENSTYLE_SHORT_DASH));
		draw_square(half, wxPen(wxColour(65, 145, 220), 2));

		wxPoint handle_rel = RotatePoint(half * scale, 0.0, angle);
		wxPoint c(static_cast<int>(std::lround(center.m_x)), static_cast<int>(std::lround(center.m_y)));
		wxPoint handle(c.x + handle_rel.x, c.y + handle_rel.y);

		dc.SetPen(wxPen(wxColour(230, 120, 20), 2));
		dc.DrawLine(c, handle);
		dc.SetBrush(wxBrush(wxColour(65, 145, 220)));
		dc.SetPen(wxPen(*wxWHITE, 1));
		dc.DrawCircle(c, 4);
		dc.SetBrush(wxBrush(wxColour(230, 120, 20)));
		dc.SetPen(wxPen(wxColour(120, 60, 0), 1));
		dc.DrawCircle(handle, 6);
	}

	bool HitHandle(wxPoint const& point, motion_tracking::MotionTrackMarker const& marker) const {
		double half = marker.size / 2.0;
		auto center = ImageToScreen(marker.cx, marker.cy);
		wxPoint rel = RotatePoint(half * scale, 0.0, DegToRad(marker.rotation_deg));
		double hx = center.m_x + rel.x;
		double hy = center.m_y + rel.y;
		double dx = point.x - hx;
		double dy = point.y - hy;
		return dx * dx + dy * dy <= 100.0;
	}

	bool HitMarker(wxPoint const& point, motion_tracking::MotionTrackMarker const& marker) const {
		auto image = ScreenToImage(point);
		double dx = image.m_x - marker.cx;
		double dy = image.m_y - marker.cy;
		double angle = -DegToRad(marker.rotation_deg);
		double c = std::cos(angle);
		double s = std::sin(angle);
		double lx = dx * c - dy * s;
		double ly = dx * s + dy * c;
		double half = marker.size / 2.0;
		return std::abs(lx) <= half && std::abs(ly) <= half;
	}

	void ZoomAt(wxPoint const& mouse, double factor) {
		if (!dialog->GetPreviewImage().IsOk())
			return;

		if (fit_mode)
			UpdateFitTransform();

		auto image_before = ScreenToImage(mouse);
		scale = std::clamp(scale * factor, 0.05, 16.0);
		offset.m_x = mouse.x - image_before.m_x * scale;
		offset.m_y = mouse.y - image_before.m_y * scale;
		fit_mode = false;
		fast_render = true;
		Refresh(false);
	}

	void DrawImage(wxDC &dc, wxImage const& image) {
		wxSize client = GetClientSize();
		if (client.x <= 0 || client.y <= 0 || scale <= 0.0)
			return;

		double src_left_f = std::max(0.0, (-offset.m_x) / scale);
		double src_top_f = std::max(0.0, (-offset.m_y) / scale);
		double src_right_f = std::min(static_cast<double>(image.GetWidth()), (client.x - offset.m_x) / scale);
		double src_bottom_f = std::min(static_cast<double>(image.GetHeight()), (client.y - offset.m_y) / scale);

		int src_left = std::clamp(static_cast<int>(std::floor(src_left_f)), 0, image.GetWidth());
		int src_top = std::clamp(static_cast<int>(std::floor(src_top_f)), 0, image.GetHeight());
		int src_right = std::clamp(static_cast<int>(std::ceil(src_right_f)), src_left, image.GetWidth());
		int src_bottom = std::clamp(static_cast<int>(std::ceil(src_bottom_f)), src_top, image.GetHeight());
		int src_w = src_right - src_left;
		int src_h = src_bottom - src_top;
		if (src_w <= 0 || src_h <= 0)
			return;

		int dst_x = static_cast<int>(std::floor(offset.m_x + src_left * scale));
		int dst_y = static_cast<int>(std::floor(offset.m_y + src_top * scale));
		int dst_right = static_cast<int>(std::ceil(offset.m_x + src_right * scale));
		int dst_bottom = static_cast<int>(std::ceil(offset.m_y + src_bottom * scale));
		int dst_w = std::max(1, dst_right - dst_x);
		int dst_h = std::max(1, dst_bottom - dst_y);

		wxRect source_rect(src_left, src_top, src_w, src_h);
		wxPoint draw_pos(dst_x, dst_y);
		wxSize draw_size(dst_w, dst_h);
		bool cache_valid =
			cached_view.IsOk() &&
			cached_frame == dialog->GetPreviewFrame() &&
			cached_source_rect.x == source_rect.x &&
			cached_source_rect.y == source_rect.y &&
			cached_source_rect.width == source_rect.width &&
			cached_source_rect.height == source_rect.height &&
			cached_draw_pos.x == draw_pos.x &&
			cached_draw_pos.y == draw_pos.y &&
			cached_draw_size.x == draw_size.x &&
			cached_draw_size.y == draw_size.y &&
			cached_fast_render == fast_render;

		if (!cache_valid) {
			wxImage crop = image.GetSubImage(source_rect);
			wxImage scaled = crop.Scale(dst_w, dst_h, fast_render ? wxIMAGE_QUALITY_NORMAL : wxIMAGE_QUALITY_HIGH);
			cached_view = wxBitmap(scaled);
			cached_source_rect = source_rect;
			cached_draw_pos = draw_pos;
			cached_draw_size = draw_size;
			cached_frame = dialog->GetPreviewFrame();
			cached_fast_render = fast_render;
		}

		dc.DrawBitmap(cached_view, cached_draw_pos.x, cached_draw_pos.y);
	}

	void OnPaint(wxPaintEvent &) {
		wxAutoBufferedPaintDC dc(this);
		dc.SetBackground(wxBrush(wxColour(28, 28, 28)));
		dc.Clear();

		auto const& image = dialog->GetPreviewImage();
		if (!image.IsOk()) {
			dc.SetTextForeground(wxColour(220, 220, 220));
			dc.DrawText(_("No video frame"), 12, 12);
			return;
		}

		if (fit_mode)
			UpdateFitTransform();

		DrawImage(dc, image);
		DrawMarker(dc);
	}

	void OnMouse(wxMouseEvent &evt) {
		if (!dialog->GetPreviewImage().IsOk()) {
			evt.Skip();
			return;
		}

		if (fit_mode)
			UpdateFitTransform();

		if (evt.MiddleDown()) {
			fit_mode = false;
			drag_mode = DragMode::Pan;
			last_mouse_pos = evt.GetPosition();
			CaptureMouse();
			return;
		}

		if (evt.LeftDown()) {
			SetFocus();
			auto image_pos = ScreenToImage(evt.GetPosition());
			if (dialog->HasCurrentMarker()) {
				auto marker = dialog->GetCurrentMarker();
				if (HitHandle(evt.GetPosition(), marker))
					drag_mode = DragMode::Handle;
				else if (HitMarker(evt.GetPosition(), marker))
					drag_mode = DragMode::Move;
				else {
					dialog->PlaceCurrentMarker(image_pos.m_x, image_pos.m_y);
					drag_mode = DragMode::Move;
				}
			}
			else {
				dialog->PlaceCurrentMarker(image_pos.m_x, image_pos.m_y);
				drag_mode = DragMode::Move;
			}
			last_video_pos = image_pos;
			CaptureMouse();
			return;
		}

		if (evt.LeftUp() || evt.MiddleUp()) {
			drag_mode = DragMode::NoDrag;
			fast_render = false;
			if (HasCapture())
				ReleaseMouse();
			Refresh(false);
			return;
		}

		if (evt.Dragging() && drag_mode != DragMode::NoDrag) {
			if (drag_mode == DragMode::Pan) {
				wxPoint pos = evt.GetPosition();
				offset.m_x += pos.x - last_mouse_pos.x;
				offset.m_y += pos.y - last_mouse_pos.y;
				last_mouse_pos = pos;
				fast_render = true;
				Refresh(false);
				return;
			}

			if (!dialog->HasCurrentMarker())
				return;

			auto marker = dialog->GetCurrentMarker();
			auto image_pos = ScreenToImage(evt.GetPosition());
			if (drag_mode == DragMode::Move) {
				marker.cx += image_pos.m_x - last_video_pos.m_x;
				marker.cy += image_pos.m_y - last_video_pos.m_y;
				last_video_pos = image_pos;
			}
			else if (drag_mode == DragMode::Handle) {
				double dx = image_pos.m_x - marker.cx;
				double dy = image_pos.m_y - marker.cy;
				double radius = std::sqrt(dx * dx + dy * dy);
				marker.size = std::max(4.0, radius * 2.0);
				marker.search_size = std::max(marker.search_size, marker.size);
				marker.rotation_deg = std::atan2(dy, dx) * 180.0 / pi;
			}
			dialog->SetCurrentMarker(marker);
			return;
		}

		evt.Skip();
	}

	void OnMouseWheel(wxMouseEvent &evt) {
		if (!dialog->GetPreviewImage().IsOk())
			return;

		double factor = evt.GetWheelRotation() > 0 ? 1.15 : 1.0 / 1.15;
		ZoomAt(evt.GetPosition(), factor);
	}

	void OnDoubleClick(wxMouseEvent &) {
		Fit();
	}

	void OnIdle(wxIdleEvent &) {
		if (fast_render && drag_mode == DragMode::NoDrag) {
			fast_render = false;
			Refresh(false);
		}
	}

	void OnKeyDown(wxKeyEvent &evt) {
		int code = evt.GetKeyCode();
		if (code == WXK_DELETE || code == WXK_BACK) {
			dialog->DeleteCurrentMarker();
			return;
		}
		if (code == '1') {
			auto const& image = dialog->GetPreviewImage();
			if (image.IsOk()) {
				wxSize client = GetClientSize();
				scale = 1.0;
				offset.m_x = (client.x - image.GetWidth()) / 2.0;
				offset.m_y = (client.y - image.GetHeight()) / 2.0;
				fit_mode = false;
				fast_render = false;
				Refresh(false);
			}
			return;
		}
		if (code == '+' || code == WXK_NUMPAD_ADD || code == '-' || code == WXK_NUMPAD_SUBTRACT) {
			ZoomAt(ScreenToClient(wxGetMousePosition()), (code == '+' || code == WXK_NUMPAD_ADD) ? 1.15 : 1.0 / 1.15);
			return;
		}
		evt.Skip();
	}

public:
	MotionTrackPreviewPanel(wxWindow *parent, DialogMotionTrack *dialog)
	: wxPanel(parent, -1, wxDefaultPosition, wxSize(640, 360), wxBORDER_SIMPLE)
	, dialog(dialog) {
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		SetMinSize(wxSize(320, 220));
		Bind(wxEVT_PAINT, &MotionTrackPreviewPanel::OnPaint, this);
		Bind(wxEVT_LEFT_DOWN, &MotionTrackPreviewPanel::OnMouse, this);
		Bind(wxEVT_LEFT_UP, &MotionTrackPreviewPanel::OnMouse, this);
		Bind(wxEVT_MIDDLE_DOWN, &MotionTrackPreviewPanel::OnMouse, this);
		Bind(wxEVT_MIDDLE_UP, &MotionTrackPreviewPanel::OnMouse, this);
		Bind(wxEVT_MOTION, &MotionTrackPreviewPanel::OnMouse, this);
		Bind(wxEVT_MOUSEWHEEL, &MotionTrackPreviewPanel::OnMouseWheel, this);
		Bind(wxEVT_LEFT_DCLICK, &MotionTrackPreviewPanel::OnDoubleClick, this);
		Bind(wxEVT_KEY_DOWN, &MotionTrackPreviewPanel::OnKeyDown, this);
		Bind(wxEVT_IDLE, &MotionTrackPreviewPanel::OnIdle, this);
	}

	void Fit() {
		fit_mode = true;
		fast_render = false;
		Refresh(false);
	}
};

class MotionTrackGraphPanel final : public wxPanel {
	DialogMotionTrack *dialog = nullptr;

	void Plot(wxDC &dc, std::vector<motion_tracking::MotionTrackFrame> const& frames, wxRect rect, wxColour colour, double motion_tracking::MotionTrackFrame::*member) {
		if (frames.size() < 2)
			return;

		auto [min_it, max_it] = std::minmax_element(frames.begin(), frames.end(), [member](auto const& a, auto const& b) {
			return a.*member < b.*member;
		});
		double min_v = (*min_it).*member;
		double max_v = (*max_it).*member;
		if (std::abs(max_v - min_v) < 1e-9) {
			max_v += 1.0;
			min_v -= 1.0;
		}

		int start = frames.front().frame;
		int end = frames.back().frame;
		if (end <= start)
			end = start + 1;

		dc.SetPen(wxPen(colour, 1));
		wxPoint prev;
		bool have_prev = false;
		for (auto const& frame : frames) {
			double tx = (frame.frame - start) / static_cast<double>(end - start);
			double ty = (frame.*member - min_v) / (max_v - min_v);
			wxPoint cur(
				rect.x + static_cast<int>(std::lround(tx * rect.width)),
				rect.y + rect.height - static_cast<int>(std::lround(ty * rect.height)));
			if (have_prev)
				dc.DrawLine(prev, cur);
			prev = cur;
			have_prev = true;
		}
	}

	void OnPaint(wxPaintEvent &) {
		wxAutoBufferedPaintDC dc(this);
		dc.SetBackground(wxBrush(GetBackgroundColour()));
		dc.Clear();

		auto frames = dialog->GetResult().frames;
		std::sort(frames.begin(), frames.end(), [](auto const& a, auto const& b) { return a.frame < b.frame; });

		wxSize size = GetClientSize();
		wxRect plot(44, 12, std::max(1, size.x - 58), std::max(1, size.y - 34));
		dc.SetPen(wxPen(wxColour(170, 170, 170), 1));
		dc.SetBrush(*wxTRANSPARENT_BRUSH);
		dc.DrawRectangle(plot);

		dc.SetTextForeground(wxColour(80, 80, 80));
		dc.DrawText(_("X"), 6, 8);
		dc.DrawText(_("Y"), 6, 24);
		dc.DrawText(_("Scale"), 6, 40);
		dc.DrawText(_("Rot"), 6, 56);
		dc.DrawText(_("Conf"), 6, 72);

		if (frames.size() < 2) {
			dc.SetTextForeground(wxColour(110, 110, 110));
			dc.DrawText(_("Place and track a marker to plot data"), plot.x + 8, plot.y + 8);
			return;
		}

		Plot(dc, frames, plot, wxColour(30, 120, 220), &motion_tracking::MotionTrackFrame::x);
		Plot(dc, frames, plot, wxColour(30, 160, 80), &motion_tracking::MotionTrackFrame::y);
		Plot(dc, frames, plot, wxColour(220, 150, 20), &motion_tracking::MotionTrackFrame::scale_x);
		Plot(dc, frames, plot, wxColour(180, 80, 180), &motion_tracking::MotionTrackFrame::rotation_deg);
		Plot(dc, frames, plot, wxColour(210, 60, 60), &motion_tracking::MotionTrackFrame::confidence);
	}

public:
	MotionTrackGraphPanel(wxWindow *parent, DialogMotionTrack *dialog)
	: wxPanel(parent, -1, wxDefaultPosition, wxSize(-1, 120), wxBORDER_SIMPLE)
	, dialog(dialog) {
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		SetMinSize(wxSize(320, 90));
		Bind(wxEVT_PAINT, &MotionTrackGraphPanel::OnPaint, this);
	}
};

DialogMotionTrack::DialogMotionTrack(agi::Context *c)
: wxDialog(c->parent, -1, _("Motion Track"), wxDefaultPosition, wxSize(780, 640), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxMAXIMIZE_BOX | wxMINIMIZE_BOX)
, context(c) {
	if (!context->project->VideoProvider())
		throw agi::UserCancelException("No video loaded");

	SetIcon(GETICON(button_motion_track_16));

	CalculateSelectedFrameRange();
	result.fps = context->project->Timecodes().FPS();
	result.source_width = context->project->VideoProvider()->GetWidth();
	result.source_height = context->project->VideoProvider()->GetHeight();

	CreateControls();
	BindControls();
	Fit();
	SetMinSize(wxSize(620, 460));
	CenterOnParent();

	try {
		persist = agi::make_unique<PersistLocation>(this, "Tool/Motion Track");
	}
	catch (agi::InternalError const& e) {
		if (e.GetMessage().find("Tool/Motion Track/") == std::string::npos)
			throw;
	}
	connections = agi::signal::make_vector({
		context->videoController->AddSeekListener(&DialogMotionTrack::OnSeek, this),
		context->project->AddVideoProviderListener([=](AsyncVideoProvider *) { Close(); })
	});

	JumpToFrame(current_frame);
}

DialogMotionTrack::~DialogMotionTrack() {
}

void DialogMotionTrack::CalculateSelectedFrameRange() {
	auto selection = context->selectionController->GetSortedSelection();
	if (selection.empty()) {
		if (auto active = context->selectionController->GetActiveLine())
			selection.push_back(active);
	}
	if (selection.empty())
		throw agi::UserCancelException("No subtitle lines selected");

	int start_ms = std::numeric_limits<int>::max();
	int end_ms = std::numeric_limits<int>::min();
	for (auto line : selection) {
		start_ms = std::min<int>(start_ms, line->Start);
		end_ms = std::max<int>(end_ms, line->End);
	}

	int last_frame = std::max(0, context->project->VideoProvider()->GetFrameCount() - 1);
	settings.start_frame = mid(0, context->videoController->FrameAtTime(start_ms, agi::vfr::START), last_frame);
	settings.end_frame = mid(0, context->videoController->FrameAtTime(end_ms, agi::vfr::END), last_frame);
	if (settings.end_frame < settings.start_frame)
		std::swap(settings.start_frame, settings.end_frame);

	current_frame = mid(settings.start_frame, context->videoController->GetFrameN(), settings.end_frame);
}

void DialogMotionTrack::CreateControls() {
	auto main_sizer = new wxBoxSizer(wxVERTICAL);

	auto top_row = new wxBoxSizer(wxHORIZONTAL);
	range_label = new wxStaticText(this, -1, "");
	current_label = new wxStaticText(this, -1, "");
	top_row->Add(range_label, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
	top_row->Add(current_label, 0, wxALIGN_CENTER_VERTICAL);
	main_sizer->Add(top_row, 0, wxEXPAND | wxALL, 6);

	frame_bar = new MotionTrackFrameBar(this, this);
	main_sizer->Add(frame_bar, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

	auto controls = new wxBoxSizer(wxVERTICAL);
	auto track_buttons = new wxBoxSizer(wxHORIZONTAL);
	track_to_start = new wxButton(this, -1, _("Track to Start"));
	track_previous = new wxButton(this, -1, _("Track Previous"));
	track_next = new wxButton(this, -1, _("Track Next"));
	track_to_end = new wxButton(this, -1, _("Track to End"));
	track_to_start->SetToolTip(_("Track backward to start frame"));
	track_previous->SetToolTip(_("Track previous frame"));
	track_next->SetToolTip(_("Track next frame"));
	track_to_end->SetToolTip(_("Track forward to end frame"));
	track_buttons->Add(track_to_start, 0, wxRIGHT, 4);
	track_buttons->Add(track_previous, 0, wxRIGHT, 4);
	track_buttons->Add(track_next, 0, wxRIGHT, 4);
	track_buttons->Add(track_to_end, 0);
	controls->Add(track_buttons, 0, wxBOTTOM, 4);

	auto settings_row = new wxBoxSizer(wxHORIZONTAL);
	settings_row->Add(new wxStaticText(this, -1, _("Square:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
	square_ctrl = new wxSpinCtrl(this, -1, "", wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 4, 2000, settings.square_size);
	settings_row->Add(square_ctrl, 0, wxRIGHT, 8);
	settings_row->Add(new wxStaticText(this, -1, _("Search:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
	search_ctrl = new wxSpinCtrl(this, -1, "", wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 4, 4000, settings.search_size);
	settings_row->Add(search_ctrl, 0, wxRIGHT, 8);
	settings_row->Add(new wxStaticText(this, -1, _("Threshold:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
	threshold_ctrl = new wxSpinCtrlDouble(this, -1, "", wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 0.0, 1.0, settings.correlation_threshold, 0.05);
	threshold_ctrl->SetDigits(2);
	settings_row->Add(threshold_ctrl, 0, wxRIGHT, 8);
	normalize_check = new wxCheckBox(this, -1, _("Normalize"));
	normalize_check->SetValue(settings.brightness_normalize);
	normalize_check->SetToolTip(_("Normalize pattern and search patches before correlation matching"));
	settings_row->Add(normalize_check, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
	settings_row->Add(new wxStaticText(this, -1, _("Base:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
	base_choice = new wxChoice(this, -1);
	base_choice->Append(_("Previous Frame"));
	base_choice->Append(_("First Frame"));
	base_choice->SetSelection(0);
	settings_row->Add(base_choice, 0);
	controls->Add(settings_row, 0, wxBOTTOM, 4);

	auto mode_row = new wxBoxSizer(wxHORIZONTAL);
	mode_row->Add(new wxStaticText(this, -1, _("Mode:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
	mode_choice = new wxChoice(this, -1);
	mode_choice->Append(_("Position Only"));
	mode_choice->Append(_("Position + Rotation"));
	mode_choice->Append(_("Position + Size"));
	mode_choice->Append(_("Position + Size + Rotation"));
	mode_choice->SetSelection(3);
	mode_row->Add(mode_choice, 0, wxRIGHT, 8);
	mode_row->Add(new wxStaticText(this, -1, _("Smoothing:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
	smoothing_choice = new wxChoice(this, -1);
	smoothing_choice->Append(_("Off"));
	smoothing_choice->Append(_("Light"));
	smoothing_choice->Append(_("Medium"));
	smoothing_choice->Append(_("Heavy"));
	smoothing_choice->SetSelection(2);
	mode_row->Add(smoothing_choice, 0);
	controls->Add(mode_row, 0, wxBOTTOM, 2);
	main_sizer->Add(controls, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

	preview = new MotionTrackPreviewPanel(this, this);
	main_sizer->Add(preview, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

	graph = new MotionTrackGraphPanel(this, this);
	main_sizer->Add(graph, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

	main_sizer->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, 6);
	auto bottom = new wxBoxSizer(wxHORIZONTAL);
	auto copy = new wxButton(this, -1, _("Copy Data"));
	auto save = new wxButton(this, -1, _("Save Data"));
	auto clear = new wxButton(this, -1, _("Clear"));
	auto close = new wxButton(this, wxID_CANCEL, _("Close"));
	bottom->Add(copy, 0, wxRIGHT, 4);
	bottom->Add(save, 0, wxRIGHT, 4);
	bottom->Add(clear, 0, wxRIGHT, 4);
	bottom->AddStretchSpacer(1);
	bottom->Add(close, 0);
	main_sizer->Add(bottom, 0, wxEXPAND | wxALL, 6);

	copy->Bind(wxEVT_BUTTON, [=](wxCommandEvent &) { CopyData(); });
	save->Bind(wxEVT_BUTTON, [=](wxCommandEvent &) { SaveData(); });
	clear->Bind(wxEVT_BUTTON, [=](wxCommandEvent &) { ClearData(); });

	SetSizer(main_sizer);
	UpdateLabels();
}

void DialogMotionTrack::BindControls() {
	auto update_settings = [=] { UpdateSettingsFromControls(); };
	square_ctrl->Bind(wxEVT_SPINCTRL, [=](wxSpinEvent &) { update_settings(); });
	search_ctrl->Bind(wxEVT_SPINCTRL, [=](wxSpinEvent &) { update_settings(); });
	threshold_ctrl->Bind(wxEVT_SPINCTRLDOUBLE, [=](wxSpinDoubleEvent &) { update_settings(); });
	normalize_check->Bind(wxEVT_CHECKBOX, [=](wxCommandEvent &) { update_settings(); });
	base_choice->Bind(wxEVT_CHOICE, [=](wxCommandEvent &) { update_settings(); });
	mode_choice->Bind(wxEVT_CHOICE, [=](wxCommandEvent &) { update_settings(); });
	smoothing_choice->Bind(wxEVT_CHOICE, [=](wxCommandEvent &) { update_settings(); });

	track_to_start->Bind(wxEVT_BUTTON, [=](wxCommandEvent &) { TrackRange(settings.start_frame); });
	track_previous->Bind(wxEVT_BUTTON, [=](wxCommandEvent &) { TrackOne(current_frame - 1); });
	track_next->Bind(wxEVT_BUTTON, [=](wxCommandEvent &) { TrackOne(current_frame + 1); });
	track_to_end->Bind(wxEVT_BUTTON, [=](wxCommandEvent &) { TrackRange(settings.end_frame); });
}

void DialogMotionTrack::UpdateSettingsFromControls() {
	settings.square_size = square_ctrl->GetValue();
	settings.search_size = std::max(search_ctrl->GetValue(), settings.square_size);
	if (search_ctrl->GetValue() != settings.search_size)
		search_ctrl->SetValue(settings.search_size);
	settings.correlation_threshold = std::clamp(threshold_ctrl->GetValue(), 0.0, 1.0);
	settings.brightness_normalize = normalize_check->IsChecked();

	settings.base = base_choice->GetSelection() == 1 ? motion_tracking::MotionTrackBase::FirstFrame : motion_tracking::MotionTrackBase::PreviousFrame;

	switch (mode_choice->GetSelection()) {
		case 0: settings.mode = motion_tracking::MotionTrackMode::PositionOnly; break;
		case 1: settings.mode = motion_tracking::MotionTrackMode::PositionRotation; break;
		case 2: settings.mode = motion_tracking::MotionTrackMode::PositionSize; break;
		default: settings.mode = motion_tracking::MotionTrackMode::PositionSizeRotation; break;
	}

	switch (smoothing_choice->GetSelection()) {
		case 0: settings.smoothing = motion_tracking::MotionTrackSmoothing::Off; break;
		case 1: settings.smoothing = motion_tracking::MotionTrackSmoothing::Light; break;
		case 3: settings.smoothing = motion_tracking::MotionTrackSmoothing::Heavy; break;
		default: settings.smoothing = motion_tracking::MotionTrackSmoothing::Medium; break;
	}
}

void DialogMotionTrack::UpdateLabels() {
	range_label->SetLabel(fmt_wx("Selected line frames: %d - %d", settings.start_frame, settings.end_frame));
	current_label->SetLabel(fmt_wx("Current: %d", current_frame));
}

void DialogMotionTrack::UpdatePanels() {
	UpdateLabels();
	if (frame_bar)
		frame_bar->Refresh(false);
	if (preview)
		preview->Refresh(false);
	if (graph)
		graph->Refresh(false);
}

void DialogMotionTrack::RefreshPreview() {
	if (preview)
		preview->Refresh(false);
}

void DialogMotionTrack::LoadCurrentFrame() {
	if (preview_frame == current_frame) {
		UpdatePanels();
		return;
	}

	try {
		auto frame = context->videoController->GetFrame(current_frame, true);
		preview_image = GetImage(*frame);
		preview_frame = current_frame;
	}
	catch (...) {
		preview_image = wxImage();
		preview_frame = -1;
	}
	UpdatePanels();
}

void DialogMotionTrack::OnSeek(int frame) {
	current_frame = mid(0, frame, std::max(0, context->project->VideoProvider()->GetFrameCount() - 1));
	LoadCurrentFrame();
}

bool DialogMotionTrack::HasCurrentMarker() const {
	return markers.find(current_frame) != markers.end();
}

motion_tracking::MotionTrackMarker DialogMotionTrack::GetCurrentMarker() const {
	return MarkerForFrame(current_frame);
}

motion_tracking::MotionTrackMarker DialogMotionTrack::MarkerForFrame(int frame) const {
	auto it = markers.find(frame);
	if (it != markers.end())
		return it->second;
	return {};
}

void DialogMotionTrack::SetCurrentMarker(motion_tracking::MotionTrackMarker marker) {
	marker.search_size = std::max(marker.search_size, marker.size);
	markers[current_frame] = marker;
	if (base_frame < 0) {
		base_frame = current_frame;
		initial_marker_size = marker.size;
	}
	RefreshPreview();
}

void DialogMotionTrack::PlaceCurrentMarker(double x, double y) {
	UpdateSettingsFromControls();
	motion_tracking::MotionTrackMarker marker;
	marker.cx = x;
	marker.cy = y;
	marker.size = settings.square_size;
	marker.search_size = std::max(settings.search_size, settings.square_size);
	marker.rotation_deg = 0.0;
	SetCurrentMarker(marker);
}

void DialogMotionTrack::DeleteCurrentMarker() {
	markers.erase(current_frame);
	result.frames.erase(std::remove_if(result.frames.begin(), result.frames.end(), [=](auto const& frame) {
		return frame.frame == current_frame;
	}), result.frames.end());
	if (base_frame == current_frame) {
		base_frame = markers.empty() ? -1 : markers.begin()->first;
		if (base_frame >= 0)
			initial_marker_size = markers.begin()->second.size;
	}
	UpdatePanels();
}

void DialogMotionTrack::JumpToFrame(int frame) {
	frame = mid(settings.start_frame, frame, settings.end_frame);
	current_frame = frame;
	context->videoController->JumpToFrame(frame);
	LoadCurrentFrame();
}

motion_tracking::MotionTrackFrame DialogMotionTrack::MakeFrame(
	int frame,
	motion_tracking::MotionTrackMarker const& marker,
	double confidence,
	motion_tracking::MotionTrackState state) const {

	double scale = initial_marker_size > 0.0 ? marker.size / initial_marker_size : 1.0;
	return motion_tracking::MotionTrackFrame{
		frame,
		marker.cx,
		marker.cy,
		marker.cx,
		marker.cy,
		scale,
		scale,
		marker.rotation_deg,
		confidence,
		state
	};
}

void DialogMotionTrack::StoreFrame(int frame, motion_tracking::MotionTrackMarker const& marker, double confidence, motion_tracking::MotionTrackState state) {
	auto output = MakeFrame(frame, marker, confidence, state);
	auto it = std::find_if(result.frames.begin(), result.frames.end(), [=](auto const& existing) {
		return existing.frame == frame;
	});
	if (it == result.frames.end())
		result.frames.push_back(output);
	else
		*it = output;
}

motion_tracking::MotionTrackImage DialogMotionTrack::GetTrackImage(int frame_number) const {
	auto frame = context->videoController->GetFrame(frame_number, true);
	motion_tracking::MotionTrackImage image;
	image.width = static_cast<int>(frame->width);
	image.height = static_cast<int>(frame->height);
	image.pitch = static_cast<int>(frame->pitch);
	image.flipped = frame->flipped;
	image.bgra = frame->data;
	return image;
}

void DialogMotionTrack::TrackOne(int target_frame) {
	UpdateSettingsFromControls();
	if (!motion_tracking::MotionTrackEngine::IsAvailable()) {
		wxMessageBox(_("Motion Track requires OpenCV, but this build was configured without OpenCV."), _("Motion Track"), wxOK | wxICON_INFORMATION, this);
		return;
	}

	if (target_frame < settings.start_frame || target_frame > settings.end_frame)
		return;
	if (!HasCurrentMarker()) {
		wxMessageBox(_("Place a tracker marker before tracking."), _("Motion Track"), wxOK | wxICON_INFORMATION, this);
		return;
	}

	int source_frame = current_frame;
	auto search_marker = GetCurrentMarker();
	auto source_marker = search_marker;
	if (settings.base == motion_tracking::MotionTrackBase::FirstFrame && base_frame >= 0 && markers.count(base_frame)) {
		source_frame = base_frame;
		source_marker = markers[base_frame];
	}
	auto source_output = std::find_if(result.frames.begin(), result.frames.end(), [=](auto const& frame) {
		return frame.frame == source_frame;
	});
	if (source_output == result.frames.end())
		StoreFrame(source_frame, source_marker, 1.0, motion_tracking::MotionTrackState::Untracked);
	else
		StoreFrame(source_frame, source_marker, source_output->confidence, source_output->state);

	try {
		motion_tracking::MotionTrackEngine engine;
		auto step = engine.TrackFrame(GetTrackImage(source_frame), GetTrackImage(target_frame), source_marker, search_marker, target_frame, settings);

		if (!ModeHasSize(settings.mode))
			step.marker.size = search_marker.size;
		if (!ModeHasRotation(settings.mode))
			step.marker.rotation_deg = search_marker.rotation_deg;
		step.marker.search_size = std::max(step.marker.search_size, step.marker.size);

		markers[target_frame] = step.marker;
		StoreFrame(target_frame, step.marker, step.frame.confidence, step.frame.state);
		JumpToFrame(target_frame);
	}
	catch (...) {
		wxMessageBox(_("Tracking failed while reading video frames."), _("Motion Track"), wxOK | wxICON_ERROR, this);
	}
}

void DialogMotionTrack::TrackRange(int target_frame) {
	if (target_frame == current_frame)
		return;
	if (!HasCurrentMarker()) {
		wxMessageBox(_("Place a tracker marker before tracking."), _("Motion Track"), wxOK | wxICON_INFORMATION, this);
		return;
	}

	wxBeginBusyCursor();
	int step = target_frame > current_frame ? 1 : -1;
	while (current_frame != target_frame) {
		int next = current_frame + step;
		TrackOne(next);
		if (!markers.count(next))
			break;
		wxYieldIfNeeded();
	}
	wxEndBusyCursor();
}

void DialogMotionTrack::CopyData() {
	if (result.frames.empty()) {
		wxMessageBox(_("No motion data to copy."), _("Motion Track"), wxOK | wxICON_INFORMATION, this);
		return;
	}
	motion_tracking::MotionTrackExportSettings export_settings;
	export_settings.smoothing = settings.smoothing;
	SetClipboard(motion_tracking::ExportAfterEffectsKeyframes(result, settings.start_frame, export_settings));
}

void DialogMotionTrack::SaveData() {
	if (result.frames.empty()) {
		wxMessageBox(_("No motion data to save."), _("Motion Track"), wxOK | wxICON_INFORMATION, this);
		return;
	}

	wxString filename = wxFileSelector(
		_("Save motion track data"),
		"",
		"motion_track.txt",
		"txt",
		_("Text files (*.txt)|*.txt|All files (*.*)|*.*"),
		wxFD_SAVE | wxFD_OVERWRITE_PROMPT,
		this);
	if (filename.empty())
		return;

	std::ofstream file(from_wx(filename), std::ios::binary);
	if (!file) {
		wxMessageBox(_("Could not open the selected file for writing."), _("Motion Track"), wxOK | wxICON_ERROR, this);
		return;
	}
	motion_tracking::MotionTrackExportSettings export_settings;
	export_settings.smoothing = settings.smoothing;
	file << motion_tracking::ExportAfterEffectsKeyframes(result, settings.start_frame, export_settings);
}

void DialogMotionTrack::ClearData() {
	markers.clear();
	result.frames.clear();
	base_frame = -1;
	initial_marker_size = settings.square_size;
	UpdatePanels();
}
