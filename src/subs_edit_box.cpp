// Copyright (c) 2005, Rodrigo Braz Monteiro
// Copyright (c) 2010, Thomas Goyne <plorkyeran@aegisub.org>
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//   * Neither the name of the Aegisub Group nor the names of its contributors
//     may be used to endorse or promote products derived from this software
//     without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Aegisub Project http://www.aegisub.org/

/// @file subs_edit_box.cpp
/// @brief Main subtitle editing area, including toolbars around the text control
/// @ingroup main_ui

#include "subs_edit_box.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "base_grid.h"
#include "command/command.h"
#include "compat.h"
#include "dialog_style_editor.h"
#include "flyweight_hash.h"
#include "include/aegisub/context.h"
#include "include/aegisub/hotkey.h"
#include "initial_line_state.h"
#include "options.h"
#include "placeholder_ctrl.h"
#include "project.h"
#include "retina_helper.h"
#include "selection_controller.h"
#include "subs_edit_ctrl.h"
#include "text_selection_controller.h"
#include "timeedit_ctrl.h"
#include "tooltip_manager.h"
#include "utils.h"
#include "validators.h"

#include <libaegisub/character_count.h>
#include <libaegisub/make_unique.h>
#include <libaegisub/util.h>

#include <algorithm>
#include <iterator>
#include <deque>
#include <functional>
#include <set>
#include <unordered_set>
#include <vector>

#include <wx/arrstr.h>
#include <wx/bmpbuttn.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/combobox.h>
#include <wx/fontenum.h>
#include <wx/listbox.h>
#include <wx/menu.h>
#include <wx/popupwin.h>
#include <wx/radiobut.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/intl.h>
#include <wx/textentry.h>

namespace {

/// Work around wxGTK's fondness for generating events from ChangeValue
void change_value(wxTextCtrl *ctrl, wxString const& value) {
	if (value != ctrl->GetValue())
		ctrl->ChangeValue(value);
}

wxString new_value(wxComboBox *ctrl, wxCommandEvent &evt) {
#ifdef __WXGTK__
	return ctrl->GetValue();
#else
	return evt.GetString();
#endif
}

void time_edit_char_hook(wxKeyEvent &event) {
	// Force a modified event on Enter
	if (event.GetKeyCode() == WXK_RETURN) {
		TimeEdit *edit = static_cast<TimeEdit*>(event.GetEventObject());
		edit->SetValue(edit->GetValue());
	}
	else
		event.Skip();
}

// Passing a pointer-to-member directly to a function sometimes does not work
// in VC++ 2015 Update 2, with it instead passing a null pointer
const auto AssDialogue_Actor = &AssDialogue::Actor;
const auto AssDialogue_Effect = &AssDialogue::Effect;

struct CaseInsensitiveLess {
	bool operator()(wxString const& lhs, wxString const& rhs) const {
		int cmp = lhs.CmpNoCase(rhs);
		if (cmp != 0) return cmp < 0;
		return lhs.Cmp(rhs) < 0;
	}
};

// Japanese bracket options for the toolbar popup
struct BracketPair {
	wxString left;
	wxString right;
};

const std::array<BracketPair, 14> kBracketPairs = {{
	{wxS("「"), wxS("」")},
	{wxS("『"), wxS("』")},
	{wxS("〈"), wxS("〉")},
	{wxS("《"), wxS("》")},
	{wxS("【"), wxS("】")},
	{wxS("〔"), wxS("〕")},
	{wxS("〖"), wxS("〗")},
	{wxS("〘"), wxS("〙")},
	{wxS("〚"), wxS("〛")},
	{wxS("（"), wxS("）")},
	{wxS("［"), wxS("］")},
	{wxS("｛"), wxS("｝")},
	{wxS("＜"), wxS("＞")},
	{wxS("≪"), wxS("≫")}
}};

}

class SubsEditBox::FastNamePopup final : public wxPopupTransientWindow {
public:
	explicit FastNamePopup(SubsEditBox *owner);
	void UpdateContents(bool mode_enabled, bool has_active, std::deque<wxString> const& names);
	void FocusList();
	wxListBox *GetListBox() const { return list_; }
protected:
	void OnDismiss() override;
private:
	SubsEditBox *owner_;
	wxPanel *panel_;
	wxStaticText *label_;
	wxListBox *list_;
};

SubsEditBox::FastNamePopup::FastNamePopup(SubsEditBox *owner)
: wxPopupTransientWindow(owner, wxBORDER_SIMPLE)
, owner_(owner)
{
	panel_ = new wxPanel(this);
	panel_->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
	auto *sizer = new wxBoxSizer(wxVERTICAL);
	label_ = new wxStaticText(panel_, wxID_ANY, wxString());
	sizer->Add(label_, 0, wxALL, 4);
	list_ = new wxListBox(panel_, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0, nullptr, wxLB_SINGLE | wxLB_NEEDED_SB | wxWANTS_CHARS);
	list_->Bind(wxEVT_LISTBOX, &SubsEditBox::OnFastListSelect, owner_);
	list_->Bind(wxEVT_LISTBOX_DCLICK, &SubsEditBox::OnFastListDClick, owner_);
	list_->Bind(wxEVT_KEY_DOWN, &SubsEditBox::OnFastListKeyDown, owner_);
	Bind(wxEVT_CHAR_HOOK, &SubsEditBox::OnFastPopupCharHook, owner_);
	sizer->Add(list_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);
	panel_->SetSizerAndFit(sizer);
	auto *outer = new wxBoxSizer(wxVERTICAL);
	outer->Add(panel_, 1, wxEXPAND);
	SetSizerAndFit(outer);
	std::deque<wxString> empty;
	UpdateContents(false, false, empty);
}

void SubsEditBox::FastNamePopup::UpdateContents(bool mode_enabled, bool has_active, std::deque<wxString> const& names)
{
	wxString mark;
	if (!mode_enabled)
		mark = _("[off]");
	else if (has_active)
		mark = _("[active]");
	else
		mark = _("[ready]");
	wxString prefix = _(">> Fast name set mode ");
	label_->SetLabel(prefix + mark);
	list_->Freeze();
	list_->Clear();
	list_->DeselectAll();
	for (auto const& name : names)
		list_->Append(name);

	int count = static_cast<int>(names.size());
	int rows = 0;
	if (count == 0)
		rows = 3;
	else
		rows = std::clamp(count, 1, 20);

	int item_height = list_->GetCharHeight();
#ifdef __WXMSW__
	// On Windows the reported char height can be very small; ensure a sensible minimum
	if (item_height < 12)
		item_height = 16;
#endif
	if (item_height <= 0)
		item_height = 16;

	int vertical_border = list_->GetWindowBorderSize().GetHeight() * 2;
	int height = item_height * rows + vertical_border + 8;

	list_->InvalidateBestSize();
	wxSize best = list_->GetBestSize();
	int width = best.GetWidth();
	if (width <= 0)
		width = 200;

	wxSizer *sizer = list_->GetContainingSizer();
	if (sizer)
		sizer->SetItemMinSize(list_, width, height);
	wxSize new_size(width, height);
	list_->SetMinSize(new_size);
	list_->SetInitialSize(new_size);
	list_->SetClientSize(new_size);
	list_->SetSize(new_size);

	list_->Thaw();
	panel_->Fit();
	panel_->Layout();
	GetSizer()->Fit(this);
	Layout();
}

void SubsEditBox::FastNamePopup::FocusList()
{
	if (list_->GetCount() == 0) {
		list_->DeselectAll();
	}
	else if (list_->GetSelection() == wxNOT_FOUND)
		list_->SetSelection(0);
	list_->SetFocus();
}

void SubsEditBox::FastNamePopup::OnDismiss()
{
	wxPopupTransientWindow::OnDismiss();
	owner_->OnFastPopupDismiss();
}

SubsEditBox::SubsEditBox(wxWindow *parent, agi::Context *context)
: wxPanel(parent, -1, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | (OPT_GET("App/Dark Mode")->GetBool() ? wxBORDER_STATIC : wxRAISED_BORDER), "SubsEditBox")
, c(context)
, retina_helper(agi::make_unique<RetinaHelper>(parent))
, undo_timer(GetEventHandler())
{
	if (c)
		c->subsEditBox = this;

	using std::bind;

	// Top controls
	top_sizer = new wxBoxSizer(wxHORIZONTAL);

	comment_box = new wxCheckBox(this,-1,_("Comment"));
	comment_box->SetToolTip(_("Comment this line out. Commented lines don't show up on screen."));
#ifdef __WXGTK__
	// Only supported in wxgtk
	comment_box->SetCanFocus(false);
#endif
	top_sizer->Add(comment_box, wxSizerFlags().Expand().Border(wxRIGHT, 5));

	style_box = MakeComboBox("Default", wxCB_READONLY, &SubsEditBox::OnStyleChange, _("Style for this line"));

	style_edit_button = new wxButton(this, -1, _("Edit"), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	style_edit_button->Bind(wxEVT_BUTTON, [=](wxCommandEvent&) {
		if (active_style) {
			wxArrayString font_list = wxFontEnumerator::GetFacenames();
			font_list.Sort();
			DialogStyleEditor(this, active_style, c, nullptr, "", font_list).ShowModal();
		}
	});
	top_sizer->Add(style_edit_button, wxSizerFlags().Expand().Border(wxRIGHT));

	actor_placeholder_text_ = _("Actor");
	actor_box = new Placeholder<wxComboBox>(this, actor_placeholder_text_, wxDefaultSize, wxCB_DROPDOWN | wxTE_PROCESS_ENTER, _("Actor name for this speech. This is only for reference, and is mainly useless."));
	Bind(wxEVT_TEXT, &SubsEditBox::OnActorChange, this, actor_box->GetId());
	Bind(wxEVT_COMBOBOX, &SubsEditBox::OnActorChange, this, actor_box->GetId());
	actor_box->Bind(wxEVT_KEY_DOWN, &SubsEditBox::OnActorKeyDown, this);
	actor_box->Bind(wxEVT_KILL_FOCUS, &SubsEditBox::OnActorKillFocus, this);
	top_sizer->Add(actor_box, wxSizerFlags(2).Expand().Border(wxRIGHT));

	actor_fast_button_ = new wxButton(this, wxID_ANY, wxS(">"), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	actor_fast_button_->SetToolTip(_("Enable fast name set mode."));
	actor_fast_button_->Bind(wxEVT_BUTTON, &SubsEditBox::OnFastButton, this);
	top_sizer->Add(actor_fast_button_, wxSizerFlags().Align(wxALIGN_CENTER_VERTICAL).Border(wxRIGHT));

	effect_box = new Placeholder<wxComboBox>(this, _("Effect"), wxDefaultSize, wxCB_DROPDOWN | wxTE_PROCESS_ENTER, _("Effect for this line. This can be used to store extra information for karaoke scripts, or for the effects supported by the renderer."));
	Bind(wxEVT_TEXT, &SubsEditBox::OnEffectChange, this, effect_box->GetId());
	Bind(wxEVT_COMBOBOX, &SubsEditBox::OnEffectChange, this, effect_box->GetId());
	effect_box->Bind(wxEVT_KILL_FOCUS, &SubsEditBox::OnEffectKillFocus, this);
	top_sizer->Add(effect_box, wxSizerFlags(3).Expand());

	char_count = new wxTextCtrl(this, -1, "0", wxDefaultPosition, wxDefaultSize, wxTE_READONLY | wxTE_CENTER);
	char_count->SetInitialSize(char_count->GetSizeFromTextSize(GetTextExtent(wxS("000"))));
	char_count->SetToolTip(_("Number of characters in the longest line of this subtitle."));
	top_sizer->Add(char_count, wxSizerFlags().Expand());

	// Middle controls
	middle_left_sizer = new wxBoxSizer(wxHORIZONTAL);

	layer = new wxSpinCtrl(this,-1,"",wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS | wxTE_PROCESS_ENTER,0,0x7FFFFFFF,0);
#ifndef __WXGTK3__
	// GTK3 has a bug that we cannot shrink the size of a widget, so do nothing there. See:
	//  http://gtk.10911.n7.nabble.com/gtk-widget-set-size-request-stopped-working-with-GTK3-td26274.html
	//  https://trac.wxwidgets.org/ticket/18568
	layer->SetInitialSize(layer->GetSizeFromTextSize(GetTextExtent(wxS("00"))));
#endif
	layer->SetToolTip(_("Layer number"));
	middle_left_sizer->Add(layer, wxSizerFlags().Expand());
	middle_left_sizer->AddSpacer(5);

	start_time = MakeTimeCtrl(_("Start time"), TIME_START);
	end_time   = MakeTimeCtrl(_("End time"), TIME_END);
	middle_left_sizer->AddSpacer(5);
	duration   = MakeTimeCtrl(_("Line duration"), TIME_DURATION);
	middle_left_sizer->AddSpacer(5);

	margin[0] = MakeMarginCtrl(_("Left Margin (0 = default from style)"), 0, _("left margin change"));
	margin[1] = MakeMarginCtrl(_("Right Margin (0 = default from style)"), 1, _("right margin change"));
	margin[2] = MakeMarginCtrl(_("Vertical Margin (0 = default from style)"), 2, _("vertical margin change"));
	middle_left_sizer->AddSpacer(5);

	// Middle-bottom controls
	middle_right_sizer = new wxBoxSizer(wxHORIZONTAL);
	MakeButton("edit/style/bold");
	MakeButton("edit/style/italic");
	MakeButton("edit/style/underline");
	MakeButton("edit/style/strikeout");
	MakeButton("edit/font");

	// Japanese bracket insert button sits next to the existing fn control
	int icon_px = OPT_GET("App/Toolbar Icon Size")->GetInt();
	icon_px = static_cast<int>(icon_px * retina_helper->GetScaleFactor());
	wxSize bracket_size(icon_px + 6, icon_px + 6);
	bracket_button_ = new wxButton(this, wxID_ANY, wxS("『』"), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	bracket_button_->SetToolTip(_("Insert Japanese bracket pair"));
	if (bracket_size.GetWidth() > 0 && bracket_size.GetHeight() > 0) {
		wxSize min_size = bracket_button_->GetMinSize();
		min_size.SetWidth(std::max(min_size.GetWidth(), bracket_size.GetWidth()));
		min_size.SetHeight(std::max(min_size.GetHeight(), bracket_size.GetHeight()));
		bracket_button_->SetMinSize(min_size);
	}
	middle_right_sizer->Add(bracket_button_, wxSizerFlags().Expand());
	bracket_button_->Bind(wxEVT_BUTTON, &SubsEditBox::OnBracketButton, this);
	middle_right_sizer->AddSpacer(5);
	MakeButton("edit/color/primary");
	MakeButton("edit/color/secondary");
	MakeButton("edit/color/outline");
	MakeButton("edit/color/shadow");
	middle_right_sizer->AddSpacer(5);
	MakeButton("grid/line/next/create");
	middle_right_sizer->AddSpacer(10);

	by_time = MakeRadio(_("T&ime"), true, _("Time by h:mm:ss.cs"));
	by_frame = MakeRadio(_("F&rame"), false, _("Time by frame number"));
	by_frame->Enable(false);

	split_box = new wxCheckBox(this,-1,_("Show Original"));
	split_box->SetToolTip(_("Show the contents of the subtitle line when it was first selected above the edit box. This is sometimes useful when editing subtitles or translating subtitles into another language."));
	split_box->Bind(wxEVT_CHECKBOX, &SubsEditBox::OnSplit, this);
	middle_right_sizer->Add(split_box, wxSizerFlags().Expand());
	middle_right_sizer->AddSpacer(5);

	better_view_enabled_ = OPT_GET("Subtitle/Better View")->GetBool();

	better_view_box = new wxCheckBox(this, -1, _("Better View"));
	better_view_box->SetToolTip(_("Display \\N as real line breaks inside the edit box."));
	better_view_box->Bind(wxEVT_CHECKBOX, &SubsEditBox::OnBetterView, this);
	middle_right_sizer->Add(better_view_box, wxSizerFlags().Expand());
	better_view_box->SetValue(better_view_enabled_);

	// Main sizer
	wxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);
	main_sizer->Add(top_sizer, wxSizerFlags().Expand().Border(wxALL, 3));
	main_sizer->Add(middle_left_sizer, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, 3));
	main_sizer->Add(middle_right_sizer, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, 3));

	// Text editor
	edit_ctrl = new SubsTextEditCtrl(this, wxDefaultSize, (OPT_GET("App/Dark Mode")->GetBool() ? wxBORDER_SIMPLE : wxBORDER_SUNKEN), c);
	edit_ctrl->Bind(wxEVT_CHAR_HOOK, &SubsEditBox::OnKeyDown, this);

	secondary_editor = new wxTextCtrl(this, -1, "", wxDefaultPosition, wxDefaultSize, (OPT_GET("App/Dark Mode")->GetBool() ? wxBORDER_SIMPLE : wxBORDER_SUNKEN) | wxTE_MULTILINE | wxTE_READONLY);
	// Here we use the height of secondary_editor as the initial size of edit_ctrl,
	// which is more reasonable than the default given by wxWidgets.
	// See: https://trac.wxwidgets.org/ticket/18471#ticket
	//      https://github.com/wangqr/Aegisub/issues/4
	edit_ctrl->SetInitialSize(secondary_editor->GetSize());

	main_sizer->Add(secondary_editor, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, 3));
	main_sizer->Add(edit_ctrl, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, 3));
	main_sizer->Hide(secondary_editor);

	bottom_sizer = new wxBoxSizer(wxHORIZONTAL);
	bottom_sizer->Add(MakeBottomButton("edit/revert"), wxSizerFlags().Border(wxRIGHT));
	bottom_sizer->Add(MakeBottomButton("edit/clear"), wxSizerFlags().Border(wxRIGHT));
	bottom_sizer->Add(MakeBottomButton("edit/clear/text"), wxSizerFlags().Border(wxRIGHT));
	bottom_sizer->Add(MakeBottomButton("edit/insert_original"), wxSizerFlags().Border(wxRIGHT));
	join_next_button = MakeBottomButton("edit/line/join/next/translatormode");
	bottom_sizer->Add(join_next_button, wxSizerFlags().Border(wxRIGHT));
	join_last_button = MakeBottomButton("edit/line/join/last");
	bottom_sizer->Add(join_last_button);
	main_sizer->Add(bottom_sizer);
	main_sizer->Hide(bottom_sizer);

	UpdateJoinButtons();
	SetSizerAndFit(main_sizer);

	edit_ctrl->Bind(wxEVT_STC_MODIFIED, &SubsEditBox::OnChange, this);
	edit_ctrl->SetModEventMask(wxSTC_MOD_INSERTTEXT | wxSTC_MOD_DELETETEXT | wxSTC_STARTACTION);

	Bind(wxEVT_TEXT, &SubsEditBox::OnLayerEnter, this, layer->GetId());
	Bind(wxEVT_SPINCTRL, &SubsEditBox::OnLayerEnter, this, layer->GetId());
	Bind(wxEVT_CHECKBOX, &SubsEditBox::OnCommentChange, this, comment_box->GetId());

	Bind(wxEVT_CHAR_HOOK, &SubsEditBox::OnKeyDown, this);
	Bind(wxEVT_SIZE, &SubsEditBox::OnSize, this);
	Bind(wxEVT_TIMER, [=](wxTimerEvent&) { commit_id = -1; });

	wxSizeEvent evt;
	OnSize(evt);

	file_changed_slot = c->ass->AddCommitListener(&SubsEditBox::OnCommit, this);
	connections = agi::signal::make_vector({
		context->project->AddTimecodesListener(&SubsEditBox::UpdateFrameTiming, this),
		context->selectionController->AddActiveLineListener(&SubsEditBox::OnActiveLineChanged, this),
		context->selectionController->AddSelectionListener(&SubsEditBox::OnSelectedSetChanged, this),
		context->initialLineState->AddChangeListener(&SubsEditBox::OnLineInitialTextChanged, this),
	 });

	context->textSelectionController->SetControl(edit_ctrl);
	edit_ctrl->SetFocus();

	bool show_original = OPT_GET("Subtitle/Show Original")->GetBool();
	if (show_original) {
		split_box->SetValue(true);
		DoOnSplit(true);

	}
}


SubsEditBox::~SubsEditBox() {
	if (c && c->subsEditBox == this)
		c->subsEditBox = nullptr;
	c->textSelectionController->SetControl(nullptr);
}

wxTextCtrl *SubsEditBox::MakeMarginCtrl(wxString const& tooltip, int margin, wxString const& commit_msg) {
	wxTextCtrl *ctrl = new wxTextCtrl(this, -1, "", wxDefaultPosition, wxDefaultSize, wxTE_CENTRE | wxTE_PROCESS_ENTER, IntValidator(0, true));
	ctrl->SetInitialSize(ctrl->GetSizeFromTextSize(GetTextExtent(wxS("0000"))));
	ctrl->SetMaxLength(5);
	ctrl->SetToolTip(tooltip);
	middle_left_sizer->Add(ctrl, wxSizerFlags().Expand());

	Bind(wxEVT_TEXT, [=](wxCommandEvent&) {
		int value = agi::util::mid(-9999, atoi(ctrl->GetValue().utf8_str()), 99999);
		SetSelectedRows([&](AssDialogue *d) { d->Margin[margin] = value; },
			commit_msg, AssFile::COMMIT_DIAG_META);
	}, ctrl->GetId());

	return ctrl;
}

TimeEdit *SubsEditBox::MakeTimeCtrl(wxString const& tooltip, TimeField field) {
	TimeEdit *ctrl = new TimeEdit(this, -1, c, "", wxDefaultSize, field == TIME_END);
	ctrl->SetInitialSize(ctrl->GetSizeFromTextSize(GetTextExtent(wxS("0:00:00.000"))));
	ctrl->SetToolTip(tooltip);
	Bind(wxEVT_TEXT, [=](wxCommandEvent&) { CommitTimes(field); }, ctrl->GetId());
	ctrl->Bind(wxEVT_CHAR_HOOK, time_edit_char_hook);
	middle_left_sizer->Add(ctrl, wxSizerFlags().Expand());
	return ctrl;
}

void SubsEditBox::MakeButton(const char *cmd_name) {
	cmd::Command *command = cmd::get(cmd_name);
	wxBitmapButton *btn = new wxBitmapButton(this, -1, command->Icon(OPT_GET("App/Toolbar Icon Size")->GetInt(), retina_helper->GetScaleFactor()));
	ToolTipManager::Bind(btn, command->StrHelp(), "Subtitle Edit Box", cmd_name);

	middle_right_sizer->Add(btn, wxSizerFlags().Expand());
	btn->Bind(wxEVT_BUTTON, std::bind(&SubsEditBox::CallCommand, this, cmd_name));
}

bool SubsEditBox::InsertTextAtCaret(wxString const& text) {
	if (!line || text.empty())
		return false;

	int sel_start = edit_ctrl->GetSelectionStart();
	int sel_end = edit_ctrl->GetSelectionEnd();
	int start = std::min(sel_start, sel_end);
	int end = std::max(sel_start, sel_end);

	std::string text_utf8 = from_wx(text);

	edit_ctrl->BeginUndoAction();
	if (start != end) {
		edit_ctrl->SetTargetStart(start);
		edit_ctrl->SetTargetEnd(end);
		edit_ctrl->ReplaceTarget(text);
	}
	else {
		edit_ctrl->InsertText(start, text);
	}

	int caret = start + static_cast<int>(text_utf8.size());
	edit_ctrl->SetSelection(caret, caret);
	edit_ctrl->GotoPos(caret);
	edit_ctrl->EndUndoAction();
	FocusTextCtrl();
	return true;
}

void SubsEditBox::FocusTextCtrl() {
	if (edit_ctrl)
		edit_ctrl->SetFocus();
}

void SubsEditBox::InsertBracketPair(wxString const& left, wxString const& right) {
	// Wrap selected text or insert a bracket pair at the caret.
	if (!line) return;

	int sel_start = edit_ctrl->GetSelectionStart();
	int sel_end = edit_ctrl->GetSelectionEnd();
	int start = std::min(sel_start, sel_end);
	int end = std::max(sel_start, sel_end);

	std::string left_utf8 = from_wx(left);
	int left_len = static_cast<int>(left_utf8.size());

	edit_ctrl->BeginUndoAction();
	if (start == end) {
		edit_ctrl->InsertText(start, left + right);
		int caret = start + left_len;
		edit_ctrl->SetSelection(caret, caret);
		edit_ctrl->GotoPos(caret);
	}
	else {
		wxString selected = edit_ctrl->GetTextRange(start, end);
		std::string selected_utf8 = from_wx(selected);
		edit_ctrl->SetTargetStart(start);
		edit_ctrl->SetTargetEnd(end);
		edit_ctrl->ReplaceTarget(left + selected + right);
		int new_start = start + left_len;
		int new_end = new_start + static_cast<int>(selected_utf8.size());
		edit_ctrl->SetSelection(new_start, new_end);
		edit_ctrl->GotoPos(new_end);
	}
	edit_ctrl->EndUndoAction();
	edit_ctrl->SetFocus();
}

void SubsEditBox::OnBracketButton(wxCommandEvent &) {
	// Build a small popup to choose the bracket flavor, defaulting to the last pick.
	if (!line) return;
	if (last_bracket_pair_index_ >= kBracketPairs.size())
		last_bracket_pair_index_ = std::min<size_t>(1, kBracketPairs.size() - 1);

	int count = static_cast<int>(kBracketPairs.size());
	int base_id = wxWindow::NewControlId(count);
	wxMenu menu;
	for (size_t i = 0; i < kBracketPairs.size(); ++i) {
		int id = base_id + static_cast<int>(i);
		wxString label = kBracketPairs[i].left + wxS(" ") + kBracketPairs[i].right;
		menu.Append(id, label);
	}

	menu.Bind(wxEVT_MENU, [=](wxCommandEvent &evt) {
		size_t index = static_cast<size_t>(evt.GetId() - base_id);
		if (index >= kBracketPairs.size()) return;
		last_bracket_pair_index_ = index;
		InsertBracketPair(kBracketPairs[index].left, kBracketPairs[index].right);
		if (bracket_button_) {
			bracket_button_->SetLabel(kBracketPairs[index].left + kBracketPairs[index].right);
		}
	});

	wxPoint pos = bracket_button_ ? bracket_button_->GetPosition() : wxPoint(0, 0);
	pos.y += bracket_button_ ? bracket_button_->GetSize().GetHeight() : 0;
	PopupMenu(&menu, pos);
	wxWindow::UnreserveControlId(base_id, count);
}

wxButton *SubsEditBox::MakeBottomButton(const char *cmd_name) {
	cmd::Command *command = cmd::get(cmd_name);
	wxButton *btn = new wxButton(this, -1, command->StrDisplay(c));
	ToolTipManager::Bind(btn, command->StrHelp(), "Subtitle Edit Box", cmd_name);

	btn->Bind(wxEVT_BUTTON, std::bind(&SubsEditBox::CallCommand, this, cmd_name));
	return btn;
}

void SubsEditBox::UpdateJoinButtons() {
	if (!join_next_button || !join_last_button) return;
	cmd::Command *next_cmd = cmd::get("edit/line/join/next");
	cmd::Command *last_cmd = cmd::get("edit/line/join/last");
	join_next_button->Enable(next_cmd->Validate(c));
	join_last_button->Enable(last_cmd->Validate(c));
}


wxComboBox *SubsEditBox::MakeComboBox(wxString const& initial_text, int style, void (SubsEditBox::*handler)(wxCommandEvent&), wxString const& tooltip) {
	wxString styles[] = { "Default" };
	wxComboBox *ctrl = new wxComboBox(this, -1, initial_text, wxDefaultPosition, wxDefaultSize, 1, styles, style | wxTE_PROCESS_ENTER);
	ctrl->SetToolTip(tooltip);
	top_sizer->Add(ctrl, wxSizerFlags(2).Expand().Border(wxRIGHT));
	Bind(wxEVT_COMBOBOX, handler, this, ctrl->GetId());
	return ctrl;
}

wxRadioButton *SubsEditBox::MakeRadio(wxString const& text, bool start, wxString const& tooltip) {
	wxRadioButton *ctrl = new wxRadioButton(this, -1, text, wxDefaultPosition, wxDefaultSize, start ? wxRB_GROUP : 0);
	ctrl->SetValue(start);
	ctrl->SetToolTip(tooltip);
	Bind(wxEVT_RADIOBUTTON, &SubsEditBox::OnFrameTimeRadio, this, ctrl->GetId());
	middle_right_sizer->Add(ctrl, wxSizerFlags().Expand().Border(wxRIGHT));
	return ctrl;
}

void SubsEditBox::OnCommit(int type) {
	wxEventBlocker blocker(this);

	initial_times.clear();

	if (type == AssFile::COMMIT_NEW || type & AssFile::COMMIT_STYLES) {
		wxString style = style_box->GetValue();
		style_box->Clear();
		style_box->Append(to_wx(c->ass->GetStyles()));
		style_box->Select(style_box->FindString(style));
		active_style = line ? c->ass->GetStyle(line->Style) : nullptr;
	}

	if (type == AssFile::COMMIT_NEW) {
		PopulateList(effect_box, AssDialogue_Effect);
		PopulateActorList();
		return;
	}
	else if (type & AssFile::COMMIT_STYLES)
		style_box->Select(style_box->FindString(to_wx(line->Style)));

	if (!(type ^ AssFile::COMMIT_ORDER)) return;

	SetControlsState(!!line);
	UpdateFields(type, true);
}

void SubsEditBox::UpdateFields(int type, bool repopulate_lists) {
	if (!line) return;

	if (type & AssFile::COMMIT_DIAG_TIME) {
		start_time->SetTime(line->Start);
		end_time->SetTime(line->End);
		SetDurationField();
	}

	if (type & AssFile::COMMIT_DIAG_TEXT) {
		// Satoshi: \N visual newline support
		if (better_view_enabled_) {
			wxString raw_text = to_wx(line->Text.get());
			wxString display_text = MakeDisplayText(raw_text);
			edit_ctrl->SetTextTo(from_wx(display_text));
		}
		else {
			edit_ctrl->SetTextTo(line->Text);
		}
		// Satoshi: \N visual newline support (end)
		UpdateCharacterCount(line->Text);
	}

	if (type & AssFile::COMMIT_DIAG_META) {
		layer->SetValue(line->Layer);
		for (size_t i = 0; i < margin.size(); ++i)
			change_value(margin[i], std::to_wstring(line->Margin[i]));
		comment_box->SetValue(line->Comment);
		style_box->Select(style_box->FindString(to_wx(line->Style)));
		active_style = line ? c->ass->GetStyle(line->Style) : nullptr;
		style_edit_button->Enable(active_style != nullptr);

		if (repopulate_lists) PopulateList(effect_box, AssDialogue_Effect);
		effect_box->ChangeValue(to_wx(line->Effect));
		effect_box->SetStringSelection(to_wx(line->Effect));
		effect_text_amend_ = false;

		if (repopulate_lists) PopulateActorList();
		actor_box->ChangeValue(to_wx(line->Actor));
		actor_box->SetStringSelection(to_wx(line->Actor));
		actor_text_amend_ = false;
		actor_should_autofill_ = false;
		actor_has_pending_selection_ = false;
		actor_selection_start_ = 0;
		actor_selection_end_ = 0;
	}
	else if (repopulate_lists && (type & AssFile::COMMIT_DIAG_ADDREM)) {
		PopulateActorList();
	}
	UpdateJoinButtons();
}

void SubsEditBox::PopulateList(wxComboBox *combo, boost::flyweight<std::string> AssDialogue::*field) {
	wxEventBlocker blocker(this);

	std::unordered_set<boost::flyweight<std::string>> values;
	for (auto const& line : c->ass->Events) {
		auto const& value = line.*field;
		if (!value.get().empty())
			values.insert(value);
	}

	wxArrayString arrstr;
	arrstr.reserve(values.size());
	transform(values.begin(), values.end(), std::back_inserter(arrstr),
		(wxString (*)(std::string const&))to_wx);

	arrstr.Sort();

	combo->Freeze();
	long pos = combo->GetInsertionPoint();
	wxString value = combo->GetValue();

	combo->Set(arrstr);
	combo->ChangeValue(value);
	combo->SetStringSelection(value);
	combo->SetInsertionPoint(pos);
	combo->Thaw();
}

void SubsEditBox::PopulateActorList() {
	wxEventBlocker blocker(this);

	long sel_start = 0;
	long sel_end = 0;
	actor_box->GetSelection(&sel_start, &sel_end);
	long insertion_point = actor_box->GetInsertionPoint();
	bool had_pending = actor_has_pending_selection_;

	std::set<wxString, CaseInsensitiveLess> unique;
	for (auto const& entry : c->ass->Events) {
		if (entry.Comment) continue;

		wxString actor = to_wx(entry.Actor);
		actor.Trim(true);
		actor.Trim(false);
		if (!actor.empty())
			unique.insert(actor);
	}

	actor_values_.assign(unique.begin(), unique.end());

	wxArrayString arr;
	arr.reserve(actor_values_.size());
	for (auto const& value : actor_values_)
		arr.push_back(value);

	actor_box->Freeze();
	long pos = actor_box->GetInsertionPoint();
	wxString value = actor_box->GetValue();
	wxString trimmed_leading = value;
	trimmed_leading.Trim(true);
	bool removed_leading = trimmed_leading.length() != value.length();
	wxString trimmed_value = trimmed_leading;

	actor_box->Set(arr);
	actor_box->ChangeValue(value);
	if (!actor_box->SetStringSelection(value) && removed_leading)
		actor_box->SetStringSelection(trimmed_value);
	actor_box->SetInsertionPoint(pos);
	actor_box->Thaw();

	long length = actor_box->GetValue().length();
	auto clamp = [length](long v) {
		if (v < 0) return 0L;
		if (v > length) return length;
		return v;
	};

	long restore_start = had_pending ? actor_selection_start_ : sel_start;
	long restore_end = had_pending ? actor_selection_end_ : sel_end;
	long restore_caret = had_pending ? actor_selection_end_ : insertion_point;

	restore_start = clamp(restore_start);
	restore_end = clamp(restore_end);
	restore_caret = clamp(restore_caret);

	if (restore_end > restore_start)
		actor_box->SetSelection(restore_start, restore_end);
	else
		actor_box->SetInsertionPoint(restore_caret);

	if (!had_pending) {
		actor_selection_start_ = restore_start;
		actor_selection_end_ = restore_end;
	}

	wxString preview_name;
	if (fast_preview_index_ >= 0 && fast_preview_index_ < static_cast<int>(fast_recent_names_.size()))
		preview_name = fast_recent_names_[fast_preview_index_];

	std::deque<wxString> filtered;
	for (auto const& name : fast_recent_names_) {
		wxString trimmed = name;
		trimmed.Trim(true);
		trimmed.Trim(false);
		if (trimmed.empty())
			continue;
		if (!actor_placeholder_text_.empty() && trimmed.CmpNoCase(actor_placeholder_text_) == 0)
			continue;
		if (std::find(filtered.begin(), filtered.end(), trimmed) != filtered.end())
			continue;
		filtered.push_back(trimmed);
	}
	fast_recent_names_.swap(filtered);
	if (!preview_name.empty()) {
		preview_name.Trim(true);
		preview_name.Trim(false);
		auto it = std::find(fast_recent_names_.begin(), fast_recent_names_.end(), preview_name);
		if (it != fast_recent_names_.end())
			fast_preview_index_ = static_cast<int>(std::distance(fast_recent_names_.begin(), it));
		else
			fast_preview_index_ = -1;
	}
	else if (fast_preview_index_ >= static_cast<int>(fast_recent_names_.size())) {
		fast_preview_index_ = -1;
	}
	if (fast_mode_enabled_)
		UpdateFastPopup();
}

void SubsEditBox::AutoFillActor() {
	if (actor_autofill_guard) return;
	if (actor_values_.empty()) return;

	wxString current = actor_box->GetValue();
	long insertion_point = actor_box->GetInsertionPoint();

	if (insertion_point <= 0) return;
	if (insertion_point > static_cast<long>(current.length())) return;
	if (insertion_point != static_cast<long>(current.length())) return;

	wxString prefix = current.Left(insertion_point);

	wxString trimmed = prefix;
	trimmed.Trim(true);
	trimmed.Trim(false);
	if (trimmed.empty()) return;
	if (trimmed.length() != prefix.length()) return;

	wxString lookup = trimmed.Lower();
	for (auto const& candidate : actor_values_) {
		wxString const candidate_lower = candidate.Lower();
		if (!candidate_lower.StartsWith(lookup))
			continue;

		if (candidate_lower.length() == lookup.length())
			return;

		actor_autofill_guard = true;
		actor_box->ChangeValue(candidate);
		long sel_start = lookup.length();
		long sel_end = candidate.length();
		actor_box->SetSelection(sel_start, sel_end);
		actor_selection_start_ = sel_start;
		actor_selection_end_ = sel_end;
		actor_has_pending_selection_ = true;
		actor_autofill_guard = false;
		break;
	}
}

void SubsEditBox::OnActorKeyDown(wxKeyEvent &evt) {
	actor_should_autofill_ = false;
	actor_has_pending_selection_ = false;

	int key_code = evt.GetKeyCode();
	int unicode = evt.GetUnicodeKey();
	bool modifier = evt.ControlDown() || evt.CmdDown() || evt.MetaDown() || evt.AltDown();
	bool printable = false;
	if (!modifier) {
		if (unicode != WXK_NONE)
			printable = unicode >= 32 && unicode < WXK_START;
		else
			printable = key_code >= 32 && key_code < WXK_START;
	}
	if (key_code == WXK_BACK || key_code == WXK_DELETE)
		printable = false;

	bool handled_fast_nav = false;
	if (fast_mode_enabled_ && !modifier &&
		(key_code == WXK_DOWN || key_code == WXK_UP || key_code == WXK_PAGEUP ||
		 key_code == WXK_PAGEDOWN || key_code == WXK_HOME || key_code == WXK_END)) {

		if (!fast_recent_names_.empty()) {
			ShowFastPopup(false);
			if (fast_popup_) {
				if (wxListBox *list = fast_popup_->GetListBox()) {
					int count = list->GetCount();
					if (count > 0) {
						int sel = list->GetSelection();
						if (sel == wxNOT_FOUND && fast_preview_index_ >= 0 && fast_preview_index_ < count)
							sel = fast_preview_index_;
						if (sel == wxNOT_FOUND)
							sel = 0;

						int new_sel = sel;
						switch (key_code) {
						case WXK_DOWN:
						case WXK_NUMPAD_DOWN:
							new_sel = (sel + 1 < count) ? sel + 1 : 0;   // wrap
							break;
						case WXK_UP:
						case WXK_NUMPAD_UP:
							new_sel = (sel > 0) ? sel - 1 : (count - 1); // wrap
							break;
						case WXK_PAGEUP:
							new_sel = std::max(0, sel - 10);
							break;
						case WXK_PAGEDOWN:
							new_sel = std::min(count - 1, sel + 10);
							break;
						case WXK_HOME:
							new_sel = 0;
							break;
						case WXK_END:
							new_sel = count - 1;
							break;
						default:
							break;
						}

						fast_list_changing_ = true;
						list->SetSelection(new_sel);
						PreviewFastSelection(new_sel, false);
						fast_list_changing_ = false;
						handled_fast_nav = true;
					}
				}
			}
		}

		if (handled_fast_nav) {
			evt.StopPropagation();
			evt.Skip(false);
			return;
		}
	}

	if (fast_mode_enabled_ && fast_popup_visible_ && !modifier &&
		(key_code == WXK_RETURN || key_code == WXK_NUMPAD_ENTER)) {
		if (auto *list = fast_popup_ ? fast_popup_->GetListBox() : nullptr) {
			int sel = list->GetSelection();
			if (sel != wxNOT_FOUND) {
				ApplyFastRecentSelection(sel, /*hide_popup=*/true, /*update_mru=*/true);
				evt.StopPropagation();
				evt.Skip(false);
				return;
			}
		}
		evt.StopPropagation();
		evt.Skip(false);
		return;
	}

	if (fast_mode_enabled_ && !modifier &&
		(key_code == WXK_TAB || key_code == WXK_RIGHT || key_code == WXK_NUMPAD_RIGHT)) {
		bool preview_applied = false;
		bool has_candidate = false;
		if (fast_preview_index_ >= 0) {
			ApplyFastRecentSelection(fast_preview_index_);
			preview_applied = true;
			has_candidate = true;
		}
		else if (fast_popup_) {
			wxListBox *list = fast_popup_->GetListBox();
			if (list) {
				int sel = list->GetSelection();
				if (sel != wxNOT_FOUND) {
					ApplyFastRecentSelection(sel);
					preview_applied = true;
					has_candidate = true;
				}
			}
		}
		if (!has_candidate && key_code != WXK_TAB) {
			actor_should_autofill_ = printable && key_code != WXK_SPACE;
			evt.Skip();
			return;
		}
		FinalizeFastActiveFromActor(!preview_applied);
		actor_should_autofill_ = false;
		evt.StopPropagation();
		evt.Skip(false);
		return;
	}

	actor_should_autofill_ = printable && key_code != WXK_SPACE;
	evt.Skip();
}

void SubsEditBox::OnActorKillFocus(wxFocusEvent &evt) {
	evt.Skip();
	actor_text_amend_ = false;
	if (!fast_mode_enabled_)
		return;
	fast_preview_active_ = false;
	if (fast_preview_index_ >= 0) {
		fast_preview_index_ = -1;
		if (line && actor_box) {
			actor_autofill_guard = true;
			wxString current = to_wx(line->Actor);
			actor_box->ChangeValue(current);
			actor_box->SetSelection(current.length(), current.length());
			actor_text_amend_ = false;
			actor_autofill_guard = false;
		}
		return;
	}
	fast_preview_index_ = -1;
	FinalizeFastActiveFromActor(true);
}

void SubsEditBox::OnEffectKillFocus(wxFocusEvent &evt) {
	evt.Skip();
	effect_text_amend_ = false;
}

void SubsEditBox::AddFastRecentName(wxString const& name) {
	if (!fast_mode_enabled_)
		return;

	wxString trimmed = name;
	trimmed.Trim(true);
	trimmed.Trim(false);
	if (trimmed.empty())
		return;
	if (!actor_placeholder_text_.empty() && trimmed.CmpNoCase(actor_placeholder_text_) == 0)
		return;

	auto it = std::find(fast_recent_names_.begin(), fast_recent_names_.end(), trimmed);
	if (it != fast_recent_names_.end())
		fast_recent_names_.erase(it);

	fast_recent_names_.push_front(trimmed);
	static constexpr size_t kMaxRecent = 20;
	if (fast_recent_names_.size() > kMaxRecent)
		fast_recent_names_.pop_back();

	fast_active_name_ = trimmed;
	fast_has_active_name_ = true;
	UpdateFastPopup();

	if (fast_popup_ && fast_popup_visible_) {
		wxListBox *list = fast_popup_->GetListBox();
		if (list && list->GetCount() > 0) {
			fast_list_changing_ = true;
			list->SetSelection(0);
			fast_list_changing_ = false;
			fast_preview_index_ = 0;
			PreviewFastSelection(0, true);
		}
	}
}

void SubsEditBox::FinalizeFastActiveFromActor(bool add_to_recent) {
	if (!fast_mode_enabled_ || !actor_box)
		return;

	wxString value = actor_box->GetValue();
	value.Trim(true);
	value.Trim(false);

	if (value.empty()) {
		ClearFastActiveName();
		return;
	}

	if (add_to_recent)
		AddFastRecentName(value);
	else if (!fast_has_active_name_ || fast_active_name_.Cmp(value) != 0) {
		fast_active_name_ = value;
		fast_has_active_name_ = true;
		UpdateFastPopup();
	}
}

void SubsEditBox::ClearFastActiveName() {
	if (!fast_has_active_name_ && fast_active_name_.empty())
		return;

	fast_active_name_.clear();
	fast_has_active_name_ = false;
	UpdateFastPopup();
}

void SubsEditBox::ApplyFastActiveToCurrentLine() {
	if (!fast_mode_enabled_ || !fast_has_active_name_ || fast_active_name_.empty())
		return;
	if (!line || !actor_box)
		return;

	wxString current = to_wx(line->Actor);
	current.Trim(true);
	current.Trim(false);
	if (!current.empty())
		return;

	wxString value = fast_active_name_;

	actor_autofill_guard = true;
	actor_box->ChangeValue(value);
	actor_text_amend_ = false;
	actor_box->SetSelection(0, value.length());
	actor_autofill_guard = false;
	actor_should_autofill_ = false;
	actor_has_pending_selection_ = true;
	actor_selection_start_ = 0;
	actor_selection_end_ = value.length();

	auto fly_value = boost::flyweight<std::string>(from_wx(value));
	SetSelectedRows([&, fly_value](AssDialogue *d) {
		if (d == line)
			d->Actor = fly_value;
	}, _("actor change"), AssFile::COMMIT_DIAG_META);

	PopulateActorList();
}

void SubsEditBox::ToggleFastMode() {
	fast_mode_enabled_ = !fast_mode_enabled_;
	if (actor_fast_button_) {
		actor_fast_button_->SetLabel(fast_mode_enabled_ ? wxS(">>") : wxS(">"));
		actor_fast_button_->SetToolTip(fast_mode_enabled_
			? _("Disable fast name set mode.")
			: _("Enable fast name set mode."));
		Layout();
	}
	if (fast_mode_enabled_) {
		fast_active_name_.clear();
		fast_has_active_name_ = false;
		fast_preview_active_ = false;
		fast_preview_index_ = -1;
		fast_target_row_ = -1;
		fast_target_line_ = nullptr;
		actor_selection_start_ = 0;
		actor_selection_end_ = 0;
		UpdateFastPopup();
		ShowFastPopup(false);
	}
	else {
		fast_preview_active_ = false;
		fast_preview_index_ = -1;
		fast_target_row_ = -1;
		fast_target_line_ = nullptr;
		actor_has_pending_selection_ = false;
		actor_selection_start_ = 0;
		actor_selection_end_ = 0;
		HideFastPopup();
		fast_active_name_.clear();
		fast_has_active_name_ = false;
		UpdateFastPopup();
	}
}

void SubsEditBox::UpdateFastPopup() {
	if (!fast_popup_)
		return;

	fast_popup_->UpdateContents(fast_mode_enabled_, fast_has_active_name_, fast_recent_names_);
}

void SubsEditBox::ShowFastPopup(bool focus_list) {
	if (!fast_mode_enabled_)
		return;

	if (!actor_box)
		return;

	if (!fast_popup_)
		fast_popup_ = new FastNamePopup(this);

	bool was_visible = fast_popup_visible_;
	UpdateFastPopup();

	wxListBox *list = fast_popup_->GetListBox();
	if (!list)
		return;

	if (!was_visible) {
		list->DeselectAll();
		fast_preview_active_ = false;
		fast_preview_index_ = -1;
		fast_target_line_ = line;
		fast_target_row_ = line ? line->Row : -1;
		if (c) {
			if (auto *active = c->selectionController->GetActiveLine()) {
				fast_target_line_ = active;
				fast_target_row_ = active->Row;
			}
		}
	}

	wxSize size = fast_popup_->GetBestSize();
	int actor_width = actor_box->GetSize().GetWidth();
	if (size.GetWidth() < actor_width)
		size.SetWidth(actor_width);
	fast_popup_->SetSize(size);

	wxPoint anchor = actor_box->ClientToScreen(wxPoint(0, actor_box->GetSize().GetHeight()));
	wxPoint pos(anchor.x, anchor.y + 4);
	int screen_height = wxSystemSettings::GetMetric(wxSYS_SCREEN_Y);
	if (screen_height > 0 && pos.y + size.GetHeight() > screen_height)
		pos.y = anchor.y - size.GetHeight() - actor_box->GetSize().GetHeight() - 4;
	if (fast_popup_visible_)
		fast_popup_->Move(pos);
	else {
		fast_popup_->SetPosition(pos);
		wxWindow *anchor = actor_fast_button_ ? static_cast<wxWindow*>(actor_fast_button_) : static_cast<wxWindow*>(actor_box);
		fast_popup_->Popup(anchor);
		fast_popup_visible_ = true;
	}

	if (focus_list && list) {
		if (list->GetSelection() == wxNOT_FOUND && list->GetCount() > 0) {
			fast_list_changing_ = true;
			list->SetSelection(0);
			fast_list_changing_ = false;
		}
		list->SetFocus();
	}
}

void SubsEditBox::HideFastPopup() {
	if (!fast_popup_ || !fast_popup_visible_)
		return;

	fast_popup_visible_ = false;
	fast_popup_->Dismiss();
	fast_preview_active_ = false;
	fast_preview_index_ = -1;
	fast_target_row_ = -1;
	fast_target_line_ = nullptr;
}

void SubsEditBox::OnFastPopupDismiss() {
	fast_popup_visible_ = false;
	fast_preview_active_ = false;
	fast_preview_index_ = -1;
	fast_target_row_ = -1;
	fast_target_line_ = nullptr;
	actor_has_pending_selection_ = false;
	actor_selection_start_ = 0;
	actor_selection_end_ = 0;
	if (actor_box)
		actor_box->SetFocus();
}

void SubsEditBox::OnFastPopupCharHook(wxKeyEvent &evt) {
	int code = evt.GetKeyCode();
	switch (code) {
	case WXK_UP:
	case WXK_NUMPAD_UP:
	case WXK_DOWN:
	case WXK_NUMPAD_DOWN:
	case WXK_PAGEUP:
	case WXK_PAGEDOWN:
	case WXK_HOME:
	case WXK_END:
	case WXK_RETURN:
	case WXK_NUMPAD_ENTER:
		evt.StopPropagation();
		evt.Skip(false);
		return;
	default:
		break;
	}
	evt.Skip();
}

void SubsEditBox::PreviewFastSelection(int index, bool keep_popup_focus) {
	if (!fast_popup_ || !actor_box)
		return;
	wxListBox *list = fast_popup_->GetListBox();
	if (!list)
		return;
	if (index < 0 || index >= list->GetCount())
		return;
	wxString name = list->GetString(index);
	if (name.empty())
		return;

	fast_preview_index_ = index;
	fast_preview_active_ = true;
	actor_autofill_guard = true;
	actor_box->ChangeValue(name);
	actor_box->SetSelection(0, name.length());
	actor_autofill_guard = false;
	actor_should_autofill_ = false;
	actor_has_pending_selection_ = true;
	actor_selection_start_ = 0;
	actor_selection_end_ = name.length();
	if (keep_popup_focus && list->IsShown())
		list->SetFocus();
}

void SubsEditBox::ApplyFastRecentSelection(int index, bool hide_popup, bool update_mru, bool restore_focus) {
	fast_preview_active_ = false;
	int applied_index = -1;
	wxString name;
	if (fast_popup_) {
		wxListBox *list = fast_popup_->GetListBox();
		if (list && index >= 0 && index < list->GetCount()) {
			name = list->GetString(index);
			applied_index = index;
		}
	}
	if (name.empty()) {
		if (index < 0 || index >= static_cast<int>(fast_recent_names_.size()))
			return;
		name = fast_recent_names_[index];
		applied_index = index;
	}
	if (name.empty())
		return;

	fast_preview_index_ = applied_index;
	actor_autofill_guard = true;
	actor_box->ChangeValue(name);
	actor_text_amend_ = false;
	actor_box->SetSelection(0, name.length());
	actor_should_autofill_ = false;
	actor_has_pending_selection_ = true;
	actor_selection_start_ = 0;
	actor_selection_end_ = name.length();

	auto fly_value = boost::flyweight<std::string>(from_wx(name));
	AssDialogue *target_line = fast_target_line_;
	bool needs_reresolve = fast_target_row_ >= 0 && (!target_line || target_line->Row != fast_target_row_);
	if (needs_reresolve)
		target_line = nullptr;
	if (needs_reresolve && c && c->ass) {
		for (auto &event_line : c->ass->Events) {
			if (event_line.Row == fast_target_row_) {
				target_line = &event_line;
				break;
			}
		}
	}
	if (!target_line && c)
		target_line = c->selectionController->GetActiveLine();
	if (!target_line)
		target_line = line;

	if (target_line) {
		target_line->Actor = fly_value;
		fast_target_line_ = target_line;
		fast_target_row_ = target_line->Row;
		Commit(_("actor change"), AssFile::COMMIT_DIAG_META, false, target_line);
	}
	else {
		SetSelectedRows(AssDialogue_Actor, name, _("actor change"), AssFile::COMMIT_DIAG_META, false);
		fast_target_line_ = nullptr;
		fast_target_row_ = -1;
	}
	PopulateActorList();

	if (actor_box && restore_focus) {
		actor_box->SetFocus();
		actor_box->SetSelection(0, name.length());
	}

	if (hide_popup) {
		if (update_mru)
			AddFastRecentName(name);
		actor_autofill_guard = false;
		HideFastPopup();
	}
	else {
		if (fast_mode_enabled_) {
			fast_active_name_ = name;
			fast_has_active_name_ = true;
		}
		actor_autofill_guard = false;
		if (fast_popup_) {
			wxListBox *list = fast_popup_->GetListBox();
			if (list) {
				if (index >= 0 && index < list->GetCount())
					list->SetSelection(index);
				list->Refresh();
			}
		}
	}
}

void SubsEditBox::OnFastButton(wxCommandEvent &) {
	ToggleFastMode();
	if (fast_mode_enabled_) {
		ShowFastPopup(true);
		if (fast_popup_) {
			if (auto *list = fast_popup_->GetListBox()) {
				if (list->GetCount() > 0) {
					if (list->GetSelection() == wxNOT_FOUND) {
						fast_list_changing_ = true;
						list->SetSelection(0);
						fast_list_changing_ = false;
					}
					int sel = list->GetSelection();
					if (sel != wxNOT_FOUND)
						PreviewFastSelection(sel, true);
				}
			}
		}
	}
	else if (actor_box) {
		actor_box->SetFocus();
	}
}

void SubsEditBox::OnFastListSelect(wxCommandEvent &evt) {
	if (!fast_popup_)
		return;

	if (fast_list_changing_) {
		evt.StopPropagation();
		evt.Skip(false);
		return;
	}

	int sel = evt.GetSelection();
	if (sel == wxNOT_FOUND)
		return;

	evt.StopPropagation();
	evt.Skip(false);
	PreviewFastSelection(sel, true);
}

void SubsEditBox::OnFastListDClick(wxCommandEvent &evt) {
	if (!fast_popup_) {
		evt.Skip();
		return;
	}

	int sel = evt.GetSelection();
	if (sel == wxNOT_FOUND && fast_popup_)
		sel = fast_popup_->GetListBox()->GetSelection();
	if (sel != wxNOT_FOUND) {
		ApplyFastRecentSelection(sel);
		evt.StopPropagation();
		evt.Skip(false);
		return;
	}
	evt.Skip();
}

void SubsEditBox::OnFastListKeyDown(wxKeyEvent &evt) {
	if (!fast_popup_) {
		evt.Skip();
		return;
	}

	wxListBox *list = fast_popup_->GetListBox();
	int count = list->GetCount();
	if (count == 0) {
		evt.StopPropagation();
		evt.Skip(false);
		return;
	}

	int sel = list->GetSelection();
	if (sel == wxNOT_FOUND && fast_preview_index_ >= 0 && fast_preview_index_ < count)
		sel = fast_preview_index_;
	if (sel == wxNOT_FOUND)
		sel = 0;

	int key_code = evt.GetKeyCode();
	bool handled = false;
	int new_sel = sel;

	switch (key_code) {
	case WXK_DOWN:
	case WXK_NUMPAD_DOWN:
		// Wrap: last -> 0
		new_sel = (sel + 1 < count) ? sel + 1 : 0;
		handled = true;
		break;
	case WXK_UP:
	case WXK_NUMPAD_UP:
		// Wrap: 0 -> last
		new_sel = (sel > 0) ? sel - 1 : (count - 1);
		handled = true;
		break;
	case WXK_PAGEUP:
		new_sel = std::max(0, sel - 10);
		handled = true;
		break;
	case WXK_PAGEDOWN:
		new_sel = std::min(count - 1, sel + 10);
		handled = true;
		break;
	case WXK_HOME:
		new_sel = 0;
		handled = true;
		break;
	case WXK_END:
		new_sel = count - 1;
		handled = true;
		break;
	case WXK_RETURN:
	case WXK_NUMPAD_ENTER:
		// Apply using the *current* selection and swallow Enter
		sel = list->GetSelection();
		if (sel != wxNOT_FOUND) {
			ApplyFastRecentSelection(sel, /*hide_popup=*/true, /*update_mru=*/true);
			if (actor_box)
				actor_box->SetFocus();
			evt.StopPropagation();
			evt.Skip(false);
			return;
		}
		evt.StopPropagation();
		evt.Skip(false);
		return;
	case WXK_ESCAPE:
		actor_has_pending_selection_ = false;
		actor_selection_start_ = 0;
		actor_selection_end_ = 0;
		HideFastPopup();
		if (actor_box)
			actor_box->SetFocus();
		return;
	default:
		break;
	}

	if (handled) {
		fast_list_changing_ = true;
		list->SetSelection(new_sel);
		PreviewFastSelection(new_sel, true);
		fast_list_changing_ = false;
		evt.StopPropagation();
		evt.Skip(false);
		return;
	}

	evt.Skip();
}

void SubsEditBox::OnActiveLineChanged(AssDialogue *new_line) {
	if (fast_mode_enabled_ && line && !fast_preview_active_)
		FinalizeFastActiveFromActor(true);
	if (fast_mode_enabled_) {
		fast_preview_active_ = false;
		fast_preview_index_ = -1;
	}
	wxEventBlocker blocker(this);
	line = new_line;
	commit_id = -1;

	UpdateFields(AssFile::COMMIT_DIAG_FULL, false);
	actor_should_autofill_ = false;
	actor_has_pending_selection_ = false;
	actor_selection_start_ = 0;
	actor_selection_end_ = 0;
	if (fast_mode_enabled_)
		ApplyFastActiveToCurrentLine();
	if (fast_mode_enabled_) {
		fast_target_line_ = new_line;
		fast_target_row_ = new_line ? new_line->Row : -1;
	}
}

void SubsEditBox::OnSelectedSetChanged() {
	initial_times.clear();
}

void SubsEditBox::OnLineInitialTextChanged(std::string const& new_text) {
	(void)new_text;
	if (split_box->IsChecked())
		UpdateSecondaryEditor();
}

void SubsEditBox::UpdateFrameTiming(agi::vfr::Framerate const& fps) {
	if (fps.IsLoaded()) {
		by_frame->Enable(true);
	}
	else {
		by_frame->Enable(false);
		by_time->SetValue(true);
		start_time->SetByFrame(false);
		end_time->SetByFrame(false);
		duration->SetByFrame(false);
		c->subsGrid->SetByFrame(false);
	}
}

void SubsEditBox::OnKeyDown(wxKeyEvent &event) {
	if (!osx::ime::process_key_event(edit_ctrl, event))
		hotkey::check("Subtitle Edit Box", c, event);
}

void SubsEditBox::OnChange(wxStyledTextEvent &event) {
	if (!line) return;

	auto raw_buffer = edit_ctrl->GetTextRaw();
	std::string control_text(raw_buffer.data(), raw_buffer.length());

	// Satoshi: \N visual newline support
	std::string normalized_text;
	if (better_view_enabled_) {
		wxString display_text = to_wx(control_text);
		wxString ass_text = MakeAssText(display_text);
		normalized_text = from_wx(ass_text);
	}
	else {
		normalized_text = control_text;
	}
	// Satoshi: \N visual newline support (end)

	if (normalized_text != line->Text.get()) {
		if (event.GetModificationType() & wxSTC_STARTACTION)
			commit_id = -1;
		CommitText(_("modify text"));
		UpdateCharacterCount(line->Text);
	}
}

void SubsEditBox::Commit(wxString const& desc, int type, bool amend, AssDialogue *line) {
	file_changed_slot.Block();
	commit_id = c->ass->Commit(desc, type, (amend && desc == last_commit_type) ? commit_id : -1, line);
	file_changed_slot.Unblock();
	last_commit_type = desc;
	last_time_commit_type = -1;
	initial_times.clear();
	undo_timer.Start(30000, wxTIMER_ONE_SHOT);
}

template<class setter>
void SubsEditBox::SetSelectedRows(setter set, wxString const& desc, int type, bool amend) {
	auto const& sel = c->selectionController->GetSelectedSet();
	for_each(sel.begin(), sel.end(), set);
	Commit(desc, type, amend, sel.size() == 1 ? *sel.begin() : nullptr);
}

template<class T>
void SubsEditBox::SetSelectedRows(T AssDialogueBase::*field, T value, wxString const& desc, int type, bool amend) {
	SetSelectedRows([&](AssDialogue *d) { d->*field = value; }, desc, type, amend);
}

template<class T>
void SubsEditBox::SetSelectedRows(T AssDialogueBase::*field, wxString const& value, wxString const& desc, int type, bool amend) {
	boost::flyweight<std::string> conv_value(from_wx(value));
	SetSelectedRows([&](AssDialogue *d) { d->*field = conv_value; }, desc, type, amend);
}

void SubsEditBox::CommitText(wxString const& desc) {
	auto data = edit_ctrl->GetTextRaw();
	std::string control_text(data.data(), data.length());
	// Satoshi: \N visual newline support
	std::string normalized_text;
	if (better_view_enabled_) {
		wxString ass_text = MakeAssText(to_wx(control_text));
		normalized_text = from_wx(ass_text);
	}
	else {
		normalized_text = control_text;
	}
	// Satoshi: \N visual newline support (end)
	SetSelectedRows(&AssDialogue::Text, boost::flyweight<std::string>(normalized_text), desc, AssFile::COMMIT_DIAG_TEXT, true);
}

void SubsEditBox::CommitTimes(TimeField field) {
	auto const& sel = c->selectionController->GetSelectedSet();
	for (AssDialogue *d : sel) {
		if (!initial_times.count(d))
			initial_times[d] = {d->Start, d->End};

		switch (field) {
			case TIME_START:
				initial_times[d].first = d->Start = start_time->GetTime();
				d->End = std::max(d->Start, initial_times[d].second);
				break;

			case TIME_END:
				initial_times[d].second = d->End = end_time->GetTime();
				d->Start = std::min(d->End, initial_times[d].first);
				break;

			case TIME_DURATION:
				if (by_frame->GetValue()) {
					auto const& fps = c->project->Timecodes();
					d->End = fps.TimeAtFrame(fps.FrameAtTime(d->Start, agi::vfr::START) + duration->GetFrame() - 1, agi::vfr::END);
				}
				else
					d->End = d->Start + duration->GetTime();
				initial_times[d].second = d->End;
				break;
		}
	}

	start_time->SetTime(line->Start);
	end_time->SetTime(line->End);

	if (field != TIME_DURATION)
		SetDurationField();

	if (field != last_time_commit_type)
		commit_id = -1;

	last_time_commit_type = field;
	file_changed_slot.Block();
	commit_id = c->ass->Commit(_("modify times"), AssFile::COMMIT_DIAG_TIME, commit_id, sel.size() == 1 ? *sel.begin() : nullptr);
	file_changed_slot.Unblock();
}

void SubsEditBox::SetDurationField() {
	// With VFR, the frame count calculated from the duration in time can be
	// completely wrong (since the duration is calculated as if it were a start
	// time), so we need to explicitly set it with the correct units.
	if (by_frame->GetValue())
		duration->SetFrame(end_time->GetFrame() - start_time->GetFrame() + 1);
	else
		duration->SetTime(end_time->GetTime() - start_time->GetTime());
}

void SubsEditBox::OnSize(wxSizeEvent &evt) {
	int availableWidth = GetVirtualSize().GetWidth();
	int midMin = middle_left_sizer->GetMinSize().GetWidth();
	int botMin = middle_right_sizer->GetMinSize().GetWidth();

	if (button_bar_split) {
		if (availableWidth > midMin + botMin) {
			GetSizer()->Detach(middle_right_sizer);
			middle_left_sizer->Add(middle_right_sizer,0,wxALIGN_CENTER_VERTICAL);
			button_bar_split = false;
		}
	}
	else {
		if (availableWidth < midMin) {
			middle_left_sizer->Detach(middle_right_sizer);
			GetSizer()->Insert(2,middle_right_sizer,0,wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,3);
			button_bar_split = true;
		}
	}

	evt.Skip();
}

void SubsEditBox::OnFrameTimeRadio(wxCommandEvent &event) {
	event.Skip();

	bool byFrame = by_frame->GetValue();
	start_time->SetByFrame(byFrame);
	end_time->SetByFrame(byFrame);
	duration->SetByFrame(byFrame);
	c->subsGrid->SetByFrame(byFrame);

	SetDurationField();
}

void SubsEditBox::SetControlsState(bool state) {
	if (state == controls_enabled) return;
	controls_enabled = state;

	Enable(state);
	if (!state) {
		wxEventBlocker blocker(this);
		edit_ctrl->SetTextTo("");
	}
	UpdateJoinButtons();
}

void SubsEditBox::OnSplit(wxCommandEvent&) {
	bool show_original = split_box->IsChecked();
	DoOnSplit(show_original);
	OPT_SET("Subtitle/Show Original")->SetBool(show_original);
}

static wxString ConvertAssVisualBreaks(wxString const& src) {
	wxString out;
	out.reserve(src.length());

	for (size_t i = 0; i < src.length(); ++i) {
		wxUniChar ch = src[i];
		if (ch == '\\' && i + 1 < src.length() && src[i + 1] == 'N') {
			out += '\n';
			++i;
		}
		else {
			out += ch;
		}
	}

	return out;
}

void SubsEditBox::OnBetterView(wxCommandEvent&) {
	bool new_state = better_view_box->IsChecked();
	if (better_view_enabled_ == new_state) return;

	auto buffer = edit_ctrl->GetTextRaw();
	std::string current_text(buffer.data(), buffer.length());
	wxString wx_text = to_wx(current_text);

	wxString converted = new_state ? ConvertAssVisualBreaks(wx_text) : EditorDisplayToAss(wx_text);

	better_view_enabled_ = new_state;
	OPT_SET("Subtitle/Better View")->SetBool(new_state);
	edit_ctrl->SetTextTo(from_wx(converted));

	UpdateSecondaryEditor();
}

void SubsEditBox::DoOnSplit(bool show_original) {
	Freeze();
	GetSizer()->Show(secondary_editor, show_original);
	GetSizer()->Show(bottom_sizer, show_original);
	Fit();
	SetMinSize(GetSize());
	wxSizer* parent_sizer = GetParent()->GetSizer();
	if (parent_sizer) parent_sizer->Layout();
	Thaw();

	if (show_original)
		UpdateSecondaryEditor();
	UpdateJoinButtons();
}

void SubsEditBox::UpdateSecondaryEditor() {
	if (!split_box || !split_box->IsChecked() || !c || !c->initialLineState) return;
	wxString raw_text = to_wx(c->initialLineState->GetInitialText());
	secondary_editor->SetValue(MakeDisplayText(raw_text));
}

wxString SubsEditBox::MakeDisplayText(wxString const& raw) const {
	if (!better_view_enabled_)
		return raw;
	return ConvertAssVisualBreaks(raw);
}

wxString SubsEditBox::MakeAssText(wxString const& display) const {
	if (!better_view_enabled_)
		return display;
	return EditorDisplayToAss(display);
}

void SubsEditBox::OnStyleChange(wxCommandEvent &evt) {
	SetSelectedRows(&AssDialogue::Style, new_value(style_box, evt), _("style change"), AssFile::COMMIT_DIAG_META);
	active_style = c->ass->GetStyle(line->Style);
}

void SubsEditBox::OnActorChange(wxCommandEvent &evt) {
	bool is_text = evt.GetEventType() == wxEVT_TEXT;
	if (is_text) {
		if (!actor_autofill_guard && actor_should_autofill_)
			AutoFillActor();
		actor_should_autofill_ = false;
	}
	else {
		actor_should_autofill_ = false;
		actor_has_pending_selection_ = false;
	}

	if (actor_autofill_guard)
		return;

	if (fast_preview_active_) {
		fast_preview_active_ = false;
		long const len = actor_box->GetValue().length();
		actor_has_pending_selection_ = true;
		actor_selection_start_ = 0;
		actor_selection_end_ = len;
		return;
	}

	if (!is_text)
		actor_text_amend_ = false;

	wxString value = actor_box->GetValue();
	bool amend = is_text && actor_text_amend_;
	actor_text_amend_ = is_text;
	SetSelectedRows(AssDialogue_Actor, value, _("actor change"), AssFile::COMMIT_DIAG_META, amend);
	if (fast_mode_enabled_)
		FinalizeFastActiveFromActor(false);
	PopulateActorList();
	if (fast_mode_enabled_) {
		wxString trimmed = value;
		trimmed.Trim(true);
		trimmed.Trim(false);
		if (trimmed.empty())
			ClearFastActiveName();
		else if (!is_text)
			AddFastRecentName(trimmed);
	}
	if (actor_has_pending_selection_) {
		long const length = actor_box->GetValue().length();
		long start = std::min<long>(actor_selection_start_, length);
		long end = std::min<long>(actor_selection_end_, length);
		if (end > start)
			actor_box->SetSelection(start, end);
	}
	actor_has_pending_selection_ = false;
	actor_selection_start_ = 0;
	actor_selection_end_ = 0;
}


void SubsEditBox::OnLayerEnter(wxCommandEvent &evt) {
	SetSelectedRows(&AssDialogue::Layer, evt.GetInt(), _("layer change"), AssFile::COMMIT_DIAG_META);
}

void SubsEditBox::OnEffectChange(wxCommandEvent &evt) {
	bool is_text = evt.GetEventType() == wxEVT_TEXT;
	if (!is_text)
		effect_text_amend_ = false;

	bool amend = is_text && effect_text_amend_;
	effect_text_amend_ = is_text;
	SetSelectedRows(AssDialogue_Effect, new_value(effect_box, evt), _("effect change"), AssFile::COMMIT_DIAG_META, amend);
	PopulateList(effect_box, AssDialogue_Effect);
}

void SubsEditBox::OnCommentChange(wxCommandEvent &evt) {
	SetSelectedRows(&AssDialogue::Comment, !!evt.GetInt(), _("comment change"), AssFile::COMMIT_DIAG_META);
}

void SubsEditBox::CallCommand(const char *cmd_name) {
	cmd::call(cmd_name, c);
	edit_ctrl->SetFocus();
}

void SubsEditBox::UpdateCharacterCount(std::string const& text) {
	int ignore = agi::IGNORE_BLOCKS;
	if (OPT_GET("Subtitle/Character Counter/Ignore Whitespace")->GetBool())
		ignore |= agi::IGNORE_WHITESPACE;
	if (OPT_GET("Subtitle/Character Counter/Ignore Punctuation")->GetBool())
		ignore |= agi::IGNORE_PUNCTUATION;
	size_t length = agi::MaxLineLength(text, ignore);
	char_count->SetValue(std::to_wstring(length));
	size_t limit = (size_t)OPT_GET("Subtitle/Character Limit")->GetInt();
	if (limit && length > limit)
		char_count->SetBackgroundColour(to_wx(OPT_GET("Colour/Subtitle/Syntax/Background/Error")->GetColor()));
	else
		char_count->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
}


