// [actor_MRU] BEGIN
#include "actor_MRU.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "subs_edit_box.h"
#include "utils.h"

#include <algorithm>

#include <wx/listbox.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/settings.h>
#include <wx/intl.h>

namespace {
constexpr size_t kMaxEntries = 20;
}

ActorMRUWindow::ActorMRUWindow(wxWindow *parent)
: wxPopupWindow(parent, wxBORDER_SIMPLE)
{
	panel_ = new wxPanel(this);
	panel_->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
	auto *panel_sizer = new wxBoxSizer(wxVERTICAL);
	label_ = new wxStaticText(panel_, wxID_ANY, _("Recent actors"));
	panel_sizer->Add(label_, 0, wxALL, 4);
	list_ = new wxListBox(panel_, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0, nullptr,
		wxLB_SINGLE | wxLB_NEEDED_SB | wxWANTS_CHARS);
	panel_sizer->Add(list_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);
	panel_->SetSizer(panel_sizer);

	auto *outer = new wxBoxSizer(wxVERTICAL);
	outer->Add(panel_, 1, wxEXPAND);
	SetSizerAndFit(outer);
	SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
}

void ActorMRUWindow::SetNames(std::vector<wxString> const& names) {
	list_->Freeze();
	list_->Clear();
	for (auto const& entry : names)
		list_->Append(entry);
	list_->Thaw();
	UpdateLabel(!names.empty());
	panel_->Layout();
	GetSizer()->Fit(this);
}

void ActorMRUWindow::SetActive(bool active) {
	is_active_ = active;
	wxColour colour = active
		? wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT)
		: wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);
	label_->SetForegroundColour(colour);
	UpdateLabel(list_->GetCount() > 0);
}

void ActorMRUWindow::SetSelection(int index) {
	if (index == wxNOT_FOUND)
		list_->DeselectAll();
	else if (index >= 0 && index < list_->GetCount())
		list_->SetSelection(index);
}

int ActorMRUWindow::GetSelection() const {
	return list_->GetSelection();
}

void ActorMRUWindow::UpdateLabel(bool has_entries) {
	wxString status = has_entries ? _("Recent actors") : _("Recent actors (empty)");
	if (!is_active_)
		status += wxS(" [") + _("inactive") + wxS("]");
	label_->SetLabel(wxS(">> ") + status);
}

ActorMRUManager::ActorMRUManager(SubsEditBox *owner, wxComboBox *actor_ctrl, wxButton *anchor_button)
: owner_(owner)
, actor_ctrl_(actor_ctrl)
, anchor_button_(anchor_button)
{
}

ActorMRUManager::~ActorMRUManager() = default;

void ActorMRUManager::SetFastModeEnabled(bool enabled) {
	if (fast_mode_enabled_ == enabled)
		return;
	fast_mode_enabled_ = enabled;
	if (!fast_mode_enabled_)
		HideWindow();
	else
		UpdateWindowVisibility();
}

void ActorMRUManager::OnActorCommitted(wxString const& new_actor, wxString const& old_actor, AssFile *subs) {
	if (!fast_mode_enabled_)
		return;

	wxString normalized_new = Normalize(new_actor);
	if (!normalized_new.empty()) {
		PromoteName(new_actor);
		ResetSelection();
	}
	else {
		RemoveIfUnused(old_actor, subs);
	}
	RefreshWindow();
	ShowWindow();
}

void ActorMRUManager::OnActorFocusChanged(bool has_focus) {
	actor_has_focus_ = has_focus;
	UpdateActiveState();
}

void ActorMRUManager::UpdateWindowVisibility() {
	if (!fast_mode_enabled_ || !actor_ctrl_) {
		HideWindow();
		return;
	}
	ShowWindow();
}

bool ActorMRUManager::HandleUpKey() {
	if (!fast_mode_enabled_)
		return false;
	ShowWindow();
	if (!HasEntries()) {
		ResetSelection();
		RefreshWindow();
		return true;
	}
	if (!HasSelection()) {
		SelectIndex(static_cast<int>(names_.size()) - 1);
		return true;
	}
	return StepSelection(-1);
}

bool ActorMRUManager::HandleDownKey() {
	if (!fast_mode_enabled_)
		return false;
	ShowWindow();
	if (!HasEntries()) {
		ResetSelection();
		RefreshWindow();
		return true;
	}
	if (!HasSelection()) {
		if (names_.size() >= 2)
			SelectIndex(1);
		else
			SelectIndex(0);
		return true;
	}
	return StepSelection(1);
}

bool ActorMRUManager::HandleEnterKey() {
	if (!fast_mode_enabled_ || !HasEntries() || !HasSelection() || !owner_)
		return false;

	wxString selected = GetSelectedName();
	if (selected.empty())
		return false;

	owner_->ApplyActorNameFromMRU(selected);
	owner_->AdvanceLineAfterMRU();
	PromoteName(selected);
	ResetSelection();
	RefreshWindow();
	ShowWindow();
	if (actor_ctrl_)
		actor_ctrl_->SetFocus();
	return true;
}

void ActorMRUManager::EnsureWindow() {
	if (window_ || !actor_ctrl_)
		return;

	window_ = std::make_unique<ActorMRUWindow>(actor_ctrl_);
	if (auto *list = window_->GetListBox()) {
		list->Bind(wxEVT_LISTBOX, &ActorMRUManager::OnListSelect, this);
		list->Bind(wxEVT_LISTBOX_DCLICK, &ActorMRUManager::OnListDClick, this);
	}
	window_->Hide();
	window_visible_ = false;
}

void ActorMRUManager::ShowWindow() {
	if (!fast_mode_enabled_ || !actor_ctrl_)
		return;

	EnsureWindow();
	if (!window_)
		return;

	RefreshWindow();
	PositionWindow();
	if (!window_visible_) {
		window_->Show();
		window_->Raise();
		window_visible_ = true;
	}
}

void ActorMRUManager::HideWindow() {
	if (!window_ || !window_visible_)
		return;
	window_->Hide();
	window_visible_ = false;
}

void ActorMRUManager::PositionWindow() {
	if (!window_ || !actor_ctrl_)
		return;

	wxWindow *anchor = anchor_button_
		? static_cast<wxWindow*>(anchor_button_)
		: static_cast<wxWindow*>(actor_ctrl_);
	wxPoint origin = anchor->ClientToScreen(wxPoint(0, anchor->GetSize().GetHeight()));
	wxSize size = window_->GetBestSize();
	int min_width = actor_ctrl_->GetSize().GetWidth();
	if (size.GetWidth() < min_width)
		size.SetWidth(min_width);
	window_->SetSize(size);

	wxPoint pos(origin.x, origin.y + 4);
	int screen_height = wxSystemSettings::GetMetric(wxSYS_SCREEN_Y);
	if (screen_height > 0 && pos.y + size.GetHeight() > screen_height)
		pos.y = origin.y - size.GetHeight() - anchor->GetSize().GetHeight() - 4;
	window_->Move(pos);
}

void ActorMRUManager::RefreshWindow() {
	if (!window_)
		return;
	window_->SetNames(names_);
	if (HasSelection())
		window_->SetSelection(current_selection_);
	else
		window_->SetSelection(wxNOT_FOUND);
	UpdateActiveState();
}

void ActorMRUManager::UpdateActiveState() {
	if (!window_)
		return;
	window_->SetActive(actor_has_focus_ && fast_mode_enabled_);
}

bool ActorMRUManager::SelectIndex(int index) {
	if (index < 0 || index >= static_cast<int>(names_.size()))
		return false;
	current_selection_ = index;
	RefreshWindow();
	return true;
}

bool ActorMRUManager::StepSelection(int delta) {
	if (!HasEntries())
		return false;
	if (!HasSelection())
		current_selection_ = 0;
	int count = static_cast<int>(names_.size());
	current_selection_ = (current_selection_ + delta + count) % count;
	RefreshWindow();
	return true;
}

bool ActorMRUManager::HasSelection() const {
	return current_selection_ != wxNOT_FOUND &&
		current_selection_ >= 0 &&
		current_selection_ < static_cast<int>(names_.size());
}

wxString ActorMRUManager::GetSelectedName() const {
	if (!HasSelection())
		return wxString();
	return names_[current_selection_];
}

wxString ActorMRUManager::Normalize(wxString const& value) const {
	wxString trimmed = value;
	trimmed.Trim(true);
	trimmed.Trim(false);
	return trimmed;
}

void ActorMRUManager::PromoteName(wxString const& name) {
	wxString normalized = Normalize(name);
	if (normalized.empty())
		return;

	auto it = std::find_if(names_.begin(), names_.end(), [&](wxString const& entry) {
		return Normalize(entry) == normalized;
	});
	if (it != names_.end())
		names_.erase(it);

	names_.insert(names_.begin(), name);
	TrimList();
}

void ActorMRUManager::RemoveIfUnused(wxString const& actor, AssFile *subs) {
	wxString normalized = Normalize(actor);
	if (normalized.empty() || !subs)
		return;

	for (auto const& entry : subs->Events) {
		wxString candidate = Normalize(to_wx(entry.Actor));
		if (candidate == normalized)
			return;
	}

	auto it = std::remove_if(names_.begin(), names_.end(), [&](wxString const& entry) {
		return Normalize(entry) == normalized;
	});
	if (it != names_.end()) {
		names_.erase(it, names_.end());
		if (!HasSelection())
			ResetSelection();
	}
}

void ActorMRUManager::TrimList() {
	if (names_.size() <= kMaxEntries)
		return;
	names_.resize(kMaxEntries);
}

void ActorMRUManager::ResetSelection() {
	current_selection_ = wxNOT_FOUND;
}

void ActorMRUManager::OnListSelect(wxCommandEvent &evt) {
	current_selection_ = evt.GetSelection();
	RefreshWindow();
	evt.StopPropagation();
	evt.Skip(false);
}

void ActorMRUManager::OnListDClick(wxCommandEvent &evt) {
	current_selection_ = evt.GetSelection();
	if (HandleEnterKey()) {
		evt.StopPropagation();
		evt.Skip(false);
		return;
	}
	evt.Skip();
}
// [actor_MRU] END
