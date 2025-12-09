// [actor_MRU] BEGIN
#include "actor_MRU.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "compat.h"
#include "subs_edit_box.h"
#include "utils.h"

#include <algorithm>

#include <wx/app.h>
#include <wx/combobox.h>
#include <wx/listbox.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/settings.h>
#include <wx/intl.h>
#include <libaegisub/log.h>

wxBEGIN_EVENT_TABLE(ActorMRUWindow, wxPopupWindow)
	EVT_KEY_DOWN(ActorMRUWindow::OnKeyDown)
	EVT_ACTIVATE(ActorMRUWindow::OnActivate)
wxEND_EVENT_TABLE()

namespace {
constexpr size_t kMaxEntries = 10; // actor MRU max size = 10
constexpr int kMaxVisibleRows = 10;
}

ActorMRUWindow::ActorMRUWindow(wxWindow *parent, ActorMRUManager *manager)
: wxPopupWindow(parent, wxBORDER_SIMPLE)
, manager_(manager)
{
	panel_ = new wxPanel(this);
	panel_->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
	auto *panel_sizer = new wxBoxSizer(wxVERTICAL);
	label_ = new wxStaticText(panel_, wxID_ANY, _("Recent actors"));
	panel_sizer->Add(label_, 0, wxALL, 4);
	list_ = new wxListBox(panel_, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0, nullptr,
		wxLB_SINGLE | wxLB_NEEDED_SB | wxWANTS_CHARS);
	list_->Bind(wxEVT_KEY_DOWN, &ActorMRUWindow::OnListBoxKeyDown, this);
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
	int count = list_ ? static_cast<int>(list_->GetCount()) : 0;
	int rows = ClampVisibleRows(count);
	pending_rows_ = rows;

	if (manager_ && manager_->IsWindowVisible())
		AdjustHeightForRows(rows);
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

void ActorMRUWindow::ShowForActor(wxWindow *anchor) {
	if (!anchor)
		return;
	if (!IsShown()) {
		Show();
		Raise();
	}
}

void ActorMRUWindow::HideWindow() {
	if (IsShown())
		Hide();
}

void ActorMRUWindow::UpdateLabel(bool has_entries) {
	wxString status = has_entries ? _("Recent actors") : _("Recent actors (empty)");
	if (!is_active_)
		status += wxS(" [") + _("inactive") + wxS("]");
	label_->SetLabel(wxS(">> ") + status);
}

int ActorMRUWindow::ClampVisibleRows(int rows) const {
	if (rows < 0)
		rows = 0;
	if (rows > kMaxVisibleRows)
		rows = kMaxVisibleRows;
	return rows;
}

void ActorMRUWindow::EnsureMetrics() {
	if (!list_)
		return;

	if (row_height_ <= 0) {
		row_height_ = list_->GetCharHeight();
		if (row_height_ <= 0)
			row_height_ = 12;
	}

	if (chrome_height_ >= 0)
		return;

	panel_->Layout();
	if (GetSize().GetHeight() == 0)
		Fit();

	wxSize full = GetSize();
	wxSize list_client = list_->GetClientSize();
	int client_height = list_client.GetHeight();
	if (client_height <= 0) {
		int approx_rows = kMaxVisibleRows;
		client_height = approx_rows * row_height_;
	}

	int approx_rows = client_height / row_height_;
	if (approx_rows < 1)
		approx_rows = 1;

	int rows_pixels = approx_rows * row_height_;
	int chrome = full.GetHeight() - rows_pixels;
	if (chrome < 0)
		chrome = 0;
	chrome_height_ = chrome;
}

void ActorMRUWindow::AdjustHeightForRows(int rows) {
	if (!list_)
		return;

	rows = ClampVisibleRows(rows);
	if (rows < 0)
		rows = 0;

	pending_rows_ = rows;

	EnsureMetrics();

	bool window_shown = IsShown();
	if (window_shown && rows <= visible_rows_cache_)
		return;

	int target_height = chrome_height_;
	if (rows > 0)
		target_height += row_height_ * rows;

	wxSize current = GetSize();
	int current_height = current.GetHeight();

	if (!window_shown) {
		if (current_height == 0 || target_height < current_height)
			SetSize(current.GetWidth(), target_height);
		else if (target_height > current_height)
			SetSize(current.GetWidth(), target_height);
	}
	else if (target_height > current_height) {
		SetSize(current.GetWidth(), target_height);
	}

	if (window_shown && rows > visible_rows_cache_)
		visible_rows_cache_ = rows;
}

void ActorMRUWindow::OnKeyDown(wxKeyEvent &evt) {
	wxLogDebug("[actor_MRU] ActorMRUWindow::OnKeyDown key=%d", evt.GetKeyCode());
	if (!manager_) {
		evt.Skip();
		return;
	}

	int key = evt.GetKeyCode();
	bool handled = false;
	switch (key) {
	case WXK_RETURN:
	case WXK_NUMPAD_ENTER:
		handled = manager_->HandleEnterKey();
		break;
	case WXK_UP:
	case WXK_NUMPAD_UP:
		handled = manager_->HandleUpKey();
		break;
	case WXK_DOWN:
	case WXK_NUMPAD_DOWN:
		handled = manager_->HandleDownKey();
		break;
	default:
		break;
	}

	if (handled) {
		evt.Skip(false);
		return;
	}
	evt.Skip();
}

void ActorMRUWindow::OnListBoxKeyDown(wxKeyEvent &evt) {
	// [actor_MRU] Forward list events to window handler so manager sees arrows/enter
	OnKeyDown(evt);
}

void ActorMRUWindow::OnActivate(wxActivateEvent &evt) {
	if (!evt.GetActive())
		HideWindow();
	evt.Skip();
}

ActorMRUManager::ActorMRUManager(SubsEditBox *owner, wxComboBox *actor_ctrl, wxButton *anchor_button)
: owner_(owner)
, actor_ctrl_(actor_ctrl)
, anchor_button_(anchor_button)
{
}

ActorMRUManager::~ActorMRUManager() {
	if (top_level_parent_ && app_activate_bound_)
		top_level_parent_->Unbind(wxEVT_ACTIVATE_APP, &ActorMRUManager::OnAppActivate, this);
}

void ActorMRUManager::SetFastModeEnabled(bool enabled) {
	if (fast_mode_enabled_ == enabled)
		return;

	fast_mode_enabled_ = enabled;
	LOG_D("actor/MRU") << "SetFastModeEnabled fast=" << fast_mode_enabled_;

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
	LOG_D("actor/MRU") << "OnActorFocusChanged has_focus=" << actor_has_focus_
		<< " fast=" << fast_mode_enabled_ << " visible=" << window_visible_;

	UpdateActiveState();
	UpdateWindowVisibility();
}

void ActorMRUManager::UpdateWindowVisibility() {
	if (!fast_mode_enabled_ || !actor_ctrl_ || !actor_has_focus_) {
		HideWindow();
		return;
	}
	ShowWindow();
}

bool ActorMRUManager::HandleUpKey() {
	LOG_D("actor/MRU") << "HandleUpKey()";
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
	LOG_D("actor/MRU") << "HandleDownKey()";
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
	LOG_D("actor/MRU") << "HandleEnterKey called: fast=" << fast_mode_enabled_
		<< " hasEntries=" << HasEntries() << " hasSelection=" << HasSelection()
		<< " owner=" << owner_;
	if (!fast_mode_enabled_ || !owner_ || !actor_ctrl_)
		return false;
	wxString typed = actor_ctrl_->GetValue();
	typed.Trim(true);
	typed.Trim(false);

	bool has_selection = HasSelection();
	wxString final_actor;
	if (has_selection)
		final_actor = GetSelectedName();
	else if (!typed.empty())
		final_actor = typed;
	else if (!names_.empty())
		final_actor = names_.front();
	// MRU behaviour: prefer explicit selection, then typed text, then entry[0] if text is empty.

	int current_row = owner_->line ? owner_->line->Row : -1;
	LOG_D("actor/MRU") << "HandleEnterKey applying '" << from_wx(final_actor) << "' on row " << current_row;
	owner_->ApplyActorNameFromMRU(final_actor);
	owner_->AdvanceLineAfterMRU();
	if (!final_actor.empty())
		PromoteName(final_actor);
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

	window_ = std::make_unique<ActorMRUWindow>(actor_ctrl_, this);
	if (auto *list = window_->GetListBox()) {
		list->Bind(wxEVT_LISTBOX, &ActorMRUManager::OnListSelect, this);
		list->Bind(wxEVT_LISTBOX_DCLICK, &ActorMRUManager::OnListDClick, this);
	}
	window_->Hide();
	window_visible_ = false;

	if (!app_activate_bound_) {
		top_level_parent_ = wxGetTopLevelParent(actor_ctrl_);
		if (top_level_parent_) {
			top_level_parent_->Bind(wxEVT_ACTIVATE_APP, &ActorMRUManager::OnAppActivate, this);
			app_activate_bound_ = true;
		}
	}
}

void ActorMRUManager::ShowWindow() {
	if (!fast_mode_enabled_ || !actor_ctrl_)
		return;

	EnsureWindow();
	if (!window_)
		return;

	RefreshWindow();
	if (!window_visible_) {
		window_->AdjustHeightForRows(window_->GetPendingVisibleRows());
		PositionWindow();
		window_->ShowForActor(actor_ctrl_);
		window_visible_ = true;
	}
}

void ActorMRUManager::HideWindow() {
	if (!window_ || !window_visible_)
		return;
	window_->HideWindow();
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

void ActorMRUManager::ApplySelectionToActorControl(bool select_all) {
	if (!actor_ctrl_ || !HasSelection())
		return;
	wxString name = GetSelectedName();
	if (name.empty())
		return;

	actor_ctrl_->ChangeValue(name);
	long end = static_cast<long>(name.length());
	if (select_all)
		actor_ctrl_->SetSelection(0, end);
	else
		actor_ctrl_->SetInsertionPoint(end);
}

bool ActorMRUManager::SelectIndex(int index) {
	if (index < 0 || index >= static_cast<int>(names_.size()))
		return false;
	current_selection_ = index;
	RefreshWindow();
	ApplySelectionToActorControl(true);
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
	ApplySelectionToActorControl(true);
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
	ApplySelectionToActorControl(true);
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

void ActorMRUManager::OnAppActivate(wxActivateEvent &evt) {
	if (!evt.GetActive())
		HideWindow();
	else if (fast_mode_enabled_ && actor_has_focus_)
		ShowWindow();
	evt.Skip();
}
// [actor_MRU] END
