#include "toast_popup.h"

#include <wx/gdicmn.h>
#include <wx/panel.h>
#include <wx/popupwin.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/timer.h>

namespace {

class ToastPopup final : public wxPopupTransientWindow {
public:
	ToastPopup(wxWindow *parent, const wxString &message)
	: wxPopupTransientWindow(parent, wxBORDER_NONE)
	, timer_(this)
	{
		SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_INFOBK));

		auto panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
		panel->SetBackgroundColour(GetBackgroundColour());

		auto text = new wxStaticText(panel, wxID_ANY, message);
		text->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_INFOTEXT));

		auto panel_sizer = new wxBoxSizer(wxVERTICAL);
		panel_sizer->Add(text, 0, wxALL, 8);
		panel->SetSizer(panel_sizer);

		auto root_sizer = new wxBoxSizer(wxVERTICAL);
		root_sizer->Add(panel, 0, wxEXPAND);
		SetSizerAndFit(root_sizer);

		timer_.Bind(wxEVT_TIMER, &ToastPopup::OnTimer, this);
		timer_.StartOnce(1800);
	}

	void ShowFor(wxWindow *anchor) {
		if (!anchor)
			anchor = GetParent();

		wxWindow *top = anchor ? anchor->GetTopLevelParent() : nullptr;
		if (!top)
			top = anchor;

		wxPoint origin = top ? top->ClientToScreen(wxPoint(0, 0)) : wxPoint(0, 0);
		wxSize client = top ? top->GetClientSize() : wxGetDisplaySize();
		wxSize size = GetBestSize();

		int margin_top = 20;
		int x = origin.x + (client.GetWidth() - size.GetWidth()) / 2;
		if (x < origin.x) x = origin.x;
		int y = origin.y + margin_top;

		Move(x, y);
		Popup(top);
	}

protected:
	void OnDismiss() override {
		timer_.Stop();
		wxPopupTransientWindow::OnDismiss();
		Destroy();
	}

private:
	wxTimer timer_;

	void OnTimer(wxTimerEvent &) {
		Dismiss();
	}
};

} // namespace

void ShowToast(wxWindow *parent, const wxString &message) {
	if (!parent || message.empty())
		return;

	auto toast = new ToastPopup(parent, message);
	toast->ShowFor(parent);
}
