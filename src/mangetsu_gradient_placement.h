// Copyright (c) 2026
//
// Helpers shared by the Mangetsu gradient editor and its non-GUI tests.

#pragma once

#include <string>
#include <vector>

namespace mangetsu {

struct PlacementRect {
	bool valid = false;
	double left = 0;
	double top = 0;
	double right = 0;
	double bottom = 0;
};

/// The only fixed-frame gradient tags implemented by the current Mangetsu
/// branch. No channel is silently remapped to either alias.
bool IsPlacementGradientTagName(std::string const& name);

/// Normalize a script-coordinate rectangle without clipping it to the frame.
PlacementRect NormalizePlacementRect(double x1, double y1, double x2, double y2);

/// Split a parenthesized Mangetsu gradient value into its top-level arguments.
std::vector<std::string> TokenizeGradientValue(std::string const& value);

/// Parse the value of a \pgrd or \1pgrd tag. On success attached_value receives
/// the equivalent attached-gradient value, including its parentheses.
bool ParsePlacementGradientValue(std::string const& value, PlacementRect& rect, std::string& attached_value);

/// Prefix the already-formatted attached-gradient value with a normalized
/// placement rectangle. Values are deliberately compact ASS decimals.
std::string FormatPlacementGradientValue(PlacementRect const& rect, std::string const& attached_value);

} // namespace mangetsu
