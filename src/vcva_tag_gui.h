#pragma once

#include <array>
#include <functional>

#include <libaegisub/color.h>

class wxWindow;

struct VcVaGradientState {
	std::array<agi::Color, 4> colors{};
	std::array<uint8_t, 4> alphas{};
	std::array<agi::Color, 4> style_colors{};
	std::array<uint8_t, 4> style_alphas{};
};

struct VcVaGradientResult {
	bool accepted = false;
	bool has_color = false;
	bool has_alpha = false;
	std::array<agi::Color, 4> colors{};
	std::array<uint8_t, 4> alphas{};
};

VcVaGradientResult ShowVcVaGradientDialog(
	wxWindow *parent,
	const VcVaGradientState& initial,
	std::function<void(const VcVaGradientState&)> preview_cb,
	std::function<void()> revert_cb);
