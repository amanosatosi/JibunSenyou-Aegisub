// Copyright (c) 2026, Aegisub Project
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

#include "dialog_ocr.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "async_video_provider.h"
#include "compat.h"
#include "format.h"
#include "include/aegisub/context.h"
#include "libresrc/libresrc.h"
#include "options.h"
#include "ocr/ocr_engine.h"
#include "persist_location.h"
#include "project.h"
#include "selection_controller.h"
#include "subs_edit_box.h"
#include "utils.h"
#include "value_event.h"
#include "video_controller.h"
#include "video_frame.h"

#include <libaegisub/dispatch.h>
#include <libaegisub/fs.h>
#include <libaegisub/make_unique.h>

#include <boost/algorithm/string/replace.hpp>

#include <memory>
#include <string>
#include <vector>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/filepicker.h>
#include <wx/filename.h>
#include <wx/gauge.h>
#include <wx/msgdlg.h>
#include <wx/radiobut.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/timer.h>

namespace {

struct OCRThreadResult {
	ocr::OCRResult result;
	agi::fs::path temporary_image;
};

AGI_DEFINE_EVENT(EVT_OCR_COMPLETE, OCRThreadResult)

struct LanguageChoice {
	wxString label;
	std::string key;
};

std::vector<LanguageChoice> const& Languages() {
	static std::vector<LanguageChoice> languages = {
		{_("Japanese"), "japanese"},
		{_("English"), "english"},
		{_("Simplified Chinese"), "chinese_simplified"},
		{_("Traditional Chinese"), "chinese_traditional"},
		{_("Korean"), "korean"}
	};
	return languages;
}

wxString DisplayTextToAss(wxString text) {
	std::string ass = from_wx(text);
	boost::replace_all(ass, "\r\n", "\\N");
	boost::replace_all(ass, "\r", "\\N");
	boost::replace_all(ass, "\n", "\\N");
	return to_wx(ass);
}

wxString InsertionText(agi::Context *c, wxString const& text) {
	if (c->subsEditBox && c->subsEditBox->BetterViewEnabled())
		return text;
	return DisplayTextToAss(text);
}

wxImage GetCurrentFrameImage(agi::Context *c) {
	auto frame = c->videoController->GetFrameN();
	return GetImage(*c->project->VideoProvider()->GetFrame(frame, c->project->Timecodes().TimeAtFrame(frame), true));
}

} // namespace

struct DialogOCR::Impl {
	DialogOCR *dialog;
	agi::Context *c;
	ocr::OCREngine engine;
	std::unique_ptr<PersistLocation> persist;

	wxRadioButton *source_frame = nullptr;
	wxRadioButton *source_file = nullptr;
	wxFilePickerCtrl *file_picker = nullptr;
	wxChoice *language = nullptr;
	wxCheckBox *keep_line_breaks = nullptr;
	wxCheckBox *copy_after = nullptr;
	wxCheckBox *insert_after = nullptr;
	wxTextCtrl *result_text = nullptr;
	wxStaticText *status = nullptr;
	wxGauge *gauge = nullptr;
	wxTimer pulse_timer;
	wxButton *recognize_button = nullptr;
	wxButton *insert_button = nullptr;
	wxButton *replace_button = nullptr;
	wxButton *copy_button = nullptr;
	wxButton *close_button = nullptr;

	bool running = false;

	Impl(DialogOCR *dialog, agi::Context *c);

	void OnRecognize(wxCommandEvent&);
	void OnInsert(wxCommandEvent&);
	void OnReplace(wxCommandEvent&);
	void OnCopy(wxCommandEvent&);
	void OnComplete(ValueEvent<OCRThreadResult>& event);
	void OnClose(wxCloseEvent& event);
	void OnSourceChanged(wxCommandEvent&);

	void SetRunning(bool value);
	void UpdateControls();
	void SaveOptions();
	ocr::OCROptions CurrentOptions() const;
	agi::fs::path PrepareImage();
	void InsertResult();
	void CopyResult();
};

DialogOCR::Impl::Impl(DialogOCR *dialog, agi::Context *c)
: dialog(dialog)
, c(c)
, pulse_timer(dialog)
{
	dialog->SetIcon(GETICON(open_video_menu_16));

	auto main_sizer = new wxBoxSizer(wxVERTICAL);

	auto source_box = new wxStaticBoxSizer(wxVERTICAL, dialog, _("Source"));
	source_frame = new wxRadioButton(dialog, -1, _("Current video frame"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	source_file = new wxRadioButton(dialog, -1, _("Image file"));
	file_picker = new wxFilePickerCtrl(dialog, -1, "", _("Choose image for OCR"), _("Image files (*.png;*.jpg;*.jpeg;*.bmp;*.webp)|*.png;*.jpg;*.jpeg;*.bmp;*.webp|All files (*.*)|*.*"));
	source_box->Add(source_frame, 0, wxALL, 4);
	source_box->Add(source_file, 0, wxLEFT | wxRIGHT | wxBOTTOM, 4);
	source_box->Add(file_picker, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);
	main_sizer->Add(source_box, 0, wxEXPAND | wxALL, 5);

	auto options_box = new wxStaticBoxSizer(wxVERTICAL, dialog, _("Options"));
	auto language_sizer = new wxBoxSizer(wxHORIZONTAL);
	language_sizer->Add(new wxStaticText(dialog, -1, _("Language")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
	language = new wxChoice(dialog, -1);
	for (auto const& item : Languages())
		language->Append(item.label);
	language_sizer->Add(language, 1, wxEXPAND);
	options_box->Add(language_sizer, 0, wxEXPAND | wxALL, 4);

	keep_line_breaks = new wxCheckBox(dialog, -1, _("Keep detected line breaks"));
	copy_after = new wxCheckBox(dialog, -1, _("Copy to clipboard after OCR"));
	insert_after = new wxCheckBox(dialog, -1, _("Insert into current line after OCR"));
	options_box->Add(keep_line_breaks, 0, wxLEFT | wxRIGHT | wxBOTTOM, 4);
	options_box->Add(copy_after, 0, wxLEFT | wxRIGHT | wxBOTTOM, 4);
	options_box->Add(insert_after, 0, wxLEFT | wxRIGHT | wxBOTTOM, 4);
	main_sizer->Add(options_box, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);

	auto result_box = new wxStaticBoxSizer(wxVERTICAL, dialog, _("Result"));
	result_text = new wxTextCtrl(dialog, -1, "", wxDefaultPosition, wxSize(520, 180), wxTE_MULTILINE | wxTE_RICH2);
	result_box->Add(result_text, 1, wxEXPAND | wxALL, 4);
	main_sizer->Add(result_box, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);

	auto progress_sizer = new wxBoxSizer(wxHORIZONTAL);
	status = new wxStaticText(dialog, -1, _("Ready"));
	gauge = new wxGauge(dialog, -1, 100, wxDefaultPosition, wxSize(140, -1));
	progress_sizer->Add(status, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
	progress_sizer->Add(gauge, 0, wxALIGN_CENTER_VERTICAL);
	main_sizer->Add(progress_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);

	auto button_sizer = new wxBoxSizer(wxHORIZONTAL);
	recognize_button = new wxButton(dialog, wxID_OK, _("Recognize"));
	insert_button = new wxButton(dialog, -1, _("Insert"));
	replace_button = new wxButton(dialog, -1, _("Replace Line..."));
	copy_button = new wxButton(dialog, -1, _("Copy"));
	close_button = new wxButton(dialog, wxID_CANCEL);
	button_sizer->Add(recognize_button, 0, wxRIGHT, 5);
	button_sizer->Add(insert_button, 0, wxRIGHT, 5);
	button_sizer->Add(replace_button, 0, wxRIGHT, 5);
	button_sizer->Add(copy_button, 0, wxRIGHT, 5);
	button_sizer->AddStretchSpacer();
	button_sizer->Add(close_button, 0);
	main_sizer->Add(button_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);

	source_frame->SetValue(!!c->project->VideoProvider());
	source_file->SetValue(!c->project->VideoProvider());
	keep_line_breaks->SetValue(OPT_GET("Tool/OCR/Keep Line Breaks")->GetBool());
	copy_after->SetValue(OPT_GET("Tool/OCR/Copy After OCR")->GetBool());
	insert_after->SetValue(OPT_GET("Tool/OCR/Insert After OCR")->GetBool());

	std::string saved_language = OPT_GET("Tool/OCR/Language")->GetString();
	int language_index = 0;
	for (size_t i = 0; i < Languages().size(); ++i) {
		if (Languages()[i].key == saved_language) {
			language_index = static_cast<int>(i);
			break;
		}
	}
	language->SetSelection(language_index);

	dialog->SetSizerAndFit(main_sizer);
	dialog->CenterOnParent();

	persist = agi::make_unique<PersistLocation>(dialog, "Tool/OCR");

	dialog->Bind(wxEVT_BUTTON, &DialogOCR::Impl::OnRecognize, this, wxID_OK);
	insert_button->Bind(wxEVT_BUTTON, &DialogOCR::Impl::OnInsert, this);
	replace_button->Bind(wxEVT_BUTTON, &DialogOCR::Impl::OnReplace, this);
	copy_button->Bind(wxEVT_BUTTON, &DialogOCR::Impl::OnCopy, this);
	dialog->Bind(EVT_OCR_COMPLETE, &DialogOCR::Impl::OnComplete, this);
	dialog->Bind(wxEVT_CLOSE_WINDOW, &DialogOCR::Impl::OnClose, this);
	dialog->Bind(wxEVT_TIMER, [=](wxTimerEvent&) { gauge->Pulse(); });
	source_frame->Bind(wxEVT_RADIOBUTTON, &DialogOCR::Impl::OnSourceChanged, this);
	source_file->Bind(wxEVT_RADIOBUTTON, &DialogOCR::Impl::OnSourceChanged, this);

	UpdateControls();
}

void DialogOCR::Impl::SaveOptions() {
	OPT_SET("Tool/OCR/Keep Line Breaks")->SetBool(keep_line_breaks->GetValue());
	OPT_SET("Tool/OCR/Copy After OCR")->SetBool(copy_after->GetValue());
	OPT_SET("Tool/OCR/Insert After OCR")->SetBool(insert_after->GetValue());
	OPT_SET("Tool/OCR/Language")->SetString(Languages()[language->GetSelection()].key);
}

ocr::OCROptions DialogOCR::Impl::CurrentOptions() const {
	ocr::OCROptions options;
	options.keep_line_breaks = keep_line_breaks->GetValue();
	options.language = Languages()[language->GetSelection()].key;
	return options;
}

void DialogOCR::Impl::SetRunning(bool value) {
	running = value;
	source_frame->Enable(!running);
	source_file->Enable(!running);
	file_picker->Enable(!running && source_file->GetValue());
	language->Enable(!running);
	keep_line_breaks->Enable(!running);
	copy_after->Enable(!running);
	insert_after->Enable(!running);
	recognize_button->Enable(!running);
	insert_button->Enable(!running && !result_text->IsEmpty());
	replace_button->Enable(!running && !result_text->IsEmpty());
	copy_button->Enable(!running && !result_text->IsEmpty());
	close_button->Enable(!running);

	if (running) {
		status->SetLabelText(_("Recognizing..."));
		pulse_timer.Start(100);
	}
	else {
		pulse_timer.Stop();
		gauge->SetValue(0);
	}
}

void DialogOCR::Impl::UpdateControls() {
	file_picker->Enable(!running && source_file->GetValue());
	insert_button->Enable(!running && !result_text->IsEmpty());
	replace_button->Enable(!running && !result_text->IsEmpty());
	copy_button->Enable(!running && !result_text->IsEmpty());
}

agi::fs::path DialogOCR::Impl::PrepareImage() {
	if (source_file->GetValue()) {
		auto path = agi::fs::path(std::wstring(file_picker->GetPath().wc_str()));
		if (path.empty())
			throw agi::InvalidInputException("Choose an image file before running OCR.");
		if (!agi::fs::FileExists(path))
			throw agi::FileNotFound("Image file not found: " + path.string());
		return path;
	}

	if (!c->project->VideoProvider())
		throw agi::InvalidInputException("No video is loaded. Open a video or choose an image file for OCR.");

	wxString temp_file = wxFileName::CreateTempFileName("aegisub-ocr-");
	if (temp_file.empty())
		throw agi::EnvironmentError("Failed to create a temporary image file for OCR.");

	auto image = GetCurrentFrameImage(c);
	if (!image.SaveFile(temp_file, wxBITMAP_TYPE_PNG)) {
		agi::fs::Remove(agi::fs::path(std::wstring(temp_file.wc_str())));
		throw agi::EnvironmentError("Failed to save the current video frame for OCR.");
	}

	return agi::fs::path(std::wstring(temp_file.wc_str()));
}

void DialogOCR::Impl::OnRecognize(wxCommandEvent&) {
	SaveOptions();

	ocr::OCROptions options = CurrentOptions();
	auto diagnostic = engine.GetDiagnostic(options);
	if (!diagnostic.empty()) {
		wxMessageBox(diagnostic, _("OCR runtime unavailable"), wxOK | wxICON_ERROR | wxCENTER, dialog);
		return;
	}

	agi::fs::path image_path;
	try {
		image_path = PrepareImage();
	}
	catch (agi::Exception const& e) {
		wxMessageBox(to_wx(e.GetMessage()), _("OCR"), wxOK | wxICON_ERROR | wxCENTER, dialog);
		return;
	}

	agi::fs::path temporary_image;
	if (source_frame->GetValue())
		temporary_image = image_path;

	SetRunning(true);

	auto engine_copy = engine;
	auto handler = dialog;
	agi::dispatch::Background().Async([handler, engine_copy, image_path, options, temporary_image]{
		OCRThreadResult thread_result;
		thread_result.temporary_image = temporary_image;
		thread_result.result = engine_copy.RecognizeImage(image_path, options);
		handler->AddPendingEvent(ValueEvent<OCRThreadResult>(EVT_OCR_COMPLETE, -1, std::move(thread_result)));
	});
}

void DialogOCR::Impl::OnComplete(ValueEvent<OCRThreadResult>& event) {
	auto const& thread_result = event.Get();
	if (!thread_result.temporary_image.empty()) {
		try {
			agi::fs::Remove(thread_result.temporary_image);
		}
		catch (agi::Exception const&) {
		}
	}

	SetRunning(false);

	auto const& result = thread_result.result;
	if (!result.ok) {
		status->SetLabelText(_("OCR failed"));
		wxMessageBox(to_wx(result.diagnostic), _("OCR failed"), wxOK | wxICON_ERROR | wxCENTER, dialog);
		UpdateControls();
		return;
	}

	result_text->SetValue(to_wx(result.text));
	if (result.text.empty())
		status->SetLabelText(to_wx(result.diagnostic.empty() ? "No text found." : result.diagnostic));
	else
		status->SetLabelText(fmt_tl("Recognized %d line(s).", static_cast<int>(result.lines.size())));

	if (!result.text.empty() && copy_after->GetValue())
		CopyResult();
	if (!result.text.empty() && insert_after->GetValue())
		InsertResult();

	UpdateControls();
}

void DialogOCR::Impl::InsertResult() {
	if (result_text->IsEmpty())
		return;

	if (!c->subsEditBox || !c->subsEditBox->InsertTextAtCaret(InsertionText(c, result_text->GetValue()))) {
		wxMessageBox(_("No active subtitle edit box is available."), _("OCR"), wxOK | wxICON_ERROR | wxCENTER, dialog);
		return;
	}

	status->SetLabelText(_("Inserted OCR text."));
}

void DialogOCR::Impl::CopyResult() {
	if (result_text->IsEmpty())
		return;

	SetClipboard(from_wx(result_text->GetValue()));
	status->SetLabelText(_("Copied OCR text to clipboard."));
}

void DialogOCR::Impl::OnInsert(wxCommandEvent&) {
	InsertResult();
}

void DialogOCR::Impl::OnCopy(wxCommandEvent&) {
	CopyResult();
}

void DialogOCR::Impl::OnReplace(wxCommandEvent&) {
	if (result_text->IsEmpty())
		return;

	AssDialogue *line = c->selectionController->GetActiveLine();
	if (!line) {
		wxMessageBox(_("No active subtitle line is selected."), _("OCR"), wxOK | wxICON_ERROR | wxCENTER, dialog);
		return;
	}

	if (wxMessageBox(_("Replace the current subtitle line text with the OCR result?"), _("Replace line text"), wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION | wxCENTER, dialog) != wxYES)
		return;

	line->Text = from_wx(DisplayTextToAss(result_text->GetValue()));
	c->ass->Commit(_("replace line with OCR text"), AssFile::COMMIT_DIAG_TEXT, -1, line);
	status->SetLabelText(_("Replaced current line text."));
}

void DialogOCR::Impl::OnClose(wxCloseEvent& event) {
	if (running) {
		event.Veto();
		return;
	}

	event.Skip();
}

void DialogOCR::Impl::OnSourceChanged(wxCommandEvent&) {
	UpdateControls();
}

DialogOCR::DialogOCR(agi::Context *context)
: wxDialog(context->parent, -1, _("Image to Text (OCR)"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
, impl(new Impl(this, context))
{
}

DialogOCR::~DialogOCR() {
	delete impl;
}

void ShowOCRDialog(agi::Context *c) {
	DialogOCR dialog(c);
	dialog.ShowModal();
}
