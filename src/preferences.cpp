// Copyright (c) 2010, Amar Takhar <verm@aegisub.org>
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

/// @file preferences.cpp
/// @brief Preferences dialogue
/// @ingroup configuration_ui

#include "preferences.h"

#include "ass_style_storage.h"
#include "audio_provider_factory.h"
#include "audio_renderer_waveform.h"
#include "command/command.h"
#include "compat.h"
#include "colour_button.h"
#include "help_button.h"
#include "hotkey_data_view_model.h"
#include "include/aegisub/audio_player.h"
#include "include/aegisub/hotkey.h"
#include "include/aegisub/subtitles_provider.h"
#include "libaegisub/cajun/writer.h"
#include "libaegisub/fs.h"
#include "libaegisub/io.h"
#include "libaegisub/json.h"
#include "libresrc/libresrc.h"
#include "options.h"
#include "preferences_base.h"
#include "theme_preset.h"
#include "video_provider_manager.h"

#ifdef WITH_PORTAUDIO
#include "audio_player_portaudio.h"
#endif

#ifdef WITH_FFMS2
#include <ffms.h>
#endif

#include <libaegisub/hotkey.h>

#include <unordered_set>

#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/combobox.h>
#include <wx/event.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/filedlg.h>
#include <wx/textdlg.h>
#include <wx/srchctrl.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/treebook.h>
#include <cstring>

namespace {
/// General preferences page
void General(wxTreebook *book, Preferences *parent) {
	auto p = new OptionPage(book, parent, _("General"));

	auto general = p->PageSizer(_("General"));
	p->OptionAdd(general, _("Check for updates on startup"), "App/Auto/Check For Updates");
	p->OptionAdd(general, _("Show main toolbar"), "App/Show Toolbar");
	p->OptionAdd(general, _("Save UI state in subtitles files"), "App/Save UI State");
	p->CellSkip(general);

	p->OptionAdd(general, _("Toolbar Icon Size"), "App/Toolbar Icon Size");
	wxString autoload_modes[] = { _("Never"), _("Always"), _("Ask") };
	wxArrayString autoload_modes_arr(3, autoload_modes);
	p->OptionChoice(general, _("Automatically load linked files"), autoload_modes_arr, "App/Auto/Load Linked Files");
	p->OptionAdd(general, _("Undo Levels"), "Limits/Undo Levels", 2, 10000);

	// Fast actor naming options (moved from Interface)
	auto fast_naming = p->PageSizer(_("Fast naming"));
	{
		p->parent->AddChangeableOption("App/Fast Naming Mode");
		wxArrayString fast_mode_choices;
		fast_mode_choices.Add(_("Off"));
		fast_mode_choices.Add(_("Normal"));
		fast_mode_choices.Add(_("Nanashi"));
		auto combo = new wxComboBox(p, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, fast_mode_choices, wxCB_READONLY | wxCB_DROPDOWN);
		fast_naming->Add(new wxStaticText(p, wxID_ANY, _("Fast naming mode")), 1, wxALIGN_CENTRE_VERTICAL | wxRIGHT, 5);
		fast_naming->Add(combo, wxSizerFlags().Expand());

		wxString current_mode = to_wx(OPT_GET("App/Fast Naming Mode")->GetString());
		current_mode.MakeLower();
		int selection = 0;
		if (current_mode == wxS("normal"))
			selection = 1;
		else if (current_mode == wxS("nanashi"))
			selection = 2;
		combo->SetSelection(selection);

		Preferences *prefs_parent = p->parent;
		combo->Bind(wxEVT_COMBOBOX, [prefs_parent](wxCommandEvent &evt) {
			wxString token = wxS("off");
			switch (evt.GetSelection()) {
			case 1: token = wxS("normal"); break;
			case 2: token = wxS("nanashi"); break;
			default: break;
			}
			auto new_value = std::make_unique<agi::OptionValueString>("App/Fast Naming Mode", from_wx(token));
			prefs_parent->SetOption(std::move(new_value));
		});
	}
	{
		p->parent->AddChangeableOption("App/Fast Naming Playback Mode");
		wxArrayString fast_playback_choices;
		fast_playback_choices.Add(_("Video"));
		fast_playback_choices.Add(_("Audio"));
		fast_playback_choices.Add(_("Off"));
		auto combo = new wxComboBox(p, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, fast_playback_choices, wxCB_READONLY | wxCB_DROPDOWN);
		fast_naming->Add(new wxStaticText(p, wxID_ANY, _("Fast naming auto playback")), 1, wxALIGN_CENTRE_VERTICAL | wxRIGHT, 5);
		fast_naming->Add(combo, wxSizerFlags().Expand());

		wxString playback_mode = to_wx(OPT_GET("App/Fast Naming Playback Mode")->GetString());
		playback_mode.MakeLower();
		int playback_selection = 0;
		if (playback_mode == wxS("audio"))
			playback_selection = 1;
		else if (playback_mode == wxS("off"))
			playback_selection = 2;
		if (playback_selection < 0 || playback_selection >= (int)fast_playback_choices.size())
			playback_selection = 0;
		combo->SetSelection(playback_selection);

		Preferences *prefs_parent = p->parent;
		combo->Bind(wxEVT_COMBOBOX, [prefs_parent](wxCommandEvent &evt) {
			wxString token = wxS("video");
			switch (evt.GetSelection()) {
			case 1: token = wxS("audio"); break;
			case 2: token = wxS("off"); break;
			default: token = wxS("video"); break;
			}
			auto new_value = std::make_unique<agi::OptionValueString>("App/Fast Naming Playback Mode", from_wx(token));
			prefs_parent->SetOption(std::move(new_value));
		});
	}

	auto recent = p->PageSizer(_("Recently Used Lists"));
	p->OptionAdd(recent, _("Files"), "Limits/MRU", 0, 16);
	p->OptionAdd(recent, _("Find/Replace"), "Limits/Find Replace");

	p->SetSizerAndFit(p->sizer);
}

void General_DefaultStyles(wxTreebook *book, Preferences *parent) {
	auto p = new OptionPage(book, parent, _("Default styles"), OptionPage::PAGE_SUB);

	auto staticbox = new wxStaticBoxSizer(wxVERTICAL, p, _("Default style catalogs"));
	p->sizer->Add(staticbox, 0, wxEXPAND, 5);
	p->sizer->AddSpacer(8);

	auto instructions = new wxStaticText(p, wxID_ANY, _("The chosen style catalogs will be loaded when you start a new file or import files in the various formats.\n\nYou can set up style catalogs in the Style Manager."));
	p->sizer->Fit(p);
	instructions->Wrap(400);
	staticbox->Add(instructions, 0, wxALL, 5);
	staticbox->AddSpacer(16);

	auto general = new wxFlexGridSizer(2, 5, 5);
	general->AddGrowableCol(0, 1);
	staticbox->Add(general, 1, wxEXPAND, 5);

	// Build a list of available style catalogs, and wished-available ones
	auto const& avail_catalogs = AssStyleStorage::GetCatalogs();
	std::unordered_set<std::string> catalogs_set(begin(avail_catalogs), end(avail_catalogs));
	// Always include one named "Default" even if it doesn't exist (ensure there is at least one on the list)
	catalogs_set.insert("Default");
	// Include all catalogs named in the existing configuration
	static const char *formats[] = { "ASS", "MicroDVD", "SRT", "TTXT", "TXT" };
	for (auto formatname : formats)
		catalogs_set.insert(OPT_GET("Subtitle Format/" + std::string(formatname) + "/Default Style Catalog")->GetString());
	// Sorted version
	wxArrayString catalogs;
	for (auto const& cn : catalogs_set)
		catalogs.Add(to_wx(cn));
	catalogs.Sort();

	p->OptionChoice(general, _("New files"), catalogs, "Subtitle Format/ASS/Default Style Catalog");
	p->OptionChoice(general, _("MicroDVD import"), catalogs, "Subtitle Format/MicroDVD/Default Style Catalog");
	p->OptionChoice(general, _("SRT import"), catalogs, "Subtitle Format/SRT/Default Style Catalog");
	p->OptionChoice(general, _("TTXT import"), catalogs, "Subtitle Format/TTXT/Default Style Catalog");
	p->OptionChoice(general, _("Plain text import"), catalogs, "Subtitle Format/TXT/Default Style Catalog");

	p->SetSizerAndFit(p->sizer);
}

/// Audio preferences page
void Audio(wxTreebook *book, Preferences *parent) {
	auto p = new OptionPage(book, parent, _("Audio"));

	auto general = p->PageSizer(_("Options"));
	p->OptionAdd(general, _("Default mouse wheel to zoom"), "Audio/Wheel Default to Zoom");
	p->OptionAdd(general, _("Lock scroll on cursor"), "Audio/Lock Scroll on Cursor");
	p->OptionAdd(general, _("Snap markers by default"), "Audio/Snap/Enable");
	p->OptionAdd(general, _("Auto-focus on mouse over"), "Audio/Auto/Focus");
	p->OptionAdd(general, _("Play audio when stepping in video"), "Audio/Plays When Stepping Video");
	p->OptionAdd(general, _("Left-click-drag moves end marker"), "Audio/Drag Timing");
	p->OptionAdd(general, _("Default timing length (ms)"), "Timing/Default Duration", 0, 36000);
	p->OptionAdd(general, _("Default lead-in length (ms)"), "Audio/Lead/IN", 0, 36000);
	p->OptionAdd(general, _("Default lead-out length (ms)"), "Audio/Lead/OUT", 0, 36000);

	p->OptionAdd(general, _("Marker drag-start sensitivity (px)"), "Audio/Start Drag Sensitivity", 1, 15);
	p->OptionAdd(general, _("Line boundary thickness (px)"), "Audio/Line Boundaries Thickness", 1, 5);
	p->OptionAdd(general, _("Maximum snap distance (px)"), "Audio/Snap/Distance", 0, 25);

	const wxString dtl_arr[] = { _("Don't show"), _("Show previous"), _("Show previous and next"), _("Show all") };
	wxArrayString choice_dtl(4, dtl_arr);
	p->OptionChoice(general, _("Show inactive lines"), choice_dtl, "Audio/Inactive Lines Display Mode");
	p->CellSkip(general);
	p->OptionAdd(general, _("Include commented inactive lines"), "Audio/Display/Draw/Inactive Comments");

	auto display = p->PageSizer(_("Display Visual Options"));
	p->OptionAdd(display, _("Keyframes in dialogue mode"), "Audio/Display/Draw/Keyframes in Dialogue Mode");
	p->OptionAdd(display, _("Keyframes in karaoke mode"), "Audio/Display/Draw/Keyframes in Karaoke Mode");
	p->OptionAdd(display, _("Cursor time"), "Audio/Display/Draw/Cursor Time");
	p->OptionAdd(display, _("Video position"), "Audio/Display/Draw/Video Position");
	p->OptionAdd(display, _("Seconds boundaries"), "Audio/Display/Draw/Seconds");
	p->CellSkip(display);
	p->OptionChoice(display, _("Waveform Style"), AudioWaveformRenderer::GetWaveformStyles(), "Audio/Display/Waveform Style");

	auto label = p->PageSizer(_("Audio labels"));
	p->OptionAdd(label, _("Preserve existing timings when cutting/splitting"), "Audio/Karaoke/Preserve Timings on Cut");
	p->OptionFont(label, "Audio/Karaoke/");

	p->SetSizerAndFit(p->sizer);
}

/// Video preferences page
void Video(wxTreebook *book, Preferences *parent) {
	auto p = new OptionPage(book, parent, _("Video"));

	auto general = p->PageSizer(_("Options"));
	p->OptionAdd(general, _("Show keyframes in slider"), "Video/Slider/Show Keyframes");
	p->CellSkip(general);
	p->OptionAdd(general, _("Only show visual tools when mouse is over video"), "Tool/Visual/Autohide");
	p->CellSkip(general);
	p->OptionAdd(general, _("Seek video to line start on selection change"), "Video/Subtitle Sync");
	p->CellSkip(general);
	p->OptionAdd(general, _("Automatically open audio when opening video"), "Video/Open Audio");
	p->OptionAdd(general, _("Default to Video Zoom"), "Video/Default to Video Zoom")
		->SetToolTip("Reverses the behavior of Ctrl while scrolling the video display. If not set, scrolling will default to UI zoom and Ctrl+scrolling will zoom the video. If set, this will be reversed.");
	p->OptionAdd(general, _("Disable zooming with scroll bar"), "Video/Disable Scroll Zoom")
		->SetToolTip("Makes the scroll bar not zoom the video. Useful when using a track pad that often scrolls accidentally.");
	p->OptionAdd(general, _("Reverse zoom direction"), "Video/Reverse Zoom");

	auto relative = p->PageSizer(_("Relative time readouts"));
	p->OptionAdd(relative, _("Disable the popup message for copy/inserting the relative time"), "Video/Disable Click Popup");
	wxArrayString readout_choices;
	readout_choices.Add(_("Copy to clipboard"));
	readout_choices.Add(_("Insert into edit box"));
	readout_choices.Add(_("Copy and insert"));
	readout_choices.Add(_("Nothing happens"));
	p->OptionChoice(relative, _("When you click relative time section"), readout_choices, "Video/Click Time Readout Action");

	auto zoom_section = p->PageSizer(_("Default zoom"));
	const wxString czoom_arr[24] = { "12.5%", "25%", "37.5%", "50%", "62.5%", "75%", "87.5%", "100%", "112.5%", "125%", "137.5%", "150%", "162.5%", "175%", "187.5%", "200%", "212.5%", "225%", "237.5%", "250%", "262.5%", "275%", "287.5%", "300%" };
	wxArrayString choice_zoom(24, czoom_arr);
	p->OptionChoice(zoom_section, _("Default zoom (%)"), choice_zoom, "Video/Default Zoom");
	auto force_default_zoom = p->OptionAdd(zoom_section, _("Force default video zoom"), "Video/Force Default Zoom");
	force_default_zoom->SetToolTip(_("Ignore saved project video zoom and always start using the default video zoom level."));

	p->OptionAdd(general, _("Fast jump step in frames"), "Video/Slider/Fast Jump Step");

	const wxString cscr_arr[3] = { "?video", "?script", "." };
	wxArrayString scr_res(3, cscr_arr);
	p->OptionChoice(general, _("Screenshot save path"), scr_res, "Path/Screenshot");

	auto resolution = p->PageSizer(_("Script Resolution"));
	wxControl *autocb = p->OptionAdd(resolution, _("Use resolution of first video opened"), "Subtitle/Default Resolution/Auto");
	p->CellSkip(resolution);
	p->DisableIfChecked(autocb,
		p->OptionAdd(resolution, _("Default width"), "Subtitle/Default Resolution/Width"));
	p->DisableIfChecked(autocb,
		p->OptionAdd(resolution, _("Default height"), "Subtitle/Default Resolution/Height"));

	const wxString cres_arr[] = {_("Never"), _("Ask"), _("Always set"), _("Always resample")};
	wxArrayString choice_res(4, cres_arr);
	p->OptionChoice(resolution, _("Match video resolution on open"), choice_res, "Video/Script Resolution Mismatch");

	p->SetSizerAndFit(p->sizer);
}

/// Interface preferences page
void Interface(wxTreebook *book, Preferences *parent) {
	auto p = new OptionPage(book, parent, _("Interface"));

	auto edit_box = p->PageSizer(_("Edit Box"));
	p->OptionAdd(edit_box, _("Enable call tips"), "App/Call Tips");
	p->OptionAdd(edit_box, _("Overwrite in time boxes"), "Subtitle/Time Edit/Insert Mode");
	p->OptionAdd(edit_box, _("Shift+Enter adds \\n"), "Subtitle/Edit Box/Soft Line Break");
	p->OptionAdd(edit_box, _("Enable syntax highlighting"), "Subtitle/Highlight/Syntax");
	p->OptionBrowse(edit_box, _("Dictionaries path"), "Path/Dictionary");
	p->OptionFont(edit_box, "Subtitle/Edit Box/");

	auto character_count = p->PageSizer(_("Character Counter"));
	p->OptionAdd(character_count, _("Maximum characters per line"), "Subtitle/Character Limit", 0, 1000);
	p->OptionAdd(character_count, _("Characters Per Second Warning Threshold"), "Subtitle/Character Counter/CPS Warning Threshold", 0, 1000);
	p->OptionAdd(character_count, _("Characters Per Second Error Threshold"), "Subtitle/Character Counter/CPS Error Threshold", 0, 1000);
	p->OptionAdd(character_count, _("Ignore whitespace"), "Subtitle/Character Counter/Ignore Whitespace");
	p->OptionAdd(character_count, _("Ignore punctuation"), "Subtitle/Character Counter/Ignore Punctuation");

	auto grid = p->PageSizer(_("Grid"));
	p->OptionAdd(grid, _("Focus grid on click"), "Subtitle/Grid/Focus Allow");
	p->OptionAdd(grid, _("Highlight visible subtitles"), "Subtitle/Grid/Highlight Subtitles in Frame");
	p->OptionAdd(grid, _("Hide overrides symbol"), "Subtitle/Grid/Hide Overrides Char");
	p->OptionFont(grid, "Subtitle/Grid/");

	auto tl_assistant = p->PageSizer(_("Translation Assistant"));
	p->OptionAdd(tl_assistant, _("Skip over whitespace"), "Tool/Translation Assistant/Skip Whitespace");

	auto visual_tools = p->PageSizer(_("Visual Tools"));
	p->OptionAdd(visual_tools, _("Shape handle size"), "Tool/Visual/Shape Handle Size");

	auto color_picker = p->PageSizer(_("Colour Picker"));
	p->OptionAdd(color_picker, _("Restrict Screen Picker to Window"), "Tool/Colour Picker/Restrict to Window");

#if defined(__WXMSW__) && wxVERSION_NUMBER >= 3300
	auto dark_mode = p->PageSizer(_("Dark Mode"));
	auto dark_mode_ctrl = p->OptionAdd(dark_mode, _("Enable experimental dark mode (restart required)"), "App/Dark Mode");
	if (auto cb = dynamic_cast<wxCheckBox*>(dark_mode_ctrl)) {
		cb->Bind(wxEVT_CHECKBOX, [parent](wxCommandEvent &evt) {
			parent->SetPendingThemePreset(evt.IsChecked() ? "dark_mode_unofficial" : std::string(), true);
			evt.Skip();
		});
	}
#endif

	p->SetSizerAndFit(p->sizer);
}

static std::string SlugifyId(const std::string& input) {
	std::string out;
	out.reserve(input.size());
	bool last_sep = true;
	for (unsigned char c : input) {
		if (std::isalnum(c)) {
			out.push_back(static_cast<char>(std::tolower(c)));
			last_sep = false;
		}
		else {
			if (!last_sep) {
				out.push_back('_');
				last_sep = true;
			}
		}
	}
	while (!out.empty() && out.back() == '_') out.pop_back();
	while (!out.empty() && out.front() == '_') out.erase(out.begin());
	if (out.empty()) out = "theme";
	return out;
}

/// Interface Colours preferences subpage
void Interface_Colours(wxTreebook *book, Preferences *parent) {
	auto p = new OptionPage(book, parent, _("Colors"), OptionPage::PAGE_SCROLL|OptionPage::PAGE_SUB);

	delete p->sizer;
	wxSizer *main_sizer = new wxBoxSizer(wxHORIZONTAL);

	p->sizer = new wxBoxSizer(wxVERTICAL);
	main_sizer->Add(p->sizer, wxEXPAND);

	auto register_opt = [parent](wxControl *ctrl, const char *opt_name, const wxArrayString& choices = wxArrayString()) {
		if (!ctrl) return;
		parent->RegisterColourControl(ctrl, opt_name, OPT_GET(opt_name)->GetType(), choices);
	};

	{
		auto theme_row = new wxFlexGridSizer(2, 5, 5);
		theme_row->AddGrowableCol(1, 1);
		theme_row->Add(new wxStaticText(p, wxID_ANY, _("Theme")), 1, wxALIGN_CENTRE_VERTICAL);
		parent->theme_choice = new wxChoice(p, wxID_ANY);
		theme_row->Add(parent->theme_choice, wxSizerFlags().Expand());
		p->sizer->Add(theme_row, wxSizerFlags().Expand().Border(wxALL & ~wxBOTTOM, 5));

		wxSizer *theme_buttons = new wxBoxSizer(wxHORIZONTAL);
		auto import_btn = new wxButton(p, wxID_ANY, _("Import..."));
		auto export_btn = new wxButton(p, wxID_ANY, _("Export..."));
		theme_buttons->Add(import_btn, wxSizerFlags().Border(wxRIGHT, 5));
		theme_buttons->Add(export_btn, wxSizerFlags());
		p->sizer->Add(theme_buttons, wxSizerFlags().Border(wxLEFT | wxBOTTOM, 5));

		parent->theme_choice->Bind(wxEVT_CHOICE, [parent](wxCommandEvent &evt) {
			size_t idx = static_cast<size_t>(evt.GetSelection());
			if (idx < parent->theme_ids.size())
				parent->SetPendingThemePreset(parent->theme_ids[idx], false);
			evt.Skip();
		});
		import_btn->Bind(wxEVT_BUTTON, &Preferences::OnThemeImport, parent);
		export_btn->Bind(wxEVT_BUTTON, &Preferences::OnThemeExport, parent);

		parent->RefreshThemeList();
	}

	auto audio = p->PageSizer(_("Audio Display"));
	register_opt(p->OptionAdd(audio, _("Play cursor"), "Colour/Audio Display/Play Cursor"), "Colour/Audio Display/Play Cursor");
	register_opt(p->OptionAdd(audio, _("Line boundary start"), "Colour/Audio Display/Line boundary Start"), "Colour/Audio Display/Line boundary Start");
	register_opt(p->OptionAdd(audio, _("Line boundary end"), "Colour/Audio Display/Line boundary End"), "Colour/Audio Display/Line boundary End");
	register_opt(p->OptionAdd(audio, _("Line boundary inactive line"), "Colour/Audio Display/Line Boundary Inactive Line"), "Colour/Audio Display/Line Boundary Inactive Line");
	register_opt(p->OptionAdd(audio, _("Syllable boundaries"), "Colour/Audio Display/Syllable Boundaries"), "Colour/Audio Display/Syllable Boundaries");
	register_opt(p->OptionAdd(audio, _("Seconds boundaries"), "Colour/Audio Display/Seconds Line"), "Colour/Audio Display/Seconds Line");

	auto syntax = p->PageSizer(_("Syntax Highlighting"));
	register_opt(p->OptionAdd(syntax, _("Background"), "Colour/Subtitle/Background"), "Colour/Subtitle/Background");
	register_opt(p->OptionAdd(syntax, _("Normal"), "Colour/Subtitle/Syntax/Normal"), "Colour/Subtitle/Syntax/Normal");
	register_opt(p->OptionAdd(syntax, _("Comments"), "Colour/Subtitle/Syntax/Comment"), "Colour/Subtitle/Syntax/Comment");
	register_opt(p->OptionAdd(syntax, _("Drawing Commands"), "Colour/Subtitle/Syntax/Drawing Command"), "Colour/Subtitle/Syntax/Drawing Command");
	register_opt(p->OptionAdd(syntax, _("Drawing X Coords"), "Colour/Subtitle/Syntax/Drawing X"), "Colour/Subtitle/Syntax/Drawing X");
	register_opt(p->OptionAdd(syntax, _("Drawing Y Coords"), "Colour/Subtitle/Syntax/Drawing Y"), "Colour/Subtitle/Syntax/Drawing Y");
	register_opt(p->OptionAdd(syntax, _("Underline Spline Endpoints"), "Colour/Subtitle/Syntax/Underline/Drawing Endpoint"), "Colour/Subtitle/Syntax/Underline/Drawing Endpoint");
	p->CellSkip(syntax);
	register_opt(p->OptionAdd(syntax, _("Brackets"), "Colour/Subtitle/Syntax/Brackets"), "Colour/Subtitle/Syntax/Brackets");
	register_opt(p->OptionAdd(syntax, _("Slashes and Parentheses"), "Colour/Subtitle/Syntax/Slashes"), "Colour/Subtitle/Syntax/Slashes");
	register_opt(p->OptionAdd(syntax, _("Tags"), "Colour/Subtitle/Syntax/Tags"), "Colour/Subtitle/Syntax/Tags");
	register_opt(p->OptionAdd(syntax, _("Parameters"), "Colour/Subtitle/Syntax/Parameters"), "Colour/Subtitle/Syntax/Parameters");
	register_opt(p->OptionAdd(syntax, _("Error"), "Colour/Subtitle/Syntax/Error"), "Colour/Subtitle/Syntax/Error");
	register_opt(p->OptionAdd(syntax, _("Error Background"), "Colour/Subtitle/Syntax/Background/Error"), "Colour/Subtitle/Syntax/Background/Error");
	register_opt(p->OptionAdd(syntax, _("Line Break"), "Colour/Subtitle/Syntax/Line Break"), "Colour/Subtitle/Syntax/Line Break");
	register_opt(p->OptionAdd(syntax, _("Karaoke templates"), "Colour/Subtitle/Syntax/Karaoke Template"), "Colour/Subtitle/Syntax/Karaoke Template");
	register_opt(p->OptionAdd(syntax, _("Karaoke variables"), "Colour/Subtitle/Syntax/Karaoke Variable"), "Colour/Subtitle/Syntax/Karaoke Variable");

	p->sizer = new wxBoxSizer(wxVERTICAL);
	main_sizer->AddSpacer(5);
	main_sizer->Add(p->sizer, wxEXPAND);

	auto color_schemes = p->PageSizer(_("Audio Color Schemes"));
	wxArrayString schemes = to_wx(OPT_GET("Audio/Colour Schemes")->GetListString());
	register_opt(p->OptionChoice(color_schemes, _("Spectrum"), schemes, "Colour/Audio Display/Spectrum"), "Colour/Audio Display/Spectrum", schemes);
	register_opt(p->OptionChoice(color_schemes, _("Waveform"), schemes, "Colour/Audio Display/Waveform"), "Colour/Audio Display/Waveform", schemes);

	auto grid = p->PageSizer(_("Subtitle Grid"));
	register_opt(p->OptionAdd(grid, _("Standard foreground"), "Colour/Subtitle Grid/Standard"), "Colour/Subtitle Grid/Standard");
	register_opt(p->OptionAdd(grid, _("Standard background"), "Colour/Subtitle Grid/Background/Background"), "Colour/Subtitle Grid/Background/Background");
	register_opt(p->OptionAdd(grid, _("Selection foreground"), "Colour/Subtitle Grid/Selection"), "Colour/Subtitle Grid/Selection");
	register_opt(p->OptionAdd(grid, _("Selection background"), "Colour/Subtitle Grid/Background/Selection"), "Colour/Subtitle Grid/Background/Selection");
	register_opt(p->OptionAdd(grid, _("Collision foreground"), "Colour/Subtitle Grid/Collision"), "Colour/Subtitle Grid/Collision");
	register_opt(p->OptionAdd(grid, _("In frame background"), "Colour/Subtitle Grid/Background/Inframe"), "Colour/Subtitle Grid/Background/Inframe");
	register_opt(p->OptionAdd(grid, _("Comment background"), "Colour/Subtitle Grid/Background/Comment"), "Colour/Subtitle Grid/Background/Comment");
	register_opt(p->OptionAdd(grid, _("Selected comment background"), "Colour/Subtitle Grid/Background/Selected Comment"), "Colour/Subtitle Grid/Background/Selected Comment");
	register_opt(p->OptionAdd(grid, _("Open fold background"), "Colour/Subtitle Grid/Background/Open Fold"), "Colour/Subtitle Grid/Background/Open Fold");
	register_opt(p->OptionAdd(grid, _("Closed fold background"), "Colour/Subtitle Grid/Background/Closed Fold"), "Colour/Subtitle Grid/Background/Closed Fold");
	register_opt(p->OptionAdd(grid, _("Header background"), "Colour/Subtitle Grid/Header"), "Colour/Subtitle Grid/Header");
	register_opt(p->OptionAdd(grid, _("Left Column"), "Colour/Subtitle Grid/Left Column"), "Colour/Subtitle Grid/Left Column");
	register_opt(p->OptionAdd(grid, _("Active Line Border"), "Colour/Subtitle Grid/Active Border"), "Colour/Subtitle Grid/Active Border");
	register_opt(p->OptionAdd(grid, _("Lines"), "Colour/Subtitle Grid/Lines"), "Colour/Subtitle Grid/Lines");
	register_opt(p->OptionAdd(grid, _("CPS Error"), "Colour/Subtitle Grid/CPS Error"), "Colour/Subtitle Grid/CPS Error");

	auto visual_tools = p->PageSizer(_("Visual Typesetting Tools"));
	register_opt(p->OptionAdd(visual_tools, _("Primary Lines"), "Colour/Visual Tools/Lines Primary"), "Colour/Visual Tools/Lines Primary");
	register_opt(p->OptionAdd(visual_tools, _("Secondary Lines"), "Colour/Visual Tools/Lines Secondary"), "Colour/Visual Tools/Lines Secondary");
	register_opt(p->OptionAdd(visual_tools, _("Primary Highlight"), "Colour/Visual Tools/Highlight Primary"), "Colour/Visual Tools/Highlight Primary");
	register_opt(p->OptionAdd(visual_tools, _("Secondary Highlight"), "Colour/Visual Tools/Highlight Secondary"), "Colour/Visual Tools/Highlight Secondary");

	// Separate sizer to prevent the colors in the visual tools section from getting resized
	auto visual_tools_alpha = p->PageSizer(_("Visual Typesetting Tools Alpha"));
	register_opt(p->OptionAdd(visual_tools_alpha, _("Shaded Area"), "Colour/Visual Tools/Shaded Area Alpha", 0, 1, 0.1), "Colour/Visual Tools/Shaded Area Alpha");

	p->sizer = main_sizer;

	p->SetSizerAndFit(p->sizer);
}

/// Backup preferences page
void Backup(wxTreebook *book, Preferences *parent) {
	auto p = new OptionPage(book, parent, _("Backup"));

	auto save = p->PageSizer(_("Automatic Save"));
	wxControl *cb = p->OptionAdd(save, _("Enable"), "App/Auto/Save");
	p->CellSkip(save);
	p->EnableIfChecked(cb,
		p->OptionAdd(save, _("Interval in seconds"), "App/Auto/Save Every Seconds", 1));
	p->OptionBrowse(save, _("Path"), "Path/Auto/Save", cb, true);
	p->OptionAdd(save, _("Autosave after every change"), "App/Auto/Save on Every Change");

	auto backup = p->PageSizer(_("Automatic Backup"));
	cb = p->OptionAdd(backup, _("Enable"), "App/Auto/Backup");
	p->CellSkip(backup);
	p->OptionBrowse(backup, _("Path"), "Path/Auto/Backup", cb, true);

	p->SetSizerAndFit(p->sizer);
}

/// Automation preferences page
void Automation(wxTreebook *book, Preferences *parent) {
	auto p = new OptionPage(book, parent, _("Automation"));

	auto general = p->PageSizer(_("General"));

	p->OptionAdd(general, _("Base path"), "Path/Automation/Base");
	p->OptionAdd(general, _("Include path"), "Path/Automation/Include");
	p->OptionAdd(general, _("Auto-load path"), "Path/Automation/Autoload");

	const wxString tl_arr[6] = { _("0: Fatal"), _("1: Error"), _("2: Warning"), _("3: Hint"), _("4: Debug"), _("5: Trace") };
	wxArrayString tl_choice(6, tl_arr);
	p->OptionChoice(general, _("Trace level"), tl_choice, "Automation/Trace Level");

	const wxString ar_arr[4] = { _("No scripts"), _("Subtitle-local scripts"), _("Global autoload scripts"), _("All scripts") };
	wxArrayString ar_choice(4, ar_arr);
	p->OptionChoice(general, _("Autoreload on Export"), ar_choice, "Automation/Autoreload Mode");

	p->SetSizerAndFit(p->sizer);
}

/// Advanced preferences page
void Advanced(wxTreebook *book, Preferences *parent) {
	auto p = new OptionPage(book, parent, _("Advanced"));

	auto general = p->PageSizer(_("General"));

	auto warning = new wxStaticText(p, wxID_ANY ,_("Changing these settings might result in bugs and/or crashes.  Do not touch these unless you know what you're doing."));
	warning->SetFont(wxFont(12, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
	p->sizer->Fit(p);
	warning->Wrap(400);
	general->Add(warning, 0, wxALL, 5);

	p->SetSizerAndFit(p->sizer);
}

/// Advanced Audio preferences subpage
void Advanced_Audio(wxTreebook *book, Preferences *parent) {
	auto p = new OptionPage(book, parent, _("Audio"), OptionPage::PAGE_SUB);

	auto expert = p->PageSizer(_("Expert"));

	wxArrayString ap_choice = to_wx(GetAudioProviderNames());
	p->OptionChoice(expert, _("Audio provider"), ap_choice, "Audio/Provider");

	wxArrayString apl_choice = to_wx(AudioPlayerFactory::GetClasses());
	p->OptionChoice(expert, _("Audio player"), apl_choice, "Audio/Player");

	auto cache = p->PageSizer(_("Cache"));
	const wxString ct_arr[3] = { _("None (NOT RECOMMENDED)"), _("RAM"), _("Hard Disk") };
	wxArrayString ct_choice(3, ct_arr);
	p->OptionChoice(cache, _("Cache type"), ct_choice, "Audio/Cache/Type");
	p->OptionBrowse(cache, _("Path"), "Audio/Cache/HD/Location");

	auto spectrum = p->PageSizer(_("Spectrum"));

	const wxString sq_arr[4] = { _("Regular quality"), _("Better quality"), _("High quality"), _("Insane quality") };
	wxArrayString sq_choice(4, sq_arr);
	p->OptionChoice(spectrum, _("Quality"), sq_choice, "Audio/Renderer/Spectrum/Quality");

	const wxString sc_arr[5] = { _("Linear"), _("Extended"), _("Medium"), _("Compressed"), _("Logarithmic") };
	wxArrayString sc_choice(5, sc_arr);
	p->OptionChoice(spectrum, _("Frequency mapping"), sc_choice, "Audio/Renderer/Spectrum/FreqCurve");

	p->OptionAdd(spectrum, _("Cache memory max (MB)"), "Audio/Renderer/Spectrum/Memory Max", 2, 1024);

#ifdef WITH_AVISYNTH
	auto avisynth = p->PageSizer("Avisynth");
	const wxString adm_arr[4] = { "None", "ConvertToMono", "GetLeftChannel", "GetRightChannel" };
	wxArrayString adm_choice(4, adm_arr);
	p->OptionChoice(avisynth, _("Avisynth down-mixer"), adm_choice, "Audio/Downmixer");
	p->OptionAdd(avisynth, _("Force sample rate"), "Provider/Audio/AVS/Sample Rate");
#endif

#ifdef WITH_FFMS2
	auto ffms = p->PageSizer("FFmpegSource");

	const wxString error_modes[] = { _("Ignore"), _("Clear"), _("Stop"), _("Abort") };
	wxArrayString error_modes_choice(4, error_modes);
	p->OptionChoice(ffms, _("Audio indexing error handling mode"), error_modes_choice, "Provider/Audio/FFmpegSource/Decode Error Handling");

	p->OptionAdd(ffms, _("Always index all audio tracks"), "Provider/FFmpegSource/Index All Tracks");
	wxControl* stereo = p->OptionAdd(ffms, _("Downmix to stereo"), "Provider/Audio/FFmpegSource/Downmix");
	stereo->SetToolTip("Reduces memory usage on surround audio, but may cause audio tracks to sound blank in specific circumstances. This will not affect audio with two channels or less.");
#endif

#ifdef WITH_BESTSOURCE
	auto bs = p->PageSizer("BestSource");
	p->OptionAdd(bs, _("Max BS cache size (MB)"), "Provider/Audio/BestSource/Max Cache Size");
	p->OptionAdd(bs, _("Use Aegisub's Cache"), "Provider/Audio/BestSource/Aegisub Cache");
#endif


#ifdef WITH_PORTAUDIO
	auto portaudio = p->PageSizer("Portaudio");
	p->OptionChoice(portaudio, _("Portaudio device"), PortAudioPlayer::GetOutputDevices(), "Player/Audio/PortAudio/Device Name");
#endif

#ifdef WITH_OSS
	auto oss = p->PageSizer("OSS");
	p->OptionBrowse(oss, _("OSS Device"), "Player/Audio/OSS/Device");
#endif

#ifdef WITH_DIRECTSOUND
	auto dsound = p->PageSizer("DirectSound");
	p->OptionAdd(dsound, _("Buffer latency"), "Player/Audio/DirectSound/Buffer Latency", 1, 1000);
	p->OptionAdd(dsound, _("Buffer length"), "Player/Audio/DirectSound/Buffer Length", 1, 100);
#endif

	p->SetSizerAndFit(p->sizer);
}

/// Advanced Video preferences subpage
void Advanced_Video(wxTreebook *book, Preferences *parent) {
	auto p = new OptionPage(book, parent, _("Video"), OptionPage::PAGE_SUB);

	auto expert = p->PageSizer(_("Expert"));

	wxArrayString vp_choice = to_wx(VideoProviderFactory::GetClasses());
	p->OptionChoice(expert, _("Video provider"), vp_choice, "Video/Provider");

	wxArrayString sp_choice = to_wx(SubtitlesProviderFactory::GetClasses());
	p->OptionChoice(expert, _("Subtitles provider"), sp_choice, "Subtitle/Provider");


#ifdef WITH_AVISYNTH
	auto avisynth = p->PageSizer("Avisynth");
	p->OptionAdd(avisynth, _("Avisynth memory limit"), "Provider/Avisynth/Memory Max");
#endif

#ifdef WITH_FFMS2
	auto ffms = p->PageSizer("FFmpegSource");

	const wxString log_levels[] = { "Quiet", "Panic", "Fatal", "Error", "Warning", "Info", "Verbose", "Debug" };
	wxArrayString log_levels_choice(8, log_levels);
	p->OptionChoice(ffms, _("Debug log verbosity"), log_levels_choice, "Provider/FFmpegSource/Log Level");

	p->OptionAdd(ffms, _("Decoding threads"), "Provider/Video/FFmpegSource/Decoding Threads", -1);
	p->OptionAdd(ffms, _("Enable unsafe seeking"), "Provider/Video/FFmpegSource/Unsafe Seeking");
#endif

#ifdef WITH_BESTSOURCE
	auto bs = p->PageSizer("BestSource");
	p->OptionAdd(bs, _("Max cache size (MB)"), "Provider/Video/BestSource/Max Cache Size");
	p->OptionAdd(bs, _("Decoder Threads (0 to autodetect)"), "Provider/Video/BestSource/Threads");
	p->OptionAdd(bs, _("Seek preroll (Frames)"), "Provider/Video/BestSource/Seek Preroll");
	p->OptionAdd(bs, _("Apply RFF"), "Provider/Video/BestSource/Apply RFF");
#endif

	p->SetSizerAndFit(p->sizer);
}

void VapourSynth(wxTreebook *book, Preferences *parent) {
#ifdef WITH_VAPOURSYNTH
	auto p = new OptionPage(book, parent, _("VapourSynth"), OptionPage::PAGE_SUB);
	auto general = p->PageSizer(_("General"));

	const wxString log_levels[] = { "Quiet", "Fatal", "Critical", "Warning", "Information", "Debug" };
	wxArrayString log_levels_choice(6, log_levels);
	p->OptionChoice(general, _("Log level"), log_levels_choice, "Provider/Video/VapourSynth/Log Level");
	p->CellSkip(general);
	p->OptionAdd(general, _("Load user plugins"), "Provider/VapourSynth/Autoload User Plugins");

	auto video = p->PageSizer(_("Default Video Script"));

	auto make_default_button = [=](std::string optname, wxTextCtrl *ctrl) {
		auto showdefault = new wxButton(p, -1, _("Set to Default"));
		showdefault->Bind(wxEVT_BUTTON, [=](auto e) {
			ctrl->SetValue(OPT_GET(optname)->GetDefaultString());
		});
		return showdefault;
	};

	auto vhint = new wxStaticText(p, wxID_ANY, _("This script will be executed to load video files that aren't\nVapourSynth scripts (i.e. end in .py or .vpy).\nThe filename variable stores the path to the file."));
	p->sizer->Fit(p);
	vhint->Wrap(400);
	video->Add(vhint, 0, wxALL, 5);
	p->CellSkip(video);

	auto vdef = p->OptionAddMultiline(video, "Provider/Video/VapourSynth/Default Script");
	p->CellSkip(video);

	video->Add(make_default_button("Provider/Video/VapourSynth/Default Script", vdef), wxSizerFlags().Right());

	auto audio = p->PageSizer(_("Default Audio Script"));
	auto ahint = new wxStaticText(p, wxID_ANY, _("This script will be executed to load audio files that aren't\nVapourSynth scripts (i.e. end in .py or .vpy).\nThe filename variable stores the path to the file."));
	p->sizer->Fit(p);
	ahint->Wrap(400);
	audio->Add(ahint, 0, wxALL, 5);
	p->CellSkip(audio);

	auto adef = p->OptionAddMultiline(audio, "Provider/Audio/VapourSynth/Default Script");
	p->CellSkip(audio);

	audio->Add(make_default_button("Provider/Audio/VapourSynth/Default Script", adef), wxSizerFlags().Right());

	p->SetSizerAndFit(p->sizer);
#endif
}

/// wxDataViewIconTextRenderer with command name autocompletion
class CommandRenderer final : public wxDataViewCustomRenderer {
	wxArrayString autocomplete;
	wxDataViewIconText value;
	static const int icon_width = 20;

public:
	CommandRenderer()
	: wxDataViewCustomRenderer("wxDataViewIconText", wxDATAVIEW_CELL_EDITABLE)
	, autocomplete(to_wx(cmd::get_registered_commands()))
	{
	}

	wxWindow *CreateEditorCtrl(wxWindow *parent, wxRect label_rect, wxVariant const& value) override {
		wxDataViewIconText iconText;
		iconText << value;

		wxString text = iconText.GetText();

		// adjust the label rect to take the width of the icon into account
		label_rect.x += icon_width;
		label_rect.width -= icon_width;

		wxTextCtrl* ctrl = new wxTextCtrl(parent, -1, text, label_rect.GetPosition(), label_rect.GetSize(), wxTE_PROCESS_ENTER);
		ctrl->SetInsertionPointEnd();
		ctrl->SelectAll();
		ctrl->AutoComplete(autocomplete);
		return ctrl;
	}

	bool SetValue(wxVariant const& var) override {
		if (var.GetType() == "wxDataViewIconText") {
			value << var;
			return true;
		}
		return false;
	}

	bool Render(wxRect rect, wxDC *dc, int state) override {
		wxIcon const& icon = value.GetIcon();
		if (icon.IsOk())
			dc->DrawIcon(icon, rect.x, rect.y + (rect.height - icon.GetHeight()) / 2);

		RenderText(value.GetText(), icon_width, rect, dc, state);

		return true;
	}

	wxSize GetSize() const override {
		if (!value.GetText().empty()) {
			wxSize size = GetTextExtent(value.GetText());
			size.x += icon_width;
			return size;
		}
		return wxSize(80,20);
	}

	bool GetValueFromEditorCtrl(wxWindow* editor, wxVariant &var) override {
		wxTextCtrl *text = static_cast<wxTextCtrl*>(editor);
		wxDataViewIconText iconText(text->GetValue(), value.GetIcon());
		var << iconText;
		return true;
	}

	bool GetValue(wxVariant &) const override { return false; }
	bool HasEditorCtrl() const override { return true; }
};

class HotkeyRenderer final : public wxDataViewCustomRenderer {
	wxString value;
	wxTextCtrl *ctrl = nullptr;

public:
	HotkeyRenderer()
	: wxDataViewCustomRenderer("string", wxDATAVIEW_CELL_EDITABLE)
	{ }

	wxWindow *CreateEditorCtrl(wxWindow *parent, wxRect label_rect, wxVariant const& var) override {
		ctrl = new wxTextCtrl(parent, -1, var.GetString(), label_rect.GetPosition(), label_rect.GetSize(), wxTE_PROCESS_ENTER);
		ctrl->SetInsertionPointEnd();
		ctrl->SelectAll();
		ctrl->Bind(wxEVT_CHAR_HOOK, &HotkeyRenderer::OnKeyDown, this);
		return ctrl;
	}

	void OnKeyDown(wxKeyEvent &evt) {
		ctrl->ChangeValue(to_wx(hotkey::keypress_to_str(evt.GetKeyCode(), evt.GetModifiers())));
	}

	bool SetValue(wxVariant const& var) override {
		value = var.GetString();
		return true;
	}

	bool Render(wxRect rect, wxDC *dc, int state) override {
		RenderText(value, 0, rect, dc, state);
		return true;
	}

	bool GetValueFromEditorCtrl(wxWindow*, wxVariant &var) override {
		var = ctrl->GetValue();
		return true;
	}

	bool GetValue(wxVariant &) const override { return false; }
	wxSize GetSize() const override { return !value ? wxSize(80, 20) : GetTextExtent(value); }
	bool HasEditorCtrl() const override { return true; }
};

static void edit_item(wxDataViewCtrl *dvc, wxDataViewItem item) {
	dvc->EditItem(item, dvc->GetColumn(0));
}

class Interface_Hotkeys final : public OptionPage {
	wxDataViewCtrl *dvc;
	wxObjectDataPtr<HotkeyDataViewModel> model;
	wxSearchCtrl *quick_search;

	void OnNewButton(wxCommandEvent&);
	void OnUpdateFilter(wxCommandEvent&);
public:
	Interface_Hotkeys(wxTreebook *book, Preferences *parent);
};

/// Interface Hotkeys preferences subpage
Interface_Hotkeys::Interface_Hotkeys(wxTreebook *book, Preferences *parent)
: OptionPage(book, parent, _("Hotkeys"), OptionPage::PAGE_SUB)
, model(new HotkeyDataViewModel(parent))
{
	quick_search = new wxSearchCtrl(this, -1);
	auto new_button = new wxButton(this, -1, _("&New"));
	auto edit_button = new wxButton(this, -1, _("&Edit"));
	auto delete_button = new wxButton(this, -1, _("&Delete"));

	new_button->Bind(wxEVT_BUTTON, &Interface_Hotkeys::OnNewButton, this);
	edit_button->Bind(wxEVT_BUTTON, [=](wxCommandEvent&) { edit_item(dvc, dvc->GetSelection()); });
	delete_button->Bind(wxEVT_BUTTON, [=](wxCommandEvent&) { model->Delete(dvc->GetSelection()); });

	quick_search->Bind(wxEVT_TEXT, &Interface_Hotkeys::OnUpdateFilter, this);
	quick_search->Bind(wxEVT_SEARCHCTRL_CANCEL_BTN, [=](wxCommandEvent&) { quick_search->SetValue(""); });

	dvc = new wxDataViewCtrl(this, -1);
	dvc->AssociateModel(model.get());
#ifndef __APPLE__
	dvc->AppendColumn(new wxDataViewColumn("Hotkey", new HotkeyRenderer, 0, 125, wxALIGN_LEFT, wxCOL_SORTABLE | wxCOL_RESIZABLE));
	dvc->AppendColumn(new wxDataViewColumn("Command", new CommandRenderer, 1, 250, wxALIGN_LEFT, wxCOL_SORTABLE | wxCOL_RESIZABLE));
#else
	auto col = new wxDataViewColumn("Hotkey", new wxDataViewTextRenderer("string", wxDATAVIEW_CELL_EDITABLE), 0, 150, wxALIGN_LEFT, wxCOL_SORTABLE | wxCOL_RESIZABLE);
	col->SetMinWidth(150);
	dvc->AppendColumn(col);
	dvc->AppendColumn(new wxDataViewColumn("Command", new wxDataViewIconTextRenderer("wxDataViewIconText", wxDATAVIEW_CELL_EDITABLE), 1, 250, wxALIGN_LEFT, wxCOL_SORTABLE | wxCOL_RESIZABLE));
#endif
	dvc->AppendTextColumn("Description", 2, wxDATAVIEW_CELL_INERT, 300, wxALIGN_LEFT, wxCOL_SORTABLE | wxCOL_RESIZABLE);

	wxSizer *buttons = new wxBoxSizer(wxHORIZONTAL);
	buttons->Add(quick_search, wxSizerFlags(1).Expand().Border());
	buttons->Add(new_button, wxSizerFlags().Border());
	buttons->Add(edit_button, wxSizerFlags().Border());
	buttons->Add(delete_button, wxSizerFlags().Border());

	sizer->Add(buttons, wxSizerFlags().Expand());
	sizer->Add(dvc, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT));

	SetSizerAndFit(sizer);
}

void Interface_Hotkeys::OnNewButton(wxCommandEvent&) {
	wxDataViewItem sel = dvc->GetSelection();
	dvc->ExpandAncestors(sel);
	dvc->Expand(sel);

	wxDataViewItem new_item = model->New(sel);
	if (new_item.IsOk()) {
		dvc->Select(new_item);
		dvc->EnsureVisible(new_item);
		edit_item(dvc, new_item);
	}
}

void Interface_Hotkeys::OnUpdateFilter(wxCommandEvent&) {
	model->SetFilter(quick_search->GetValue());

	if (!quick_search->GetValue().empty()) {
		wxDataViewItemArray contexts;
		model->GetChildren(wxDataViewItem(nullptr), contexts);
		for (auto const& context : contexts)
			dvc->Expand(context);
	}
}
}

void Preferences::SetOption(std::unique_ptr<agi::OptionValue> new_value) {
	pending_changes[new_value->GetName()] = std::move(new_value);
	applyButton->Enable(true);
}

void Preferences::AddPendingChange(Thunk const& callback) {
	pending_callbacks.push_back(callback);
	applyButton->Enable(true);
}

void Preferences::AddChangeableOption(std::string const& name) {
	option_names.push_back(name);
}

void Preferences::RegisterColourControl(wxControl *ctrl, const std::string& opt_name, agi::OptionType type, const wxArrayString& choices) {
	if (!ctrl) return;
	colour_controls.push_back({ctrl, opt_name, type, choices});
}

void Preferences::SetPendingThemePreset(std::string id, bool only_if_default) {
	if (id.empty()) {
		theme_preset_pending_id.clear();
		theme_preset_only_if_default = false;
		return;
	}

	theme_preset_pending_id = std::move(id);
	theme_preset_only_if_default = only_if_default;
	if (!theme_preset_callback_added) {
		AddPendingChange([this]() { ApplyPendingThemePreset(); });
		theme_preset_callback_added = true;
	}
	if (applyButton)
		applyButton->Enable(true);
}

bool Preferences::AreColourOptionsDefault() const {
	for (auto const& opt_name : option_names) {
		if (opt_name.rfind("Colour/", 0) == 0) {
			auto opt = OPT_GET(opt_name.c_str());
			if (!opt->IsDefault())
				return false;
		}
	}
	return true;
}

void Preferences::RefreshColourControls() {
	for (auto const& binding : colour_controls) {
		auto opt = OPT_GET(binding.option_name.c_str());
		switch (binding.type) {
		case agi::OptionType::Color:
			if (auto cb = dynamic_cast<ColourButton*>(binding.control))
				cb->SetColor(opt->GetColor());
			break;
		case agi::OptionType::Double:
			if (auto scd = dynamic_cast<wxSpinCtrlDouble*>(binding.control))
				scd->SetValue(opt->GetDouble());
			break;
		case agi::OptionType::Int:
			if (auto sc = dynamic_cast<wxSpinCtrl*>(binding.control)) {
				sc->SetValue(opt->GetInt());
			}
			else if (auto combo = dynamic_cast<wxComboBox*>(binding.control)) {
				int val = opt->GetInt();
				if (!binding.choices.empty())
					combo->SetSelection(val < (int)binding.choices.size() ? val : 0);
			}
			break;
		case agi::OptionType::String:
			if (auto combo = dynamic_cast<wxComboBox*>(binding.control)) {
				wxString val(to_wx(opt->GetString()));
				int idx = binding.choices.Index(val, false);
				combo->SetSelection(idx == wxNOT_FOUND ? 0 : idx);
			}
			else if (auto text = dynamic_cast<wxTextCtrl*>(binding.control)) {
				text->SetValue(to_wx(opt->GetString()));
			}
			break;
		default:
			break;
		}
	}
}

void Preferences::RefreshThemeList(const std::string& select_id) {
	if (!theme_choice)
		return;

	auto themes = theme_preset::ListAvailableThemes();
	theme_ids.clear();
	wxArrayString choices;

	choices.Add(_("-- No preset (keep current) --"));
	theme_ids.emplace_back("");

	for (auto const& t : themes) {
		choices.Add(to_wx(t.name));
		theme_ids.push_back(t.id);
	}

	if (themes.empty()) {
		choices.Add(_("No themes found (install/portable data missing)"));
		theme_ids.emplace_back("");
		theme_choice->Enable(false);
	}
	else {
		theme_choice->Enable(true);
	}

	theme_choice->Clear();
	theme_choice->Append(choices);

	size_t sel = 0;
	if (!select_id.empty()) {
		for (size_t i = 0; i < theme_ids.size(); ++i) {
			if (theme_ids[i] == select_id) {
				sel = i;
				break;
			}
		}
	}
	theme_choice->SetSelection(sel);
	if (sel < theme_ids.size())
		SetPendingThemePreset(theme_ids[sel], false);
}

static void InsertJsonValue(json::Object &root, const std::string& path, json::UnknownElement value) {
	auto pos = path.find('/');
	if (pos == std::string::npos) {
		root[path] = std::move(value);
		return;
	}
	auto head = path.substr(0, pos);
	auto tail = path.substr(pos + 1);
	json::Object *child = nullptr;
	try {
		child = &static_cast<json::Object&>(root[head]);
	}
	catch (json::Exception const&) {
		root[head] = json::Object();
		child = &static_cast<json::Object&>(root[head]);
	}
	InsertJsonValue(*child, tail, std::move(value));
}

void Preferences::OnThemeExport(wxCommandEvent &) {
	wxString name = wxGetTextFromUser(_("Enter a name for this theme:"), _("Export Theme"));
	if (name.empty())
		return;

	std::string name_utf8 = from_wx(name);
	std::string id = SlugifyId(name_utf8);
	std::string default_filename = id + ".json";

	wxFileDialog save(this, _("Export Theme"), to_wx(theme_preset::GetThemeDir()), to_wx(default_filename),
		_("JSON files (*.json)|*.json|All files (*.*)|*.*"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
	if (save.ShowModal() != wxID_OK)
		return;

	json::Object colour_obj;
	for (auto const& opt_name : option_names) {
		if (opt_name.rfind("Colour/", 0) != 0)
			continue;
		auto opt = OPT_GET(opt_name.c_str());
		switch (opt->GetType()) {
		case agi::OptionType::String:
			InsertJsonValue(colour_obj, opt_name.substr(strlen("Colour/")), json::UnknownElement(opt->GetString()));
			break;
		case agi::OptionType::Int:
			InsertJsonValue(colour_obj, opt_name.substr(strlen("Colour/")), json::UnknownElement((int64_t)opt->GetInt()));
			break;
		case agi::OptionType::Double:
			InsertJsonValue(colour_obj, opt_name.substr(strlen("Colour/")), json::UnknownElement(opt->GetDouble()));
			break;
		case agi::OptionType::Bool:
			InsertJsonValue(colour_obj, opt_name.substr(strlen("Colour/")), json::UnknownElement(opt->GetBool()));
			break;
		case agi::OptionType::Color:
			InsertJsonValue(colour_obj, opt_name.substr(strlen("Colour/")), json::UnknownElement(opt->GetColor().GetRgbFormatted()));
			break;
		case agi::OptionType::ListString: {
			json::Array arr;
			for (auto const& v : opt->GetListString()) {
				json::Object obj; obj["string"] = v; arr.emplace_back(std::move(obj));
			}
			InsertJsonValue(colour_obj, opt_name.substr(strlen("Colour/")), json::UnknownElement(std::move(arr)));
			break;
		}
		case agi::OptionType::ListInt: {
			json::Array arr;
			for (auto const& v : opt->GetListInt()) {
				json::Object obj; obj["int"] = (int64_t)v; arr.emplace_back(std::move(obj));
			}
			InsertJsonValue(colour_obj, opt_name.substr(strlen("Colour/")), json::UnknownElement(std::move(arr)));
			break;
		}
		case agi::OptionType::ListDouble: {
			json::Array arr;
			for (auto const& v : opt->GetListDouble()) {
				json::Object obj; obj["double"] = v; arr.emplace_back(std::move(obj));
			}
			InsertJsonValue(colour_obj, opt_name.substr(strlen("Colour/")), json::UnknownElement(std::move(arr)));
			break;
		}
		case agi::OptionType::ListColor: {
			json::Array arr;
			for (auto const& v : opt->GetListColor()) {
				json::Object obj; obj["color"] = v.GetRgbFormatted(); arr.emplace_back(std::move(obj));
			}
			InsertJsonValue(colour_obj, opt_name.substr(strlen("Colour/")), json::UnknownElement(std::move(arr)));
			break;
		}
		case agi::OptionType::ListBool: {
			json::Array arr;
			for (auto const& v : opt->GetListBool()) {
				json::Object obj; obj["bool"] = v; arr.emplace_back(std::move(obj));
			}
			InsertJsonValue(colour_obj, opt_name.substr(strlen("Colour/")), json::UnknownElement(std::move(arr)));
			break;
		}
		}
	}

	json::Object root_obj;
	root_obj["Name"] = name_utf8;
	root_obj["Id"] = id;
	// Move to avoid copying a map of move-only json::UnknownElement values
	root_obj["Colour"] = std::move(colour_obj);

	try {
		agi::JsonWriter::Write(root_obj, agi::io::Save(from_wx(save.GetPath())).Get());
		std::string user_dest = theme_preset::GetThemeDir() + "/" + id + ".json";
		agi::JsonWriter::Write(root_obj, agi::io::Save(user_dest).Get());
		RefreshThemeList(id);
		ApplyPendingThemePreset();
	}
	catch (agi::Exception const& e) {
		wxMessageBox(_("Failed to export theme:\n") + to_wx(e.GetMessage()), _("Export Theme"), wxOK | wxICON_ERROR);
	}
	catch (...) {
		wxMessageBox(_("Failed to export theme."), _("Export Theme"), wxOK | wxICON_ERROR);
	}
}

void Preferences::OnThemeImport(wxCommandEvent &) {
	wxFileDialog open(this, _("Import Theme"), wxEmptyString, wxEmptyString,
		_("JSON files (*.json)|*.json|All files (*.*)|*.*"), wxFD_OPEN | wxFD_FILE_MUST_EXIST);
	if (open.ShowModal() != wxID_OK)
		return;

	std::string path = from_wx(open.GetPath());

	try {
		auto stream = agi::io::Open(path);
		json::UnknownElement root = agi::json_util::parse(*stream);
		auto &obj = static_cast<json::Object&>(root);

		auto it_name = obj.find("Name");
		if (it_name == obj.end())
			throw agi::InternalError("Missing Name");
		std::string name = static_cast<json::String const&>(it_name->second);

		std::string id;
		auto it_id = obj.find("Id");
		if (it_id != obj.end())
			id = static_cast<json::String const&>(it_id->second);
		id = SlugifyId(id.empty() ? name : id);
		obj["Id"] = id;

		auto it_col = obj.find("Colour");
		if (it_col == obj.end())
			throw agi::InternalError("Missing Colour section");
		(void)static_cast<json::Object const&>(it_col->second); // validate object

		std::string user_dir = theme_preset::GetThemeDir();
		agi::fs::path dst = agi::fs::path(user_dir) / (id + ".json");
		if (agi::fs::FileExists(dst)) {
			auto res = wxMessageBox(wxString::Format(_("A theme with Id '%s' already exists. Overwrite?"), to_wx(id)), _("Import Theme"), wxYES_NO | wxICON_QUESTION);
			if (res != wxYES)
				return;
		}

		agi::JsonWriter::Write(obj, agi::io::Save(dst).Get());
		RefreshThemeList(id);
		ApplyPendingThemePreset();
	}
	catch (agi::Exception const& e) {
		wxMessageBox(_("The selected file is not a valid Aegisub colour theme.\n\nDetails: ") + to_wx(e.GetMessage()),
			_("Import Theme"), wxOK | wxICON_ERROR);
	}
	catch (std::exception const& e) {
		wxMessageBox(_("The selected file is not a valid Aegisub colour theme.\n\nDetails: ") + wxString::FromUTF8(e.what()),
			_("Import Theme"), wxOK | wxICON_ERROR);
	}
	catch (...) {
		wxMessageBox(_("The selected file is not a valid Aegisub colour theme."), _("Import Theme"), wxOK | wxICON_ERROR);
	}
}

void Preferences::ApplyPendingThemePreset() {
	if (theme_preset_pending_id.empty()) {
		theme_preset_callback_added = false;
		return;
	}

	std::string id = theme_preset_pending_id;
	bool require_default = theme_preset_only_if_default;

	theme_preset_pending_id.clear();
	theme_preset_only_if_default = false;
	theme_preset_callback_added = false;

	if (require_default && !AreColourOptionsDefault())
		return;

	std::string error_msg;
	if (theme_preset::ApplyTheme(id, &error_msg)) {
		RefreshColourControls();
	}
	else {
		wxString friendly = _("Failed to apply theme preset.");
		if (!error_msg.empty())
			friendly += "\n\n" + to_wx(error_msg);
		wxMessageBox(friendly, _("Theme preset"), wxOK | wxICON_WARNING);
	}
}

void Preferences::OnOK(wxCommandEvent &event) {
	OnApply(event);
	EndModal(0);
}

void Preferences::OnApply(wxCommandEvent &) {
	for (auto const& change : pending_changes)
		OPT_SET(change.first)->Set(change.second.get());
	pending_changes.clear();

	for (auto const& thunk : pending_callbacks)
		thunk();
	pending_callbacks.clear();

	applyButton->Enable(false);
	config::opt->Flush();
}

void Preferences::OnResetDefault(wxCommandEvent&) {
	if (wxYES != wxMessageBox(_("Are you sure that you want to restore the defaults? All your settings will be overridden."), _("Restore defaults?"), wxYES_NO))
		return;

	for (auto const& opt_name : option_names) {
		agi::OptionValue *opt = OPT_SET(opt_name);
		if (!opt->IsDefault())
			opt->Reset();
	}
	config::opt->Flush();

	agi::hotkey::Hotkey def_hotkeys("", GET_DEFAULT_CONFIG(default_hotkey));
	hotkey::inst->SetHotkeyMap(def_hotkeys.GetHotkeyMap());

	// Close and reopen the dialog to update all the controls with the new values
	OPT_SET("Tool/Preferences/Page")->SetInt(book->GetSelection());
	EndModal(-1);
}

Preferences::Preferences(wxWindow *parent): wxDialog(parent, -1, _("Preferences"), wxDefaultPosition, wxSize(-1, -1), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
	SetIcon(GETICON(options_button_16));

	book = new wxTreebook(this, -1, wxDefaultPosition, wxDefaultSize);
	General(book, this);
	General_DefaultStyles(book, this);
	Audio(book, this);
	Video(book, this);
	Interface(book, this);
	Interface_Colours(book, this);
	new Interface_Hotkeys(book, this);
	Backup(book, this);
	Automation(book, this);
	Advanced(book, this);
	Advanced_Audio(book, this);
	Advanced_Video(book, this);
	VapourSynth(book, this);

	book->Fit();

	book->ChangeSelection(OPT_GET("Tool/Preferences/Page")->GetInt());
	book->Bind(wxEVT_TREEBOOK_PAGE_CHANGED, [](wxBookCtrlEvent &evt) {
		OPT_SET("Tool/Preferences/Page")->SetInt(evt.GetSelection());
	});

	// Bottom Buttons
	auto stdButtonSizer = CreateStdDialogButtonSizer(wxOK | wxCANCEL | wxAPPLY | wxHELP);
	applyButton = stdButtonSizer->GetApplyButton();
	wxSizer *buttonSizer = new wxBoxSizer(wxHORIZONTAL);
	auto defaultButton = new wxButton(this, -1, _("&Restore Defaults"));
	buttonSizer->Add(defaultButton, wxSizerFlags(0).Expand());
	buttonSizer->AddStretchSpacer(1);
	buttonSizer->Add(stdButtonSizer, wxSizerFlags(0).Expand());

	// Main Sizer
	wxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);
	mainSizer->Add(book, wxSizerFlags(1).Expand().Border());
	mainSizer->Add(buttonSizer, wxSizerFlags(0).Expand().Border(wxALL & ~wxTOP));

	SetSizerAndFit(mainSizer);
	CenterOnParent();

	applyButton->Enable(false);

	Bind(wxEVT_BUTTON, &Preferences::OnOK, this, wxID_OK);
	Bind(wxEVT_BUTTON, &Preferences::OnApply, this, wxID_APPLY);
	Bind(wxEVT_BUTTON, std::bind(&HelpButton::OpenPage, "Options"), wxID_HELP);
	defaultButton->Bind(wxEVT_BUTTON, &Preferences::OnResetDefault, this);
}

void ShowPreferences(wxWindow *parent) {
	while (Preferences(parent).ShowModal() < 0);
}
