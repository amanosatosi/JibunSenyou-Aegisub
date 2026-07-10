// Copyright (c) 2026

#include "visual_tool_gradient_placement.h"

#include "video_display.h"

#include <cmath>

#include <wx/cursor.h>
#include <wx/event.h>

VisualToolGradientPlacement::VisualToolGradientPlacement(
	VideoDisplay *parent, agi::Context *context, std::shared_ptr<GradientPlacementSession> session)
: VisualToolBase(parent, context)
, session(std::move(session))
{
	OnAttached();
}

VisualToolGradientPlacement::~VisualToolGradientPlacement() {
	if (parent && parent->HasCapture())
		parent->ReleaseMouse();
	if (parent)
		parent->SetCursor(wxNullCursor);
	NotifyDeactivated();
}

void VisualToolGradientPlacement::OnAttached() {
	parent->SetCursor(wxCursor(wxCURSOR_CROSS));
}

bool VisualToolGradientPlacement::InVideoArea(Vector2D const& point) const {
	Vector2D const end = video_pos + video_res;
	return point.X() >= video_pos.X() && point.X() <= end.X() &&
		point.Y() >= video_pos.Y() && point.Y() <= end.Y();
}

Vector2D VisualToolGradientPlacement::ClampToVideo(Vector2D const& point) const {
	return video_pos.Max((video_pos + video_res).Min(point));
}

void VisualToolGradientPlacement::CancelDrag(char const* message) {
	if (!dragging)
		return;
	dragging = false;
	if (session)
		session->dragging = false;
	if (parent->HasCapture())
		parent->ReleaseMouse();
	if (session && !session->ending) {
		if (message && session->status)
			session->status(message);
	}
	parent->Render();
}

void VisualToolGradientPlacement::RequestEnd() {
	if (session && !session->ending && session->cancelled)
		session->cancelled();
}

void VisualToolGradientPlacement::FinishDrag() {
	if (!dragging)
		return;

	dragging = false;
	if (session)
		session->dragging = false;
	if (parent->HasCapture())
		parent->ReleaseMouse();

	Vector2D const delta = drag_current - drag_start;
	if (std::abs(delta.X()) < 4 || std::abs(delta.Y()) < 4) {
		if (session && !session->ending && session->invalid_drag)
			session->invalid_drag();
		parent->Render();
		return;
	}

	Vector2D const first = ToScriptCoords(drag_start);
	Vector2D const second = ToScriptCoords(drag_current);
	mangetsu::PlacementRect const rect = mangetsu::NormalizePlacementRect(
		std::lround(first.X()), std::lround(first.Y()),
		std::lround(second.X()), std::lround(second.Y()));
	if (!rect.valid || rect.left == rect.right || rect.top == rect.bottom) {
		if (session && !session->ending && session->invalid_drag)
			session->invalid_drag();
		parent->Render();
		return;
	}

	if (session && !session->ending) {
		session->rectangle = rect;
		if (session->accepted)
			session->accepted(rect);
	}
	parent->SetFocus();
	parent->Render();
}

void VisualToolGradientPlacement::NotifyDeactivated() {
	if (!session || session->ending || !session->deactivated)
		return;
	auto callback = session->deactivated;
	session->deactivated = nullptr;
	callback();
}

void VisualToolGradientPlacement::OnMouseCaptureLost(wxMouseCaptureLostEvent&) {
	CancelDrag("Placement drag cancelled because the video lost mouse capture.");
	RequestEnd();
}

bool VisualToolGradientPlacement::OnKeyEvent(wxKeyEvent &event) {
	if (event.GetKeyCode() != WXK_ESCAPE)
		return false;
	CancelDrag("Placement drag cancelled.");
	RequestEnd();
	return true;
}

void VisualToolGradientPlacement::OnMouseEvent(wxMouseEvent &event) {
	mouse_pos = event.GetPosition();
	if (event.Leaving() && !dragging) {
		parent->Render();
		return;
	}

	if (!dragging && event.LeftDown()) {
		if (!InVideoArea(mouse_pos)) {
			if (session && session->status)
				session->status("Start the placement drag inside the visible video area.");
			return;
		}
		drag_start = ClampToVideo(mouse_pos);
		drag_current = drag_start;
		dragging = true;
		if (session)
			session->dragging = true;
		parent->CaptureMouse();
		parent->Render();
		return;
	}

	if (!dragging)
		return;

	if (event.LeftIsDown()) {
		drag_current = ClampToVideo(mouse_pos);
		parent->Render();
	}
	else if (event.LeftUp()) {
		drag_current = ClampToVideo(mouse_pos);
		FinishDrag();
	}
}

void VisualToolGradientPlacement::Draw() {
	mangetsu::PlacementRect rect;
	bool have_rect = false;
	if (dragging) {
		Vector2D const first = ToScriptCoords(drag_start);
		Vector2D const second = ToScriptCoords(drag_current);
		rect = mangetsu::NormalizePlacementRect(first.X(), first.Y(), second.X(), second.Y());
		have_rect = rect.valid;
	}
	else if (session && session->rectangle.valid) {
		rect = session->rectangle;
		have_rect = true;
	}

	if (!have_rect)
		return;

	Vector2D const first = FromScriptCoords(Vector2D(rect.left, rect.top));
	Vector2D const second = FromScriptCoords(Vector2D(rect.right, rect.bottom));
	Vector2D const top_left = first.Min(second);
	Vector2D const bottom_right = first.Max(second);

	// A black under-stroke keeps the theme-coloured outline readable on both
	// bright and dark video; the small filled corners make its extent obvious.
	gl.SetFillColour(*wxBLACK, 0.08f);
	gl.SetLineColour(*wxBLACK, 0.85f, 4);
	gl.DrawRectangle(top_left, bottom_right);
	gl.SetFillColour(wxColour(82, 168, 255), 0.12f);
	gl.SetLineColour(wxColour(82, 168, 255), 1.0f, 2);
	gl.DrawRectangle(top_left, bottom_right);

	gl.SetFillColour(wxColour(82, 168, 255), 0.95f);
	gl.SetLineColour(*wxWHITE, 0.9f, 1);
	for (auto const& corner : { top_left, Vector2D(bottom_right.X(), top_left.Y()),
		Vector2D(top_left.X(), bottom_right.Y()), bottom_right })
		gl.DrawCircle(corner, 4.0f);
}
