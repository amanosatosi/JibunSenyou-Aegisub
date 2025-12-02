#include "toast_popup.h"

#include <wx/gdicmn.h>
#include <wx/panel.h>
#include <wx/popupwin.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/timer.h>
#include <wx/window.h>

#include <algorithm>
#include <chrono>

namespace {

class ToastPopup final : public wxPopupTransientWindow {
public:
	ToastPopup(wxWindow *parent, const wxString &message)
	: wxPopupTransientWindow(parent, wxBORDER_NONE)
	, timer_(this)
	, start_time_(std::chrono::steady_clock::now())
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
		SetTransparency(255);
	}

	void ShowFor(wxWindow *anchor) {
		if (!anchor)
			anchor = GetParent();

		wxWindow *top = anchor ? wxGetTopLevelParent(anchor) : nullptr;
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
		start_time_ = std::chrono::steady_clock::now();
		SetTransparency(255);
		timer_.Start(timer_interval_ms_);
		Popup(top);
	}

protected:
	bool AcceptsFocus() const override { return false; }
	bool AcceptsFocusFromKeyboard() const override { return false; }
	bool AcceptsFocusRecursively() const override { return false; }

	void OnDismiss() override {
		timer_.Stop();
		wxPopupTransientWindow::OnDismiss();
		Destroy();
	}

private:
	wxTimer timer_;
	std::chrono::steady_clock::time_point start_time_;
	static constexpr int kLifetimeMs = 1700;
	static constexpr int kFadeMs = 300;
	static constexpr int timer_interval_ms_ = 30;
	bool supports_alpha_ = false;

	void SetTransparency(unsigned char alpha) {
#ifdef __WXMSW__
		supports_alpha_ = wxPopupTransientWindow::SetTransparent(alpha);
#else
		(void)alpha;
		supports_alpha_ = false;
#endif
	}

	void OnTimer(wxTimerEvent &) {
		using namespace std::chrono;
		auto elapsed = duration_cast<milliseconds>(steady_clock::now() - start_time_).count();

		if (elapsed >= kLifetimeMs) {
			timer_.Stop();
			Dismiss();
			return;
		}

		if (elapsed >= kLifetimeMs - kFadeMs) {
			double ratio = std::clamp((kLifetimeMs - elapsed) / static_cast<double>(kFadeMs), 0.0, 1.0);
			if (supports_alpha_)
				SetTransparency(static_cast<unsigned char>(ratio * 255));
		}
	}
};

} // namespace

void ShowToast(wxWindow *parent, const wxString &message) {
	if (!parent || message.empty())
		return;

	auto toast = new ToastPopup(parent, message);
	toast->ShowFor(parent);
}
