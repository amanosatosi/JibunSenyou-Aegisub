// Copyright (c) 2005, Rodrigo Braz Monteiro
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

#include "video_box.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "compat.h"
#include "format.h"
#include "include/aegisub/context.h"
#include "include/aegisub/toolbar.h"
#include "options.h"
#include "project.h"
#include "selection_controller.h"
#include "subs_edit_box.h"
#include "toast_popup.h"
#include "video_controller.h"
#include "video_display.h"
#include "video_slider.h"

#include <boost/range/algorithm/binary_search.hpp>
#include <cmath>
#include <wx/clipbrd.h>
#include <wx/cursor.h>
#include <wx/dataobj.h>
#include <wx/combobox.h>
#include <wx/choice.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/textctrl.h>
#include <wx/toolbar.h>

VideoBox::VideoBox(wxWindow *parent, bool isDetached, agi::Context *context)
: wxPanel(parent, -1)
, context(context)
{
	auto videoSlider = new VideoSlider(this, context);
	videoSlider->SetToolTip(_("Seek video"));

	auto mainToolbar = toolbar::GetToolbar(this, "video", context, "Video", false);

	VideoPosition = new wxTextCtrl(this, -1, "", wxDefaultPosition, wxSize(80, -1), wxTE_READONLY);
	VideoPosition->SetMinSize(wxSize(60, -1));
	VideoPosition->SetToolTip(_("Current frame time and number"));

	VideoSubsPos = new wxTextCtrl(this, -1, "", wxDefaultPosition, wxSize(80, -1), wxTE_READONLY);
	VideoSubsPos->SetMinSize(wxSize(60, -1));
	VideoSubsPos->SetToolTip(_("Time of this frame relative to start and end of current subs"));
	VideoSubsPos->SetCursor(wxCursor(wxCURSOR_HAND));
	VideoSubsPos->Bind(wxEVT_LEFT_DOWN, &VideoBox::OnSubsReadoutClick, this);

	static const double playback_speeds[] = {
		0.25, 0.50, 0.75, 1.00, 1.25, 1.50, 1.75, 2.00,
		2.25, 2.50, 2.75, 3.00, 3.25, 3.50, 3.75, 4.00
	};
	static wxString playback_speed_labels[WXSIZEOF(playback_speeds)];
	static bool playback_labels_init = false;
	if (!playback_labels_init) {
		for (size_t i = 0; i < WXSIZEOF(playback_speeds); ++i)
			playback_speed_labels[i] = wxString::Format("%.2fx", playback_speeds[i]);
		playback_labels_init = true;
	}

	wxArrayString playback_speed_choices;
	for (auto const& label : playback_speed_labels)
		playback_speed_choices.Add(label);

	VideoPlaybackSpeed = new wxChoice(this, -1, wxDefaultPosition, wxDefaultSize, playback_speed_choices);
	VideoPlaybackSpeed->SetToolTip(_("Video playback speed"));
	VideoPlaybackSpeed->SetMinSize(wxSize(70, -1));

	auto playback_speed_to_index = [](double speed) {
		int best = 2; // 1.00x
		double best_dist = std::abs(speed - playback_speeds[best]);
		for (int i = 0; i < (int)WXSIZEOF(playback_speeds); ++i) {
			double dist = std::abs(speed - playback_speeds[i]);
			if (dist < best_dist) {
				best = i;
				best_dist = dist;
			}
		}
		return best;
	};

	VideoPlaybackSpeed->SetSelection(playback_speed_to_index(context->videoController->GetPlaybackSpeed()));
	VideoPlaybackSpeed->Bind(wxEVT_CHOICE, [=](wxCommandEvent&) {
		int sel = VideoPlaybackSpeed->GetSelection();
		if (sel >= 0 && sel < (int)WXSIZEOF(playback_speeds))
			context->videoController->SetPlaybackSpeed(playback_speeds[sel]);
	});

	wxArrayString choices;
	for (int i = 1; i <= 24; ++i)
		choices.Add(fmt_wx("%g%%", i * 12.5));
	auto zoomBox = new wxComboBox(this, -1, "75%", wxDefaultPosition, wxDefaultSize, choices, wxCB_DROPDOWN | wxTE_PROCESS_ENTER);

	auto visualToolBar = toolbar::GetToolbar(this, "visual_tools", context, "Video", true);
	auto visualSubToolBar = new wxToolBar(this, -1, wxDefaultPosition, wxDefaultSize, wxTB_VERTICAL | wxTB_BOTTOM | wxTB_NODIVIDER | wxTB_FLAT);

	auto videoDisplay = new VideoDisplay(visualSubToolBar, isDetached, zoomBox, this, context);
	videoDisplay->MoveBeforeInTabOrder(videoSlider);

	auto toolbarSizer = new wxBoxSizer(wxVERTICAL);
	toolbarSizer->Add(visualToolBar, wxSizerFlags(1));
	toolbarSizer->Add(visualSubToolBar, wxSizerFlags());

	auto topSizer = new wxBoxSizer(wxHORIZONTAL);
	topSizer->Add(toolbarSizer, 0, wxEXPAND);
	topSizer->Add(videoDisplay, isDetached, isDetached ? wxEXPAND : 0);

	auto videoBottomSizer = new wxBoxSizer(wxHORIZONTAL);
	videoBottomSizer->Add(mainToolbar, wxSizerFlags(0).Center());
	videoBottomSizer->Add(VideoPosition, wxSizerFlags(1).Center().Border(wxLEFT));
	videoBottomSizer->Add(VideoSubsPos, wxSizerFlags(1).Center().Border(wxLEFT));
	videoBottomSizer->Add(VideoPlaybackSpeed, wxSizerFlags(0).Center().Border(wxLEFT));
	videoBottomSizer->Add(zoomBox, wxSizerFlags(0).Center().Border(wxLEFT | wxRIGHT));

	auto VideoSizer = new wxBoxSizer(wxVERTICAL);
	VideoSizer->Add(topSizer, 1, wxEXPAND, 0);
	VideoSizer->Add(new wxStaticLine(this), 0, wxEXPAND, 0);
	VideoSizer->Add(videoSlider, 0, wxEXPAND, 0);
	VideoSizer->Add(videoBottomSizer, 0, wxEXPAND | wxBOTTOM, 5);
	SetSizer(VideoSizer);

	UpdateTimeBoxes();

	connections = agi::signal::make_vector({
		context->ass->AddCommitListener(&VideoBox::UpdateTimeBoxes, this),
		context->project->AddKeyframesListener(&VideoBox::UpdateTimeBoxes, this),
		context->project->AddTimecodesListener(&VideoBox::UpdateTimeBoxes, this),
		context->project->AddVideoProviderListener(&VideoBox::UpdateTimeBoxes, this),
		context->selectionController->AddSelectionListener(&VideoBox::UpdateTimeBoxes, this),
		context->videoController->AddSeekListener(&VideoBox::UpdateTimeBoxes, this),
		context->videoController->AddPlaybackSpeedListener([=](double speed) {
			if (!VideoPlaybackSpeed) return;
			int new_sel = playback_speed_to_index(speed);
			if (new_sel != VideoPlaybackSpeed->GetSelection())
				VideoPlaybackSpeed->SetSelection(new_sel);
		}),
	});
}

void VideoBox::UpdateTimeBoxes() {
	subs_offset_readout_.clear();
	subs_remaining_readout_.clear();
	if (!context->project->VideoProvider()) return;

	int frame = context->videoController->GetFrameN();
	int time = context->videoController->TimeAtFrame(frame, agi::vfr::EXACT);

	// Set the text box for frame number and time
	VideoPosition->SetValue(fmt_wx("%s - %d", agi::Time(time).GetAssFormatted(true), frame));
	if (boost::binary_search(context->project->Keyframes(), frame)) {
		// Set the background color to indicate this is a keyframe
		VideoPosition->SetBackgroundColour(to_wx(OPT_GET("Colour/Subtitle Grid/Background/Selection")->GetColor()));
		VideoPosition->SetForegroundColour(to_wx(OPT_GET("Colour/Subtitle Grid/Selection")->GetColor()));
	}
	else {
		VideoPosition->SetBackgroundColour(wxNullColour);
		VideoPosition->SetForegroundColour(wxNullColour);
	}

	AssDialogue *active_line = context->selectionController->GetActiveLine();
	if (!active_line) {
		VideoSubsPos->SetValue("");
	}
	else {
		int offset = time - active_line->Start;
		int remaining = time - active_line->End;
		subs_offset_readout_ = fmt_wx("%+dms", offset);
		subs_remaining_readout_ = fmt_wx("%+dms", remaining);
		VideoSubsPos->SetValue(fmt_wx("%s; %s", subs_offset_readout_, subs_remaining_readout_));
	}
}

void VideoBox::OnSubsReadoutClick(wxMouseEvent &event) {
	event.Skip(false);
	wxString value;
	if (GetSubsReadoutForPosition(event.GetPosition(), value))
		HandleReadoutClick(value);
}

bool VideoBox::GetSubsReadoutForPosition(wxPoint const& position, wxString &value) {
	if (!VideoSubsPos || subs_offset_readout_.IsEmpty() || subs_remaining_readout_.IsEmpty())
		return false;

	wxString current = VideoSubsPos->GetValue();
	if (current.IsEmpty())
		return false;

	wxCoord text_width = 0;
	wxCoord text_height = 0;
	VideoSubsPos->GetTextExtent(subs_offset_readout_ + "; ", &text_width, &text_height);
	wxCoord client_width = VideoSubsPos->GetClientSize().GetWidth();
	if (text_width <= 0 || text_width >= client_width)
		text_width = client_width / 2;

	int x = position.x;
	if (x < 0) x = 0;
	if (client_width > 0 && x > client_width) x = client_width;

	if (x <= text_width)
		value = subs_offset_readout_;
	else
		value = subs_remaining_readout_;
	return true;
}

bool VideoBox::HandleReadoutClick(wxString const& value) {
	if (value.IsEmpty() || value == wxS("---"))
		return false;

	wxString normalized = NormalizeReadout(value);
	if (normalized.IsEmpty())
		return false;

	int action = OPT_GET("Video/Click Time Readout Action")->GetInt();
	if (action == 3)
		return false;
	bool want_copy = action == 0 || action == 2;
	bool want_insert = action == 1 || action == 2;

	bool copied = false;
	bool inserted = false;
	if (want_copy)
		copied = CopyReadoutToClipboard(normalized);
	if (want_insert)
		inserted = InsertReadoutIntoEditBox(normalized);

	if (!copied && !inserted)
		return false;

	if (context && context->subsEditBox)
		context->subsEditBox->FocusTextCtrl();

	if (!OPT_GET("Video/Disable Click Popup")->GetBool()) {
		wxWindow *toast_parent = context && context->parent ? context->parent : this;
		if (copied && inserted)
			ShowToast(toast_parent, _("Copied and inserted"));
		else if (copied)
			ShowToast(toast_parent, _("Copied to clipboard"));
		else
			ShowToast(toast_parent, _("Inserted into edit box"));
	}
	return true;
}

bool VideoBox::CopyReadoutToClipboard(wxString const& value) {
	wxClipboard *cb = wxClipboard::Get();
	if (!cb || !cb->Open())
		return false;

	bool ok = cb->SetData(new wxTextDataObject(value));
	if (ok)
		cb->Flush();
	cb->Close();
	return ok;
}

bool VideoBox::InsertReadoutIntoEditBox(wxString const& value) {
	if (!context || !context->subsEditBox)
		return false;
	return context->subsEditBox->InsertTextAtCaret(value);
}

wxString VideoBox::NormalizeReadout(wxString const& value) const {
	wxString trimmed = value;
	trimmed.Trim(true).Trim(false);
	if (trimmed.IsEmpty())
		return wxString();

	if (trimmed.StartsWith("+") || trimmed.StartsWith("-"))
		trimmed = trimmed.Mid(1);

	wxString lower = trimmed.Lower();
	if (lower.EndsWith("ms")) {
		trimmed.Truncate(trimmed.length() - 2);
		trimmed.Trim(true).Trim(false);
	}

	if (trimmed == wxS("---") || trimmed.IsEmpty())
		return wxString();

	return trimmed;
}
