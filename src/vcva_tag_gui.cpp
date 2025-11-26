#include "vcva_tag_gui.h"

#include "colour_button.h"
#include "dialogs.h"
#include "value_event.h"

#include <vector>

#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/evtloop.h>
#include <wx/gbsizer.h>
#include <wx/intl.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>

namespace {

const wxSize slot_button_size(40, 40);
const wxSize corner_button_size(22, 22);
const wxSize top_bottom_button_size(90, 22);
const wxSize side_button_size(18, 60);

wxString GradientTooltip(const wxString& label, const wxString& slots) {
	return wxString::Format(_("Apply a color to %s (%s)"), label, slots);
}

class VcVaGradientDialog final : public wxDialog {
	VcVaGradientState state_;
	std::array<ColourButton*, 4> slot_buttons_{};
	bool alpha_slider_touched_ = false;
	std::function<void(const VcVaGradientState&)> preview_cb_;
	std::function<void()> revert_cb_;
	VcVaGradientResult result_;
	bool closed_ = false;

	void TriggerPreview() {
		state_.alpha_touched = alpha_slider_touched_;
		if (preview_cb_)
			preview_cb_(state_);
	}

	void ApplySlotColor(int idx, agi::Color color) {
		if (color.a != state_.alphas[idx]) {
			alpha_slider_touched_ = true;
			state_.alphas[idx] = color.a;
		}
		state_.colors[idx] = color;
		slot_buttons_[idx]->SetColor(color);
	}

	void OnSlotColor(ValueEvent<agi::Color>& evt) {
		for (size_t i = 0; i < slot_buttons_.size(); ++i) {
			if (slot_buttons_[i]->GetId() == evt.GetId()) {
				ApplySlotColor((int)i, evt.Get());
				TriggerPreview();
				break;
			}
		}
	}

	void FillSlots(const std::vector<int>& slots) {
		if (slots.empty()) return;
		agi::Color initial = state_.colors[slots.front()];
		bool ok = GetColorFromUser(this, initial, true, [&](agi::Color new_color) {
			for (int idx : slots)
				ApplySlotColor(idx, new_color);
			TriggerPreview();
		});
		if (!ok && preview_cb_) {
			state_.alpha_touched = alpha_slider_touched_;
			preview_cb_(state_);
		}
	}

	wxButton *CreateFillButton(const wxString& label, const wxString& slots_desc, std::vector<int> slots, const wxSize& size) {
		auto *btn = new wxButton(this, wxID_ANY, label, wxDefaultPosition, size);
		btn->SetToolTip(GradientTooltip(label, slots_desc));
		btn->Bind(wxEVT_BUTTON, [=](wxCommandEvent&) {
			FillSlots(slots);
		});
		return btn;
	}

	ColourButton *CreateSlotButton(int idx) {
		auto *btn = new ColourButton(this, slot_button_size, true, state_.colors[idx]);
		btn->SetToolTip(wxString::Format(_("Corner %d"), idx + 1));
		btn->Bind(EVT_COLOR, &VcVaGradientDialog::OnSlotColor, this);
		return btn;
	}

	void ComputeResult() {
		result_.accepted = true;
		result_.colors = state_.colors;
		result_.alphas = state_.alphas;

		bool color_diff = false;
		for (size_t i = 0; i < state_.colors.size(); ++i) {
			if (state_.colors[i].r != state_.style_colors[i].r ||
				state_.colors[i].g != state_.style_colors[i].g ||
				state_.colors[i].b != state_.style_colors[i].b) {
				color_diff = true;
				break;
			}
		}
		result_.has_color = color_diff;

		if (!alpha_slider_touched_) {
			result_.has_alpha = false;
			return;
		}

		bool alpha_diff = false;
		for (size_t i = 0; i < state_.alphas.size(); ++i) {
			if (state_.alphas[i] != state_.style_alphas[i]) {
				alpha_diff = true;
				break;
			}
		}
		result_.has_alpha = alpha_diff;
	}

	void HandleCancel() {
		if (closed_) return;
		closed_ = true;
		if (revert_cb_)
			revert_cb_();
		result_.accepted = false;
		EndModal(wxID_CANCEL);
	}

	void BuildLayout() {
		auto *grid = new wxGridBagSizer(5, 5);

		auto add_button = [&](wxButton *btn, int row, int col, int rowspan = 1, int colspan = 1, int flag = wxEXPAND) {
			grid->Add(btn, wxGBPosition(row, col), wxGBSpan(rowspan, colspan), flag);
		};

		auto top_left = CreateFillButton(wxString::FromUTF8(u8"⌈"), _("slots 1,2,3"), {0, 1, 2}, corner_button_size);
		auto top_right = CreateFillButton(wxString::FromUTF8(u8"⌉"), _("slots 1,2,4"), {0, 1, 3}, corner_button_size);
		auto bottom_left = CreateFillButton(wxString::FromUTF8(u8"⌊"), _("slots 1,3,4"), {0, 2, 3}, corner_button_size);
		auto bottom_right = CreateFillButton(wxString::FromUTF8(u8"⌋"), _("slots 2,3,4"), {1, 2, 3}, corner_button_size);

		auto top = CreateFillButton(wxString::FromUTF8(u8"—"), _("top edge"), {0, 1}, top_bottom_button_size);
		auto bottom = CreateFillButton(wxString::FromUTF8(u8"—"), _("bottom edge"), {2, 3}, top_bottom_button_size);
		auto left = CreateFillButton("|", _("left edge"), {0, 2}, side_button_size);
		auto right = CreateFillButton("|", _("right edge"), {1, 3}, side_button_size);

		add_button(top_left, 0, 0);
		add_button(top, 0, 1, 1, 3);
		add_button(top_right, 0, 4);

		add_button(left, 1, 0, 2, 1);
		add_button(right, 1, 4, 2, 1);
		add_button(bottom_left, 3, 0);
		add_button(bottom, 3, 1, 1, 3);
		add_button(bottom_right, 3, 4);

		grid->Add(10, 10, wxGBPosition(1, 2), wxGBSpan(2, 1));

		slot_buttons_[0] = CreateSlotButton(0);
		slot_buttons_[1] = CreateSlotButton(1);
		slot_buttons_[2] = CreateSlotButton(2);
		slot_buttons_[3] = CreateSlotButton(3);

		grid->Add(slot_buttons_[0], wxGBPosition(1, 1), wxGBSpan(1, 1), wxALIGN_CENTER);
		grid->Add(slot_buttons_[1], wxGBPosition(1, 3), wxGBSpan(1, 1), wxALIGN_CENTER);
		grid->Add(slot_buttons_[2], wxGBPosition(2, 1), wxGBSpan(1, 1), wxALIGN_CENTER);
		grid->Add(slot_buttons_[3], wxGBPosition(2, 3), wxGBSpan(1, 1), wxALIGN_CENTER);

		wxStdDialogButtonSizer *btn_sizer = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
		btn_sizer->Realize();

		auto *main_sizer = new wxBoxSizer(wxVERTICAL);
		main_sizer->Add(grid, 1, wxEXPAND | wxALL, 10);
		main_sizer->Add(btn_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

		SetSizerAndFit(main_sizer);

		Bind(wxEVT_BUTTON, &VcVaGradientDialog::OnOK, this, wxID_OK);
		Bind(wxEVT_BUTTON, &VcVaGradientDialog::OnCancel, this, wxID_CANCEL);
		Bind(wxEVT_CLOSE_WINDOW, &VcVaGradientDialog::OnClose, this);
	}

public:
	VcVaGradientDialog(
		wxWindow *parent,
		const VcVaGradientState& initial,
	std::function<void(const VcVaGradientState&)> preview_cb,
	std::function<void()> revert_cb)
: wxDialog(parent, wxID_ANY, _("Gradient"))
, state_(initial)
, alpha_slider_touched_(initial.alpha_touched)
, preview_cb_(std::move(preview_cb))
, revert_cb_(std::move(revert_cb))
{
	state_.alpha_touched = alpha_slider_touched_;
	result_.accepted = false;
	result_.colors = initial.colors;
		result_.alphas = initial.alphas;
		result_.has_alpha = false;
		result_.has_color = false;
		BuildLayout();
	}

	VcVaGradientResult const& GetResult() const {
		return result_;
	}

	void OnOK(wxCommandEvent&) {
		if (closed_) return;
		ComputeResult();
		closed_ = true;
		EndModal(wxID_OK);
	}

	void OnCancel(wxCommandEvent&) {
		HandleCancel();
	}

	void OnClose(wxCloseEvent&) {
		HandleCancel();
	}
};

} // namespace

VcVaGradientResult ShowVcVaGradientDialog(
	wxWindow *parent,
	const VcVaGradientState& initial,
	std::function<void(const VcVaGradientState&)> preview_cb,
	std::function<void()> revert_cb)
{
	VcVaGradientDialog dialog(parent, initial, std::move(preview_cb), std::move(revert_cb));
	dialog.ShowModal();
	return dialog.GetResult();
}
