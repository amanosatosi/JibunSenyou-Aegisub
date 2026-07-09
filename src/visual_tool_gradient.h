// Copyright (c) 2026
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

/// @file visual_tool_gradient.h
/// @brief Mangetsu true-gradient visual typesetting tool
/// @ingroup visual_ts

#pragma once

#include "visual_tool.h"

#include <libaegisub/color.h>

#include <string>
#include <vector>

class wxButton;
class wxChoice;
class wxCommandEvent;
class wxSpinCtrl;
class wxStaticText;
class wxToolBar;

class VisualToolGradient final : public VisualToolBase {
	enum class TargetGroup {
		Main,
		Border
	};

	enum class ChannelMode {
		Color,
		Alpha
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
		std::string value;

		explicit operator bool() const { return start >= 0 && end >= start; }
	};

	TargetGroup group = TargetGroup::Main;
	ChannelMode mode = ChannelMode::Color;
	int main_index = 1;
	int border_index = 1;
	int angle = 0;
	bool dirty = false;
	bool existing = false;
	bool keep_current_on_switch = false;
	int selected_stop = 0;
	bool dragging_stop = false;

	std::vector<GradientStop> stops;
	std::vector<int> border_layers{1};
	bool show_fifth = false;

	wxToolBar *tool_bar = nullptr;
	wxSpinCtrl *angle_ctrl = nullptr;
	wxChoice *main_choice = nullptr;
	wxChoice *border_choice = nullptr;
	wxButton *color_button = nullptr;
	wxButton *alpha_button = nullptr;
	wxButton *remove_button = nullptr;
	wxStaticText *status_label = nullptr;

	Vector2D bar_p1;
	Vector2D bar_p2;

	void SetToolbar(wxToolBar *tb) override;
	void OnMouseEvent(wxMouseEvent &event) override;
	bool OnKeyDown(wxKeyEvent &event) override;
	void Draw() override;

	void OnLineChanged() override;
	void OnFileChanged() override;
	void OnFrameChanged() override;

	void BuildToolbar();
	void RefreshFromLine(bool allow_prompt);
	void RefreshAvailableTargets();
	void RefreshToolbarState();
	void SelectMain(int index);
	void SelectBorder(int index);
	void SelectMode(ChannelMode new_mode);
	bool ConfirmLoadExisting(std::string const& target_label);

	TagRef CurrentTag() const;
	std::string CurrentTargetName() const;
	std::string CurrentGroupName() const;
	std::string CurrentModeName() const;
	std::string CurrentStatus() const;

	void ResetDefaultGradient();
	bool LoadTagValue(std::string const& value);
	std::string FormatTagValue() const;
	void ApplyCurrent(bool mark_dirty = true);
	void ClearCurrent();
	void ReverseStops();
	void AddStop(double pos);
	void RemoveSelectedStop();
	void FlatZone();
	void EditSelectedStop();
	GradientStop SampleAt(double pos) const;

	TagRange FindCurrentTag(AssDialogue const *line) const;
	static TagRange FindTag(std::string const& text, std::string const& tag, std::string const& alias = "");
	static std::string ReplaceOrInsertTag(std::string const& text, TagRef const& tag, std::string const& value);
	static std::vector<std::string> TokenizeTagValue(std::string const& value);
	static bool ParseAssColor(std::string const& text, agi::Color& color);
	static bool ParseAssAlpha(std::string const& text, int& alpha);
	static std::string FormatAssAlpha(int alpha);

	int HitTestStop(Vector2D pos) const;
	double PosFromMouse(Vector2D pos) const;
	bool MouseInBar(Vector2D pos) const;
	void SortStops();

public:
	VisualToolGradient(VideoDisplay *parent, agi::Context *context);
	~VisualToolGradient();
};
