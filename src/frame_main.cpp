// Copyright (c) 2005, Rodrigo Braz Monteiro, Niels Martin Hansen
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

/// @file frame_main.cpp
/// @brief Main window creation and control management
/// @ingroup main_ui

#include "frame_main.h"

#include "include/aegisub/context.h"
#include "include/aegisub/menu.h"
#include "include/aegisub/toolbar.h"
#include "include/aegisub/hotkey.h"

#include "ass_file.h"
#include "async_video_provider.h"
#include "audio_controller.h"
#include "audio_box.h"
#include "base_grid.h"
#include "compat.h"
#include "command/command.h"
#include "dialog_detached_video.h"
#include "dialog_manager.h"
#include "libresrc/libresrc.h"
#include "main.h"
#include "options.h"
#include "project.h"
#include "subs_controller.h"
#include "subs_edit_box.h"
#include "utils.h"
#include "version.h"
#include "video_box.h"
#include "video_controller.h"
#include "video_display.h"

#include <libaegisub/dispatch.h>
#include <libaegisub/format.h>
#include <libaegisub/log.h>
#include <libaegisub/make_unique.h>

#include <wx/dnd.h>
#include <wx/dcbuffer.h>
#include <wx/msgdlg.h>
#include <wx/popupwin.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/sysopt.h>
#include <wx/settings.h>
#include <wx/utils.h>

enum {
	ID_APP_TIMER_STATUSCLEAR = 12002,
	ID_ALIGNMENT_PICKER_TIMER
};

static const int ALIGNMENT_PICKER_SETTLE_MS = 150;
static const int ALIGNMENT_PICKER_POLL_MS = 25;
enum AlignmentArrowBits {
	ALIGN_ARROW_LEFT = 1,
	ALIGN_ARROW_RIGHT = 2,
	ALIGN_ARROW_UP = 4,
	ALIGN_ARROW_DOWN = 8
};

#ifdef WITH_STARTUPLOG
#define StartupLog(a) MessageBox(0, a, "Aegisub startup log", 0)
#else
#define StartupLog(a) LOG_I("frame_main/init") << a
#endif

/// Handle files drag and dropped onto Aegisub
class AegisubFileDropTarget final : public wxFileDropTarget {
	agi::Context *context;
public:
	AegisubFileDropTarget(agi::Context *context) : context(context) { }
	bool OnDropFiles(wxCoord, wxCoord, wxArrayString const& filenames) override {
		std::vector<agi::fs::path> files;
		for (wxString const& fn : filenames)
			files.push_back(from_wx(fn));
		agi::dispatch::Main().Async([=] { context->project->LoadList(files); });
		return true;
	}
};

class AlignmentPickerPopup final : public wxPopupWindow {
	int selection = 0;

public:
	AlignmentPickerPopup(wxWindow *parent)
	: wxPopupWindow(parent, wxBORDER_SIMPLE)
	{
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		SetClientSize(wxSize(96, 96));
		Bind(wxEVT_PAINT, [=](wxPaintEvent &) {
			wxAutoBufferedPaintDC dc(this);
			dc.SetBackground(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW)));
			dc.Clear();

			static const int values[3][3] = {
				{ 7, 8, 9 },
				{ 4, 5, 6 },
				{ 1, 2, 3 }
			};

			wxSize size = GetClientSize();
			int cell_w = size.GetWidth() / 3;
			int cell_h = size.GetHeight() / 3;

			for (int row = 0; row < 3; ++row) {
				for (int col = 0; col < 3; ++col) {
					int value = values[row][col];
					wxRect rect(col * cell_w, row * cell_h, cell_w, cell_h);
					if (value == selection) {
						dc.SetBrush(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT)));
						dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT));
					}
					else {
						dc.SetBrush(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW)));
						dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
					}
					dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT)));
					dc.DrawRectangle(rect);
					dc.DrawLabel(wxString::Format("%d", value), rect, wxALIGN_CENTER);
				}
			}
		});
	}

	void SetSelection(int value) {
		if (selection == value) return;
		selection = value;
		Refresh();
	}
};

static bool ctrl_alt_down() {
	bool ctrl = wxGetKeyState(WXK_CONTROL);
#ifdef WXK_RAW_CONTROL
	ctrl = ctrl || wxGetKeyState(WXK_RAW_CONTROL);
#endif
	return ctrl && wxGetKeyState(WXK_ALT);
}

static bool is_arrow_key(int key_code) {
	return key_code == WXK_LEFT || key_code == WXK_RIGHT || key_code == WXK_UP || key_code == WXK_DOWN;
}

static int arrow_bit_from_key(int key_code) {
	if (key_code == WXK_LEFT) return ALIGN_ARROW_LEFT;
	if (key_code == WXK_RIGHT) return ALIGN_ARROW_RIGHT;
	if (key_code == WXK_UP) return ALIGN_ARROW_UP;
	if (key_code == WXK_DOWN) return ALIGN_ARROW_DOWN;
	return 0;
}

static int current_arrow_bits() {
	int arrows = 0;
	if (wxGetKeyState(WXK_LEFT)) arrows |= ALIGN_ARROW_LEFT;
	if (wxGetKeyState(WXK_RIGHT)) arrows |= ALIGN_ARROW_RIGHT;
	if (wxGetKeyState(WXK_UP)) arrows |= ALIGN_ARROW_UP;
	if (wxGetKeyState(WXK_DOWN)) arrows |= ALIGN_ARROW_DOWN;
	return arrows;
}

static int alignment_from_number_key(int key_code) {
	if (key_code >= '1' && key_code <= '9')
		return key_code - '0';
	if (key_code >= WXK_NUMPAD1 && key_code <= WXK_NUMPAD9)
		return key_code - WXK_NUMPAD0;
	return 0;
}

static int alignment_from_arrow_bits(int arrows) {
	bool left = !!(arrows & ALIGN_ARROW_LEFT);
	bool right = !!(arrows & ALIGN_ARROW_RIGHT);
	bool up = !!(arrows & ALIGN_ARROW_UP);
	bool down = !!(arrows & ALIGN_ARROW_DOWN);
	int count = (left ? 1 : 0) + (right ? 1 : 0) + (up ? 1 : 0) + (down ? 1 : 0);

	if (!count) return 0;
	if (count >= 3 || (left && right) || (up && down)) return 5;
	if (up && left) return 7;
	if (up && right) return 9;
	if (down && left) return 1;
	if (down && right) return 3;
	if (up) return 8;
	if (down) return 2;
	if (left) return 4;
	return 6;
}

FrameMain::FrameMain()
: wxFrame(nullptr, -1, "", wxDefaultPosition, wxSize(920,700), wxDEFAULT_FRAME_STYLE | wxCLIP_CHILDREN)
, context(agi::make_unique<agi::Context>())
{
	StartupLog("Entering FrameMain constructor");
	AlignmentPickerTimer.SetOwner(this, ID_ALIGNMENT_PICKER_TIMER);

#ifdef __WXGTK__
	// XXX HACK XXX
	// We need to set LC_ALL to "" here for input methods to work reliably.
	setlocale(LC_ALL, "");

	// However LC_NUMERIC must be "C", otherwise some parsing fails.
	setlocale(LC_NUMERIC, "C");
#endif

	StartupLog("Initializing context controls");
	context->ass->AddCommitListener(&FrameMain::UpdateTitle, this);
	context->subsController->AddFileOpenListener(&FrameMain::OnSubtitlesOpen, this);
	context->subsController->AddFileSaveListener(&FrameMain::UpdateTitle, this);
	context->project->AddAudioProviderListener(&FrameMain::OnAudioOpen, this);
	context->project->AddVideoProviderListener(&FrameMain::OnVideoOpen, this);

	StartupLog("Initializing context frames");
	context->parent = this;
	context->frame = this;

	StartupLog("Apply saved Maximized state");
	if (OPT_GET("App/Maximized")->GetBool()) Maximize(true);

	StartupLog("Initialize toolbar");
	wxSystemOptions::SetOption("msw.remap", 0);
	OPT_SUB("App/Show Toolbar", &FrameMain::EnableToolBar, this);
	EnableToolBar(*OPT_GET("App/Show Toolbar"));

	StartupLog("Initialize menu bar");
	menu::GetMenuBar("main", this, (wxID_HIGHEST + 1) + 10000, context.get());

	StartupLog("Create status bar");
	CreateStatusBar(2);

	StartupLog("Set icon");
#ifdef _WIN32
	SetIcon(wxICON(wxicon));
#else
	wxIcon icon;
	icon.CopyFromBitmap(GETIMAGE(wxicon));
	SetIcon(icon);
#endif

	StartupLog("Create views and inner main window controls");
	InitContents();
	OPT_SUB("Video/Detached/Enabled", &FrameMain::OnVideoDetach, this);

	StartupLog("Set up drag/drop target");
	SetDropTarget(new AegisubFileDropTarget(context.get()));

	StartupLog("Load default file");
	context->project->CloseSubtitles();

	StartupLog("Display main window");
	AddFullScreenButton(this);
	Show();
	SetDisplayMode(1, 1);

	StartupLog("Leaving FrameMain constructor");
}

FrameMain::~FrameMain () {
	HideAlignmentPicker();
	context->project->CloseAudio();
	context->project->CloseVideo();

	DestroyChildren();
}

void FrameMain::EnableToolBar(agi::OptionValue const& opt) {
	if (opt.GetBool()) {
		if (!GetToolBar()) {
			toolbar::AttachToolbar(this, "main", context.get(), "Default");
			GetToolBar()->Realize();
		}
	}
	else if (wxToolBar *old_tb = GetToolBar()) {
		SetToolBar(nullptr);
		delete old_tb;
		Layout();
	}
}

void FrameMain::InitContents() {
	StartupLog("Create background panel");
	auto Panel = new wxPanel(this, -1, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | wxCLIP_CHILDREN);

	StartupLog("Create subtitles grid");
	context->subsGrid = new BaseGrid(Panel, context.get());

	StartupLog("Create video box");
	videoBox = new VideoBox(Panel, false, context.get());

	StartupLog("Create audio box");
	context->audioBox = audioBox = new AudioBox(Panel, context.get());

	StartupLog("Create subtitle editing box");
	auto EditBox = new SubsEditBox(Panel, context.get());

	StartupLog("Arrange main sizers");
	ToolsSizer = new wxBoxSizer(wxVERTICAL);
	ToolsSizer->Add(audioBox, 0, wxEXPAND);
	ToolsSizer->Add(EditBox, 1, wxEXPAND);
	TopSizer = new wxBoxSizer(wxHORIZONTAL);
	TopSizer->Add(videoBox, 0, wxEXPAND, 0);
	TopSizer->Add(ToolsSizer, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);
	MainSizer = new wxBoxSizer(wxVERTICAL);
	MainSizer->Add(new wxStaticLine(Panel),0,wxEXPAND | wxALL,0);
	MainSizer->Add(TopSizer,0,wxEXPAND | wxALL,0);
	MainSizer->Add(context->subsGrid,1,wxEXPAND | wxALL,0);
	Panel->SetSizer(MainSizer);

	StartupLog("Perform layout");
	Layout();
	StartupLog("Leaving InitContents");
}

void FrameMain::SetDisplayMode(int video, int audio) {
	if (!IsShownOnScreen()) return;

	bool sv = false, sa = false;

	if (video == -1) sv = showVideo;
	else if (video)  sv = context->project->VideoProvider() && !context->dialog->Get<DialogDetachedVideo>();

	if (audio == -1) sa = showAudio;
	else if (audio)  sa = !!context->project->AudioProvider();

	// See if anything changed
	if (sv == showVideo && sa == showAudio) return;

	showVideo = sv;
	showAudio = sa;

	bool didFreeze = !IsFrozen();
	if (didFreeze) Freeze();

	context->videoController->Stop();

	TopSizer->Show(videoBox, showVideo, true);
	ToolsSizer->Show(audioBox, showAudio, true);

	MainSizer->Layout();
	Layout();

	if (didFreeze) Thaw();
}

void FrameMain::UpdateTitle() {
	wxString newTitle;
	if (context->subsController->IsModified()) newTitle << "* ";
	newTitle << context->subsController->Filename().filename().wstring();

#ifndef __WXMAC__
	newTitle << " - Aegisub " << GetAegisubLongVersionString();
#endif

#if defined(__WXMAC__)
	// On Mac, set the mark in the close button
	OSXSetModified(context->subsController->IsModified());
#endif

	if (GetTitle() != newTitle) SetTitle(newTitle);
}

void FrameMain::OnVideoOpen(AsyncVideoProvider *provider) {
	if (!provider) {
		SetDisplayMode(0, -1);
		return;
	}

	Freeze();
	int vidx = provider->GetWidth(), vidy = provider->GetHeight();

	// Set zoom level based on video resolution and window size
	double zoom = context->videoDisplay->GetZoom();
	wxSize windowSize = GetSize();
	if (vidx*3*zoom > windowSize.GetX()*4 || vidy*4*zoom > windowSize.GetY()*6)
		context->videoDisplay->SetWindowZoom(zoom * .25);
	else if (vidx*3*zoom > windowSize.GetX()*2 || vidy*4*zoom > windowSize.GetY()*3)
		context->videoDisplay->SetWindowZoom(zoom * .5);

	SetDisplayMode(1,-1);

	if (OPT_GET("Video/Detached/Enabled")->GetBool() && !context->dialog->Get<DialogDetachedVideo>())
		cmd::call("video/detach", context.get());
	Thaw();
}

void FrameMain::OnVideoDetach(agi::OptionValue const& opt) {
	if (opt.GetBool())
		SetDisplayMode(0, -1);
	else if (context->project->VideoProvider())
		SetDisplayMode(1, -1);
}

void FrameMain::StatusTimeout(wxString text,int ms) {
	SetStatusText(text,1);
	StatusClear.SetOwner(this, ID_APP_TIMER_STATUSCLEAR);
	StatusClear.Start(ms,true);
}

BEGIN_EVENT_TABLE(FrameMain, wxFrame)
	EVT_TIMER(ID_APP_TIMER_STATUSCLEAR, FrameMain::OnStatusClear)
	EVT_TIMER(ID_ALIGNMENT_PICKER_TIMER, FrameMain::OnAlignmentPickerTimer)
	EVT_CLOSE(FrameMain::OnCloseWindow)
	EVT_CHAR_HOOK(FrameMain::OnKeyDown)
	EVT_MOUSEWHEEL(FrameMain::OnMouseWheel)
END_EVENT_TABLE()

void FrameMain::OnCloseWindow(wxCloseEvent &event) {
	wxEventBlocker blocker(this, wxEVT_CLOSE_WINDOW);

	context->videoController->Stop();
	context->audioController->Stop();

	// Ask user if he wants to save first
	if (context->subsController->TryToClose(event.CanVeto()) == wxCANCEL) {
		event.Veto();
		return;
	}

	context->dialog.reset();

	// Store maximization state
	OPT_SET("App/Maximized")->SetBool(IsMaximized());

	Destroy();
}

void FrameMain::OnStatusClear(wxTimerEvent &) {
	SetStatusText("",1);
}

void FrameMain::ResetAlignmentPickerState() {
	AlignmentPickerTimer.Stop();
	alignmentPickerPreview = 0;
	alignmentPickerSelected = 0;
	alignmentPickerPendingArrows = 0;
	alignmentPickerSettling = false;
}

void FrameMain::HideAlignmentPicker() {
	ResetAlignmentPickerState();
	if (!alignmentPicker) return;
	alignmentPicker->Destroy();
	alignmentPicker = nullptr;
}

bool FrameMain::HandleAlignmentPickerKeyDown(wxKeyEvent &event) {
	int key_code = event.GetKeyCode();
	if (key_code == WXK_ESCAPE && alignmentPicker) {
		HideAlignmentPicker();
		return true;
	}

	bool ctrl_alt = event.ControlDown() && event.AltDown();
	if (!ctrl_alt)
		return false;

	if (int an = alignment_from_number_key(key_code)) {
		HideAlignmentPicker();
		cmd::call(agi::format("subtitle/set_alignment/an%d", an), context.get());
		return true;
	}

	if (!is_arrow_key(key_code) && key_code != WXK_CONTROL && key_code != WXK_ALT)
		return false;

	if (!alignmentPicker) {
		alignmentPicker = new AlignmentPickerPopup(this);
		wxPoint pos = wxGetMousePosition();
		alignmentPicker->Move(pos + wxPoint(16, 16));
		alignmentPicker->Show();
	}

	if (int arrow = arrow_bit_from_key(key_code)) {
		if (!alignmentPickerSettling)
			alignmentPickerPendingArrows = 0;
		alignmentPickerPendingArrows |= current_arrow_bits() | arrow;
		alignmentPickerPreview = alignment_from_arrow_bits(alignmentPickerPendingArrows);
		static_cast<AlignmentPickerPopup *>(alignmentPicker)->SetSelection(alignmentPickerPreview);
		alignmentPickerSettling = true;
		AlignmentPickerTimer.Start(ALIGNMENT_PICKER_SETTLE_MS, wxTIMER_ONE_SHOT);
	}
	else if (!AlignmentPickerTimer.IsRunning()) {
		AlignmentPickerTimer.Start(ALIGNMENT_PICKER_POLL_MS);
	}
	return true;
}

void FrameMain::OnAlignmentPickerTimer(wxTimerEvent &) {
	if (!alignmentPicker) {
		AlignmentPickerTimer.Stop();
		return;
	}

	if (alignmentPickerSettling) {
		alignmentPickerSelected = alignment_from_arrow_bits(alignmentPickerPendingArrows);
		alignmentPickerSettling = false;
		if (alignmentPickerSelected)
			static_cast<AlignmentPickerPopup *>(alignmentPicker)->SetSelection(alignmentPickerSelected);
	}

	if (!ctrl_alt_down()) {
		int an = alignmentPickerSelected ? alignmentPickerSelected : alignment_from_arrow_bits(alignmentPickerPendingArrows);
		HideAlignmentPicker();
		if (an)
			cmd::call(agi::format("subtitle/set_alignment/an%d", an), context.get());
		return;
	}

	AlignmentPickerTimer.Start(ALIGNMENT_PICKER_POLL_MS);
}

void FrameMain::OnAudioOpen(agi::AudioProvider *provider) {
	if (provider)
		SetDisplayMode(-1, 1);
	else
		SetDisplayMode(-1, 0);
}

void FrameMain::OnSubtitlesOpen() {
	UpdateTitle();
	SetDisplayMode(1, 1);
}

void FrameMain::OnKeyDown(wxKeyEvent &event) {
	if (HandleAlignmentPickerKeyDown(event))
		return;
	hotkey::check("Main Frame", context.get(), event);
}

void FrameMain::OnMouseWheel(wxMouseEvent &evt) {
	ForwardMouseWheelEvent(this, evt);
}

// Satoshi: file in use fun warning system
int FrameMain::IncrementFileLockWarnings() {
	return ++fileLockWarnings;
}

void FrameMain::ResetFileLockWarnings() {
	fileLockWarnings = 0;
}
