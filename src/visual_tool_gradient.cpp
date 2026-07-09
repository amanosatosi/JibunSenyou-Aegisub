// Copyright (c) 2026
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

/// @file visual_tool_gradient.cpp
/// @brief Mangetsu true-gradient visual typesetting tool
/// @ingroup visual_ts

#include "visual_tool_gradient.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "compat.h"
#include "dialogs.h"
#include "include/aegisub/context.h"
#include "selection_controller.h"
#include "video_display.h"

#include <libaegisub/format.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/event.h>
#include <wx/intl.h>
#include <wx/msgdlg.h>
#include <wx/settings.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/toolbar.h>

namespace {
constexpr int ID_ANGLE_0 = 19000;
constexpr int ID_ANGLE_90 = 19001;
constexpr int ID_ANGLE_180 = 19002;
constexpr int ID_ANGLE_270 = 19003;

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
}

VisualToolGradient::VisualToolGradient(VideoDisplay *parent, agi::Context *context)
: VisualToolBase(parent, context)
{
	parent->SetCursor(wxNullCursor);
	ResetDefaultGradient();
	RefreshAvailableTargets();
	RefreshFromLine(false);
}

VisualToolGradient::~VisualToolGradient() {
}

void VisualToolGradient::SetToolbar(wxToolBar *tb) {
	tool_bar = tb;
	BuildToolbar();
}

void VisualToolGradient::BuildToolbar() {
	if (!tool_bar)
		return;

	auto add_control = [&](wxControl *control) {
		tool_bar->AddControl(control);
	};

	tool_bar->AddSeparator();
	add_control(new wxStaticText(tool_bar, wxID_ANY, _("Gradient")));

	angle_ctrl = new wxSpinCtrl(tool_bar, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(72, -1), wxSP_ARROW_KEYS, 0, 359, angle);
	angle_ctrl->SetToolTip(_("Gradient angle in degrees"));
	add_control(angle_ctrl);
	angle_ctrl->Bind(wxEVT_SPINCTRL, [=](wxCommandEvent&) {
		angle = angle_ctrl->GetValue();
		ApplyCurrent();
		parent->Render();
	});
	angle_ctrl->Bind(wxEVT_TEXT, [=](wxCommandEvent&) {
		angle = angle_ctrl->GetValue();
		ApplyCurrent();
		parent->Render();
	});

	auto add_angle_button = [&](int id, wxString const& label, int value) {
		auto *button = new wxButton(tool_bar, id, label, wxDefaultPosition, wxSize(48, -1), wxBU_EXACTFIT);
		button->SetToolTip(wxString::Format(_("%d degrees"), value));
		add_control(button);
		button->Bind(wxEVT_BUTTON, [=](wxCommandEvent&) {
			angle = value;
			angle_ctrl->SetValue(angle);
			ApplyCurrent();
			parent->Render();
		});
	};
	add_angle_button(ID_ANGLE_0, wxS("0"), 0);
	add_angle_button(ID_ANGLE_90, wxS("90"), 90);
	add_angle_button(ID_ANGLE_180, wxS("180"), 180);
	add_angle_button(ID_ANGLE_270, wxS("270"), 270);

	add_control(new wxStaticText(tool_bar, wxID_ANY, _("Main")));
	main_choice = new wxChoice(tool_bar, wxID_ANY, wxDefaultPosition, wxSize(118, -1));
	add_control(main_choice);
	main_choice->Bind(wxEVT_CHOICE, [=](wxCommandEvent&) {
		int sel = main_choice->GetSelection();
		if (sel == wxNOT_FOUND) return;
		int target = sel + 1;
		if (!show_fifth && target == 5)
			return;
		SelectMain(target);
	});

	add_control(new wxStaticText(tool_bar, wxID_ANY, _("Border")));
	border_choice = new wxChoice(tool_bar, wxID_ANY, wxDefaultPosition, wxSize(118, -1));
	add_control(border_choice);
	border_choice->Bind(wxEVT_CHOICE, [=](wxCommandEvent&) {
		int sel = border_choice->GetSelection();
		if (sel == wxNOT_FOUND || sel >= static_cast<int>(border_layers.size())) return;
		SelectBorder(border_layers[sel]);
	});

	color_button = new wxButton(tool_bar, wxID_ANY, _("Color"), wxDefaultPosition, wxSize(72, -1));
	alpha_button = new wxButton(tool_bar, wxID_ANY, _("Alpha"), wxDefaultPosition, wxSize(72, -1));
	add_control(color_button);
	add_control(alpha_button);
	color_button->Bind(wxEVT_BUTTON, [=](wxCommandEvent&) { SelectMode(ChannelMode::Color); });
	alpha_button->Bind(wxEVT_BUTTON, [=](wxCommandEvent&) { SelectMode(ChannelMode::Alpha); });

	status_label = new wxStaticText(tool_bar, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(150, -1));
	status_label->Wrap(145);
	add_control(status_label);

	auto add_op = [&](wxString const& label, wxString const& tooltip, auto fn) {
		auto *button = new wxButton(tool_bar, wxID_ANY, label, wxDefaultPosition, wxSize(90, -1), wxBU_EXACTFIT);
		button->SetToolTip(tooltip);
		add_control(button);
		button->Bind(wxEVT_BUTTON, fn);
		return button;
	};

	add_op(_("+ Stop"), _("Add a gradient stop at 50 percent"), [=](wxCommandEvent&) {
		AddStop(50.0);
	});
	remove_button = add_op(_("Remove"), _("Remove the selected middle stop"), [=](wxCommandEvent&) {
		RemoveSelectedStop();
	});
	add_op(_("Reverse"), _("Reverse gradient direction and stops"), [=](wxCommandEvent&) {
		ReverseStops();
	});
	add_op(_("Flat Zone"), _("Duplicate the selected stop value to create a flat region"), [=](wxCommandEvent&) {
		FlatZone();
	});
	add_op(_("Clear"), _("Reset the active gradient tag"), [=](wxCommandEvent&) {
		ClearCurrent();
	});

	RefreshToolbarState();
	tool_bar->Realize();
	tool_bar->Show(true);
}

void VisualToolGradient::OnLineChanged() {
	dirty = false;
	RefreshAvailableTargets();
	RefreshFromLine(false);
}

void VisualToolGradient::OnFileChanged() {
	RefreshAvailableTargets();
	RefreshFromLine(false);
}

void VisualToolGradient::OnFrameChanged() {
	RefreshAvailableTargets();
	RefreshToolbarState();
}

void VisualToolGradient::RefreshAvailableTargets() {
	border_layers = {1};
	show_fifth = false;
	if (!active_line)
		return;

	std::string text = active_line->Text.get();
	show_fifth = FindTag(text, "\\5grd") || FindTag(text, "\\5gra") || FindTag(text, "\\5c") || FindTag(text, "\\5a");

	for (int layer = 2; layer <= 10; ++layer) {
		if (has_layer_tag(text, layer))
			border_layers.push_back(layer);
	}

	if (std::find(border_layers.begin(), border_layers.end(), border_index) == border_layers.end())
		border_index = 1;
}

void VisualToolGradient::RefreshFromLine(bool allow_prompt) {
	if (!active_line) {
		existing = false;
		ResetDefaultGradient();
		RefreshToolbarState();
		parent->Render();
		return;
	}

	TagRange found = FindCurrentTag(active_line);
	if (found && dirty && allow_prompt) {
		if (!ConfirmLoadExisting(CurrentTargetName() + " " + CurrentModeName())) {
			RefreshToolbarState();
			parent->Render();
			return;
		}
	}

	if (found) {
		existing = true;
		if (!LoadTagValue(found.value))
			ResetDefaultGradient();
		dirty = false;
	}
	else {
		existing = false;
		ResetDefaultGradient();
	}

	RefreshToolbarState();
	parent->Render();
}

void VisualToolGradient::RefreshToolbarState() {
	if (angle_ctrl && angle_ctrl->GetValue() != angle)
		angle_ctrl->SetValue(angle);

	if (main_choice) {
		main_choice->Clear();
		std::vector<std::pair<int, wxString>> main_targets = {
			{1, _("Primary")},
			{2, _("Secondary")},
			{3, _("Border (1)")},
			{4, _("Shadow")}
		};
		if (show_fifth)
			main_targets.emplace_back(5, _("Fifth"));
		for (auto const& target : main_targets) {
			TargetGroup old_group = group;
			int old_main = main_index;
			group = TargetGroup::Main;
			main_index = target.first;
			bool has = static_cast<bool>(FindCurrentTag(active_line));
			group = old_group;
			main_index = old_main;
			main_choice->Append(target.second + wxString(has ? wxS(" *") : wxS("")));
		}
		if (group == TargetGroup::Main)
			main_choice->SetSelection(std::clamp(main_index - 1, 0, static_cast<int>(main_choice->GetCount()) - 1));
		else
			main_choice->SetSelection(wxNOT_FOUND);
	}

	if (border_choice) {
		border_choice->Clear();
		for (int layer : border_layers) {
			TargetGroup old_group = group;
			int old_border = border_index;
			group = TargetGroup::Border;
			border_index = layer;
			bool has = static_cast<bool>(FindCurrentTag(active_line));
			group = old_group;
			border_index = old_border;
			border_choice->Append(wxString::Format(_("Border %d"), layer) + wxString(has ? wxS(" *") : wxS("")));
		}
		if (group == TargetGroup::Border) {
			auto it = std::find(border_layers.begin(), border_layers.end(), border_index);
			border_choice->SetSelection(it == border_layers.end() ? 0 : static_cast<int>(it - border_layers.begin()));
		}
		else
			border_choice->SetSelection(wxNOT_FOUND);
	}

	if (color_button) {
		ChannelMode old_mode = mode;
		mode = ChannelMode::Color;
		bool has = static_cast<bool>(FindCurrentTag(active_line));
		mode = old_mode;
		color_button->SetLabel(mode == ChannelMode::Color ? (has ? _("[Color *]") : _("[Color]")) : (has ? _("Color *") : _("Color")));
	}
	if (alpha_button) {
		ChannelMode old_mode = mode;
		mode = ChannelMode::Alpha;
		bool has = static_cast<bool>(FindCurrentTag(active_line));
		mode = old_mode;
		alpha_button->SetLabel(mode == ChannelMode::Alpha ? (has ? _("[Alpha *]") : _("[Alpha]")) : (has ? _("Alpha *") : _("Alpha")));
	}
	if (remove_button)
		remove_button->Enable(selected_stop > 0 && selected_stop < static_cast<int>(stops.size()) - 1);
	if (status_label)
		status_label->SetLabel(to_wx(CurrentStatus()));
	if (tool_bar)
		tool_bar->Realize();
}

void VisualToolGradient::SelectMain(int index) {
	TargetGroup old_group = group;
	int old_main = main_index;
	group = TargetGroup::Main;
	main_index = index;
	if (dirty && FindCurrentTag(active_line) && !ConfirmLoadExisting(CurrentTargetName() + " " + CurrentModeName())) {
		group = old_group;
		main_index = old_main;
		RefreshToolbarState();
		return;
	}
	if (keep_current_on_switch) {
		keep_current_on_switch = false;
		existing = false;
		RefreshToolbarState();
		parent->Render();
		return;
	}
	RefreshFromLine(false);
}

void VisualToolGradient::SelectBorder(int index) {
	TargetGroup old_group = group;
	int old_border = border_index;
	group = TargetGroup::Border;
	border_index = index;
	if (dirty && FindCurrentTag(active_line) && !ConfirmLoadExisting(CurrentTargetName() + " " + CurrentModeName())) {
		group = old_group;
		border_index = old_border;
		RefreshToolbarState();
		return;
	}
	if (keep_current_on_switch) {
		keep_current_on_switch = false;
		existing = false;
		RefreshToolbarState();
		parent->Render();
		return;
	}
	RefreshFromLine(false);
}

void VisualToolGradient::SelectMode(ChannelMode new_mode) {
	ChannelMode old_mode = mode;
	mode = new_mode;
	if (dirty && FindCurrentTag(active_line) && !ConfirmLoadExisting(CurrentTargetName() + " " + CurrentModeName())) {
		mode = old_mode;
		RefreshToolbarState();
		return;
	}
	if (keep_current_on_switch) {
		keep_current_on_switch = false;
		existing = false;
		RefreshToolbarState();
		parent->Render();
		return;
	}
	RefreshFromLine(false);
}

bool VisualToolGradient::ConfirmLoadExisting(std::string const& target_label) {
	keep_current_on_switch = false;
	wxMessageDialog dialog(
		parent,
		to_wx("Existing gradient found for " + target_label + ".\nLoad existing gradient or keep current edit?"),
		_("Visual Gradient Tool"),
		wxYES_NO | wxCANCEL | wxICON_QUESTION);
	dialog.SetYesNoCancelLabels(_("Load Existing"), _("Keep Current Edit"), _("Cancel"));
	int ret = dialog.ShowModal();
	if (ret == wxCANCEL)
		return false;
	if (ret == wxNO) {
		keep_current_on_switch = true;
		return true;
	}
	return true;
}

VisualToolGradient::TagRef VisualToolGradient::CurrentTag() const {
	if (group == TargetGroup::Main) {
		if (mode == ChannelMode::Color)
			return {"\\" + std::to_string(main_index) + "grd", main_index == 3 ? "\\1bgrd" : "", "\\" + std::to_string(main_index) + "grd"};
		return {"\\" + std::to_string(main_index) + "gra", main_index == 3 ? "\\1bga" : "", "\\" + std::to_string(main_index) + "gra"};
	}

	if (mode == ChannelMode::Color)
		return {"\\" + std::to_string(border_index) + "bgrd", border_index == 1 ? "\\3grd" : "", "\\" + std::to_string(border_index) + "bgrd"};
	return {"\\" + std::to_string(border_index) + "bga", border_index == 1 ? "\\3gra" : "", "\\" + std::to_string(border_index) + "bga"};
}

std::string VisualToolGradient::CurrentTargetName() const {
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

std::string VisualToolGradient::CurrentGroupName() const {
	return group == TargetGroup::Main ? "Main" : "Border";
}

std::string VisualToolGradient::CurrentModeName() const {
	return mode == ChannelMode::Color ? "Color" : "Alpha";
}

std::string VisualToolGradient::CurrentStatus() const {
	TagRef tag = CurrentTag();
	return "Editing: " + CurrentGroupName() + " / " + CurrentTargetName() + " / " + CurrentModeName() +
		" -> " + tag.status_name + "\nExisting: " + (existing ? "yes" : "no");
}

void VisualToolGradient::ResetDefaultGradient() {
	angle = 0;
	selected_stop = 0;
	stops.clear();
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

bool VisualToolGradient::LoadTagValue(std::string const& value) {
	auto tokens = TokenizeTagValue(value);
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

std::string VisualToolGradient::FormatTagValue() const {
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

void VisualToolGradient::ApplyCurrent(bool mark_dirty) {
	if (!active_line)
		return;

	if (mark_dirty)
		dirty = true;
	existing = true;
	active_line->Text = ReplaceOrInsertTag(active_line->Text.get(), CurrentTag(), FormatTagValue());
	Commit(_("set gradient"));
	RefreshAvailableTargets();
	RefreshToolbarState();
}

void VisualToolGradient::ClearCurrent() {
	if (!active_line)
		return;
	dirty = true;
	existing = true;
	active_line->Text = ReplaceOrInsertTag(active_line->Text.get(), CurrentTag(), "()");
	Commit(_("clear gradient"));
	RefreshToolbarState();
	parent->Render();
}

void VisualToolGradient::ReverseStops() {
	angle = (angle + 180) % 360;
	for (auto& stop : stops)
		stop.pos = 100.0 - stop.pos;
	SortStops();
	selected_stop = static_cast<int>(stops.size()) - 1 - selected_stop;
	ApplyCurrent();
	parent->Render();
}

void VisualToolGradient::AddStop(double pos) {
	GradientStop stop = SampleAt(pos);
	stop.pos = clamp_percent(pos);
	stops.push_back(stop);
	SortStops();
	for (size_t i = 0; i < stops.size(); ++i) {
		if (std::abs(stops[i].pos - stop.pos) < 0.01) {
			selected_stop = static_cast<int>(i);
			break;
		}
	}
	ApplyCurrent();
	parent->Render();
}

void VisualToolGradient::RemoveSelectedStop() {
	if (selected_stop <= 0 || selected_stop >= static_cast<int>(stops.size()) - 1)
		return;
	stops.erase(stops.begin() + selected_stop);
	selected_stop = std::min<int>(selected_stop, static_cast<int>(stops.size()) - 1);
	ApplyCurrent();
	parent->Render();
}

void VisualToolGradient::FlatZone() {
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
	ApplyCurrent();
	parent->Render();
}

void VisualToolGradient::EditSelectedStop() {
	if (selected_stop < 0 || selected_stop >= static_cast<int>(stops.size()))
		return;

	if (mode == ChannelMode::Color) {
		agi::Color selected = stops[selected_stop].color;
		bool ok = GetColorFromUser(parent, selected, false, [&](agi::Color new_color) {
			stops[selected_stop].color = new_color;
			ApplyCurrent();
			parent->Render();
		});
		if (!ok) {
			stops[selected_stop].color = selected;
			ApplyCurrent();
		}
	}
	else {
		agi::Color selected(255, 255, 255, static_cast<unsigned char>(stops[selected_stop].alpha));
		bool ok = GetColorFromUser(parent, selected, true, [&](agi::Color new_color) {
			stops[selected_stop].alpha = new_color.a;
			ApplyCurrent();
			parent->Render();
		});
		if (!ok) {
			stops[selected_stop].alpha = selected.a;
			ApplyCurrent();
		}
	}
	parent->Render();
}

VisualToolGradient::GradientStop VisualToolGradient::SampleAt(double pos) const {
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

VisualToolGradient::TagRange VisualToolGradient::FindCurrentTag(AssDialogue const *line) const {
	if (!line)
		return {};
	TagRef tag = CurrentTag();
	return FindTag(line->Text.get(), tag.name, tag.alias);
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

VisualToolGradient::TagRange VisualToolGradient::FindTag(std::string const& text, std::string const& tag, std::string const& alias) {
	bool in_block = false;
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

		TagRange found;
		found.start = i;
		found.end = value_end;
		found.value = text.substr(value_start, value_end - value_start);
		return found;
	}

	return {};
}

std::string VisualToolGradient::ReplaceOrInsertTag(std::string const& text, TagRef const& tag, std::string const& value) {
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
			out.insert(close, replacement);
			return out;
		}
	}

	return "{" + replacement + "}" + text;
}

std::vector<std::string> VisualToolGradient::TokenizeTagValue(std::string const& value) {
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

bool VisualToolGradient::ParseAssColor(std::string const& text, agi::Color& color) {
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

bool VisualToolGradient::ParseAssAlpha(std::string const& text, int& alpha) {
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

std::string VisualToolGradient::FormatAssAlpha(int alpha) {
	return agi::format("&H%02X&", clamp_alpha(alpha));
}

void VisualToolGradient::SortStops() {
	std::sort(stops.begin(), stops.end(), [](GradientStop const& a, GradientStop const& b) {
		return a.pos < b.pos;
	});
	if (!stops.empty()) {
		stops.front().pos = 0.0;
		stops.back().pos = 100.0;
	}
}

int VisualToolGradient::HitTestStop(Vector2D pos) const {
	for (int i = static_cast<int>(stops.size()) - 1; i >= 0; --i) {
		double x = bar_p1.X() + (bar_p2.X() - bar_p1.X()) * stops[i].pos / 100.0;
		if (std::abs(pos.X() - x) <= 8 && pos.Y() >= bar_p1.Y() - 26 && pos.Y() <= bar_p2.Y() + 10)
			return i;
	}
	return -1;
}

double VisualToolGradient::PosFromMouse(Vector2D pos) const {
	if (bar_p2.X() <= bar_p1.X())
		return 0.0;
	return clamp_percent((pos.X() - bar_p1.X()) * 100.0 / (bar_p2.X() - bar_p1.X()));
}

bool VisualToolGradient::MouseInBar(Vector2D pos) const {
	return pos.X() >= bar_p1.X() && pos.X() <= bar_p2.X() && pos.Y() >= bar_p1.Y() && pos.Y() <= bar_p2.Y();
}

void VisualToolGradient::OnMouseEvent(wxMouseEvent &event) {
	mouse_pos = event.GetPosition();
	ctrl_down = event.CmdDown();

	if (event.Leaving()) {
		mouse_pos = Vector2D();
		parent->Render();
		return;
	}

	if (event.LeftDClick()) {
		int hit = HitTestStop(mouse_pos);
		if (hit >= 0) {
			selected_stop = hit;
			EditSelectedStop();
			RefreshToolbarState();
			return;
		}
	}

	if (event.LeftDown()) {
		int hit = HitTestStop(mouse_pos);
		if (hit >= 0) {
			selected_stop = hit;
			dragging_stop = hit > 0 && hit < static_cast<int>(stops.size()) - 1;
			if (dragging_stop)
				parent->CaptureMouse();
			RefreshToolbarState();
			parent->Render();
			return;
		}
		if (event.CmdDown() && MouseInBar(mouse_pos)) {
			AddStop(PosFromMouse(mouse_pos));
			return;
		}
	}

	if (dragging_stop && event.Dragging() && event.LeftIsDown()) {
		double min_pos = stops[selected_stop - 1].pos + 0.1;
		double max_pos = stops[selected_stop + 1].pos - 0.1;
		stops[selected_stop].pos = std::max(min_pos, std::min(max_pos, PosFromMouse(mouse_pos)));
		ApplyCurrent();
		parent->Render();
		return;
	}

	if (dragging_stop && event.LeftUp()) {
		dragging_stop = false;
		if (parent->HasCapture())
			parent->ReleaseMouse();
		commit_id = -1;
		return;
	}

	if (event.RightDown()) {
		int hit = HitTestStop(mouse_pos);
		if (hit >= 0) {
			selected_stop = hit;
			EditSelectedStop();
			RefreshToolbarState();
		}
	}
}

bool VisualToolGradient::OnKeyDown(wxKeyEvent &event) {
	if (event.GetKeyCode() != WXK_DELETE && event.GetKeyCode() != WXK_BACK)
		return false;
	if (selected_stop <= 0 || selected_stop >= static_cast<int>(stops.size()) - 1)
		return false;
	RemoveSelectedStop();
	return true;
}

void VisualToolGradient::Draw() {
	double width = std::min<double>(560.0, std::max(220.0, client_size.X() - 120.0));
	double left = video_pos.X() + std::max(30.0, (video_res.X() - width) / 2.0);
	double top = video_pos.Y() + std::max(24.0, video_res.Y() - 86.0);
	bar_p1 = Vector2D(left + 44.0, top + 28.0);
	bar_p2 = Vector2D(left + width - 44.0, top + 56.0);

	gl.SetFillColour(wxColour(245, 245, 245), 0.82f);
	gl.SetLineColour(wxColour(0, 0, 0), 0.35f, 1);
	gl.DrawRectangle(Vector2D(left, top), Vector2D(left + width, top + 74.0));

	int segments = std::max(1, static_cast<int>(bar_p2.X() - bar_p1.X()));
	for (int i = 0; i < segments; i += 3) {
		double p0 = i * 100.0 / segments;
		double p1 = std::min(100.0, (i + 3) * 100.0 / segments);
		GradientStop sample = SampleAt((p0 + p1) / 2.0);
		wxColour fill;
		float alpha = 1.0f;
		if (mode == ChannelMode::Color) {
			fill = to_wx_color(sample.color);
		}
		else {
			int shade = 255 - sample.alpha;
			fill = wxColour(shade, shade, shade);
			alpha = 0.95f;
		}
		gl.SetFillColour(fill, alpha);
		gl.SetLineColour(fill, 0.0f, 1);
		double x0 = bar_p1.X() + (bar_p2.X() - bar_p1.X()) * p0 / 100.0;
		double x1 = bar_p1.X() + (bar_p2.X() - bar_p1.X()) * p1 / 100.0;
		gl.DrawRectangle(Vector2D(x0, bar_p1.Y()), Vector2D(x1 + 1, bar_p2.Y()));
	}

	gl.SetFillColour(*wxWHITE, 0.0f);
	gl.SetLineColour(wxColour(0, 0, 0), 1.0f, 2);
	gl.DrawRectangle(bar_p1, bar_p2);

	for (size_t i = 0; i < stops.size(); ++i) {
		double x = bar_p1.X() + (bar_p2.X() - bar_p1.X()) * stops[i].pos / 100.0;
		Vector2D stem1(x, bar_p1.Y() - 12.0);
		Vector2D stem2(x, bar_p2.Y() + 7.0);
		gl.SetLineColour(wxColour(0, 0, 0), 1.0f, i == static_cast<size_t>(selected_stop) ? 4 : 2);
		gl.DrawLine(stem1, stem2);

		wxColour fill = mode == ChannelMode::Color
			? to_wx_color(stops[i].color)
			: wxColour(255 - stops[i].alpha, 255 - stops[i].alpha, 255 - stops[i].alpha);
		gl.SetFillColour(fill, 1.0f);
		gl.SetLineColour(i == static_cast<size_t>(selected_stop) ? wxColour(0, 120, 215) : wxColour(0, 0, 0), 1.0f, i == static_cast<size_t>(selected_stop) ? 3 : 2);
		gl.DrawRectangle(Vector2D(x - 9.0, bar_p1.Y() - 24.0), Vector2D(x + 9.0, bar_p1.Y() - 8.0));
	}
}
