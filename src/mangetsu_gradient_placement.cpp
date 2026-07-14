// Copyright (c) 2026

#include "mangetsu_gradient_placement.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace mangetsu {
namespace {

std::string trim_copy(std::string str) {
	auto not_space = [](unsigned char c) { return !std::isspace(c); };
	str.erase(str.begin(), std::find_if(str.begin(), str.end(), not_space));
	str.erase(std::find_if(str.rbegin(), str.rend(), not_space).base(), str.end());
	return str;
}

bool parse_finite_decimal(std::string const& token, double& value) {
	std::string const text = trim_copy(token);
	if (text.empty())
		return false;
	char *end = nullptr;
	value = std::strtod(text.c_str(), &end);
	return end == text.c_str() + text.size() && std::isfinite(value);
}

std::string format_decimal(double value) {
	char buffer[64];
	std::snprintf(buffer, sizeof buffer, "%.12g", value == 0 ? 0.0 : value);
	return buffer;
}

} // namespace

bool IsPlacementGradientTagName(std::string const& name) {
	return name == "\\pgrd" || name == "\\1pgrd";
}

PlacementRect NormalizePlacementRect(double x1, double y1, double x2, double y2) {
	PlacementRect rect;
	if (!std::isfinite(x1) || !std::isfinite(y1) || !std::isfinite(x2) || !std::isfinite(y2))
		return rect;
	rect.valid = true;
	rect.left = std::min(x1, x2);
	rect.right = std::max(x1, x2);
	rect.top = std::min(y1, y2);
	rect.bottom = std::max(y1, y2);
	return rect;
}

std::vector<std::string> TokenizeGradientValue(std::string const& value) {
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

bool ParsePlacementGradientValue(std::string const& value, PlacementRect& rect, std::string& attached_value) {
	auto const tokens = TokenizeGradientValue(value);
	// Four coordinates, angle, and the first and final stops are mandatory.
	if (tokens.size() < 7)
		return false;

	double x1, y1, x2, y2, angle;
	if (!parse_finite_decimal(tokens[0], x1) || !parse_finite_decimal(tokens[1], y1) ||
		!parse_finite_decimal(tokens[2], x2) || !parse_finite_decimal(tokens[3], y2) ||
		!parse_finite_decimal(tokens[4], angle))
		return false;

	PlacementRect parsed = NormalizePlacementRect(x1, y1, x2, y2);
	if (!parsed.valid)
		return false;

	attached_value = "(";
	for (size_t i = 4; i < tokens.size(); ++i) {
		if (tokens[i].empty())
			return false;
		if (i != 4)
			attached_value += ",";
		attached_value += tokens[i];
	}
	attached_value += ")";
	rect = parsed;
	return true;
}

std::string FormatPlacementGradientValue(PlacementRect const& rect, std::string const& attached_value) {
	if (!rect.valid || attached_value.size() < 2 || attached_value.front() != '(' || attached_value.back() != ')')
		return attached_value;
	return "(" + format_decimal(rect.left) + "," + format_decimal(rect.top) + "," +
		format_decimal(rect.right) + "," + format_decimal(rect.bottom) + "," +
		attached_value.substr(1);
}

} // namespace mangetsu
