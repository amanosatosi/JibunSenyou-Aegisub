// Copyright (c) 2026
// Lifetime state shared by the modeless gradient dialog and its temporary tool.

#pragma once

#include "mangetsu_gradient_placement.h"

#include <functional>
#include <memory>
#include <string>

class AssDialogue;
class VisualToolBase;

struct GradientPlacementSession {
	std::unique_ptr<VisualToolBase> previous_tool;
	AssDialogue *original_line = nullptr;
	bool ending = false;
	bool dragging = false;
	mangetsu::PlacementRect rectangle;
	std::string pending_gradient_value;
	std::function<void(mangetsu::PlacementRect const&)> accepted;
	std::function<void()> invalid_drag;
	std::function<void()> cancelled;
	std::function<void(char const*)> status;
	std::function<void()> deactivated;
};
