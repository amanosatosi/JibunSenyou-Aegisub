// Copyright (c) 2026
// Temporary fixed-frame Mangetsu gradient placement tool.

#pragma once

#include "gradient_placement_session.h"
#include "visual_tool.h"

#include <memory>

class VisualToolGradientPlacement final : public VisualToolBase {
	std::shared_ptr<GradientPlacementSession> session;
	Vector2D drag_current;

	bool InVideoArea(Vector2D const& point) const;
	Vector2D ClampToVideo(Vector2D const& point) const;
	void CancelDrag(char const* message);
	void RequestEnd();
	void FinishDrag();
	void NotifyDeactivated();

	void OnMouseCaptureLost(wxMouseCaptureLostEvent&) override;

public:
	VisualToolGradientPlacement(VideoDisplay *parent, agi::Context *context,
		std::shared_ptr<GradientPlacementSession> session);
	~VisualToolGradientPlacement();

	void OnAttached() override;
	bool OnKeyEvent(wxKeyEvent &event) override;
	void OnMouseEvent(wxMouseEvent &event) override;
	void Draw() override;
};
