// [actor_MRU] BEGIN
// Copyright (c) 2025, JibunSenyou-Aegisub contributors
//
// Module that encapsulates the actor MRU logic and popup used by the fast
// naming workflow.

#pragma once

#include <memory>
#include <vector>

#include <wx/event.h>
#include <wx/popupwin.h>
#include <wx/string.h>

class AssFile;
class SubsEditBox;
class wxButton;
class wxComboBox;
class wxListBox;
class wxPanel;
class wxStaticText;

class ActorMRUWindow;

/// Manager that tracks recently used actor names and drives the popup window
/// displayed next to the actor field.
class ActorMRUManager final : public wxEvtHandler {
public:
	ActorMRUManager(SubsEditBox *owner, wxComboBox *actor_ctrl, wxButton *anchor_button);
	~ActorMRUManager();

	void SetFastModeEnabled(bool enabled);
	bool IsFastModeEnabled() const { return fast_mode_enabled_; }

	/// Update MRU data when a line is committed in fast mode.
	void OnActorCommitted(wxString const& new_actor, wxString const& old_actor, AssFile *subs);

	void OnActorFocusChanged(bool has_focus);
	void UpdateWindowVisibility();

	bool HandleUpKey();
	bool HandleDownKey();
	bool HandleEnterKey();

private:
	SubsEditBox *owner_ = nullptr;
	wxComboBox *actor_ctrl_ = nullptr;
	wxButton *anchor_button_ = nullptr;
	std::unique_ptr<ActorMRUWindow> window_;
	std::vector<wxString> names_;
	bool fast_mode_enabled_ = false;
	bool actor_has_focus_ = false;
	bool window_visible_ = false;
	int current_selection_ = wxNOT_FOUND;

	void EnsureWindow();
	void ShowWindow();
	void HideWindow();
	void PositionWindow();
	void RefreshWindow();
	void UpdateActiveState();
	bool SelectIndex(int index);
	bool StepSelection(int delta);
	bool HasSelection() const;
	bool HasEntries() const { return !names_.empty(); }
	wxString GetSelectedName() const;
	wxString Normalize(wxString const& value) const;
	void PromoteName(wxString const& name);
	void RemoveIfUnused(wxString const& actor, AssFile *subs);
	void TrimList();
	void ResetSelection();

	void OnListSelect(wxCommandEvent &evt);
	void OnListDClick(wxCommandEvent &evt);
};

/// Simple popup window showing the MRU entries.
class ActorMRUWindow final : public wxPopupWindow {
public:
	explicit ActorMRUWindow(wxWindow *parent);

	void SetNames(std::vector<wxString> const& names);
	void SetActive(bool active);
	void SetSelection(int index);
	int GetSelection() const;
	wxListBox *GetListBox() const { return list_; }

private:
	wxPanel *panel_ = nullptr;
	wxStaticText *label_ = nullptr;
	wxListBox *list_ = nullptr;
	bool is_active_ = false;

	void UpdateLabel(bool has_entries);
};
// [actor_MRU] END
