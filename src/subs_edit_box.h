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

#include <array>
#include <deque>
#include <boost/container/map.hpp>
#include <boost/flyweight/flyweight_fwd.hpp>
#include <vector>

#include <wx/combobox.h>
#include <wx/panel.h>
#include <wx/timer.h>

#include <libaegisub/signal.h>

namespace agi { namespace vfr { class Framerate; } }
namespace agi { struct Context; }
namespace agi { class Time; }
class AssDialogue;
class AssStyle;
class RetinaHelper;
class SubsTextEditCtrl;
class TimeEdit;
class wxButton;
class wxCheckBox;
class wxRadioButton;
class wxSizer;
class wxSpinCtrl;
class wxStyledTextCtrl;
class wxStyledTextEvent;
class wxTextCtrl;
struct AssDialogueBase;

template<class Base> class Placeholder;

/// @brief Main subtitle edit box
///
/// Controls the text edit and all surrounding controls
class SubsEditBox final : public wxPanel {
	class FastNamePopup;
	enum TimeField {
		TIME_START = 0,
		TIME_END,
		TIME_DURATION
	};

	std::vector<agi::signal::Connection> connections;

	/// Currently active dialogue line
	AssDialogue *line = nullptr;
	AssStyle *active_style = nullptr;

	/// Are the buttons currently split into two lines?
	bool button_bar_split = true;
	/// Are the controls currently enabled?
	bool controls_enabled = true;

	agi::Context *c;

	agi::signal::Connection file_changed_slot;

	// Box controls
	wxCheckBox *comment_box;
	wxComboBox *style_box;
	wxButton *style_edit_button;
	Placeholder<wxComboBox> *actor_box;
	wxString actor_placeholder_text_;
	TimeEdit *start_time;
	TimeEdit *end_time;
	TimeEdit *duration;
	wxSpinCtrl *layer;
	std::array<wxTextCtrl *, 3> margin;
	Placeholder<wxComboBox> *effect_box;
	wxRadioButton *by_time;
	wxRadioButton *by_frame;
	wxTextCtrl *char_count;
	wxCheckBox *split_box;
	wxCheckBox *better_view_box;
	wxButton *join_next_button;
	wxButton *join_last_button;
	wxButton *bracket_button_ = nullptr;

	wxSizer *top_sizer;
	wxSizer *middle_right_sizer;
	wxSizer *middle_left_sizer;
	wxSizer *bottom_sizer;

	std::unique_ptr<RetinaHelper> retina_helper;
	std::vector<wxString> actor_values_;
	bool actor_autofill_guard = false;
	bool actor_should_autofill_ = false;
	bool actor_has_pending_selection_ = false;
	long actor_selection_start_ = 0;
	long actor_selection_end_ = 0;
	bool actor_text_amend_ = false;
	bool effect_text_amend_ = false;
	std::deque<wxString> fast_recent_names_;
	wxButton *actor_fast_button_ = nullptr;
	FastNamePopup *fast_popup_ = nullptr;
	bool fast_mode_enabled_ = false;
	bool fast_popup_visible_ = false;
	bool fast_has_active_name_ = false;
	wxString fast_active_name_;
	bool fast_preview_active_ = false;
	int fast_preview_index_ = -1;
	int fast_target_row_ = -1;
	AssDialogue *fast_target_line_ = nullptr;
	bool fast_list_changing_ = false;

	size_t last_bracket_pair_index_ = 1;
	bool better_view_enabled_ = true;


	void SetControlsState(bool state);
	/// @brief Update times of selected lines
	/// @param field Field which changed
	void CommitTimes(TimeField field);
	/// @brief Commits the current edit box contents
	/// @param desc Undo description to use
	void CommitText(wxString const& desc);
	void Commit(wxString const& desc, int type, bool amend, AssDialogue *line);

	/// Last commit ID for undo coalescing
	int commit_id = -1;

	/// Last used commit message to avoid coalescing different types of changes
	wxString last_commit_type;

	/// Last field to get a time commit, as they all have the same commit message
	int last_time_commit_type;

	/// Timer to stop coalescing changes after a break with no edits
	wxTimer undo_timer;

	/// The start and end times of the selected lines without changes made to
	/// avoid negative durations, so that they can be restored if future changes
	/// eliminate the negative durations
	boost::container::map<AssDialogue *, std::pair<agi::Time, agi::Time>> initial_times;

	// Constructor helpers
	wxTextCtrl *MakeMarginCtrl(wxString const& tooltip, int margin, wxString const& commit_msg);
	TimeEdit *MakeTimeCtrl(wxString const& tooltip, TimeField field);
	void MakeButton(const char *cmd_name);
	wxButton *MakeBottomButton(const char *cmd_name);
	wxComboBox *MakeComboBox(wxString const& initial_text, int style, void (SubsEditBox::*handler)(wxCommandEvent&), wxString const& tooltip);
	wxRadioButton *MakeRadio(wxString const& text, bool start, wxString const& tooltip);

	void OnChange(wxStyledTextEvent &event);
	void OnKeyDown(wxKeyEvent &event);

	void OnActiveLineChanged(AssDialogue *new_line);
	void OnSelectedSetChanged();
	void OnLineInitialTextChanged(std::string const& new_text);

	void OnFrameTimeRadio(wxCommandEvent &event);
	void OnStyleChange(wxCommandEvent &event);
	void OnActorChange(wxCommandEvent &event);
	void OnLayerEnter(wxCommandEvent &event);
	void OnCommentChange(wxCommandEvent &);
	void OnEffectChange(wxCommandEvent &);
	void OnFastButton(wxCommandEvent &);
	void OnBracketButton(wxCommandEvent &);
	void OnFastListSelect(wxCommandEvent &);
	void OnFastListDClick(wxCommandEvent &);
	void OnFastListKeyDown(wxKeyEvent &);
	void OnActorKillFocus(wxFocusEvent &);
	void OnEffectKillFocus(wxFocusEvent &);
	void OnSize(wxSizeEvent &event);
	void OnSplit(wxCommandEvent&);
	void OnBetterView(wxCommandEvent&);
	void DoOnSplit(bool show_original);
	void UpdateJoinButtons();
	void UpdateSecondaryEditor();
	wxString MakeDisplayText(wxString const& raw) const;
	wxString MakeAssText(wxString const& display) const;

	void SetPlaceholderCtrl(wxControl *ctrl, wxString const& value);

	/// @brief Set a field in each selected line to a specified value
	/// @param set   Callable which updates a passed line
	/// @param desc  Undo description to use
	/// @param type  Commit type to use
	/// @param amend Coalesce sequences of commits of the same type
	template<class setter>
	void SetSelectedRows(setter set, wxString const& desc, int type, bool amend = false);

	/// @brief Set a field in each selected line to a specified value
	/// @param field Field to set
	/// @param value Value to set the field to
	/// @param desc  Undo description to use
	/// @param type  Commit type to use
	/// @param amend Coalesce sequences of commits of the same type
	template<class T>
	void SetSelectedRows(T AssDialogueBase::*field, T value, wxString const& desc, int type, bool amend = false);

	template<class T>
	void SetSelectedRows(T AssDialogueBase::*field, wxString const& value, wxString const& desc, int type, bool amend = false);

	/// @brief Reload the current line from the file
	/// @param type AssFile::COMMITType
	void OnCommit(int type);

	void UpdateFields(int type, bool repopulate_lists);

	/// Regenerate a dropdown list with the unique values of a dialogue field
	void PopulateList(wxComboBox *combo, boost::flyweight<std::string> AssDialogue::*field);
	void PopulateActorList();
	void AutoFillActor();
	void OnActorKeyDown(wxKeyEvent &evt);
	void AddFastRecentName(wxString const& name);
	void ToggleFastMode();
	void UpdateFastPopup();
	void ShowFastPopup(bool focus_list);
	void HideFastPopup();
	void InsertBracketPair(wxString const& left, wxString const& right);
	void OnFastPopupDismiss();
	void OnFastPopupCharHook(wxKeyEvent &evt);
	void ApplyFastRecentSelection(int index, bool hide_popup = true, bool update_mru = true, bool restore_focus = true);
	void PreviewFastSelection(int index, bool keep_popup_focus = false);
	void FinalizeFastActiveFromActor(bool add_to_recent);
	void ClearFastActiveName();
	void ApplyFastActiveToCurrentLine();

	/// @brief Enable or disable frame timing mode
	void UpdateFrameTiming(agi::vfr::Framerate const& fps);

	/// Update the character count box for the given text
	void UpdateCharacterCount(std::string const& text);

	/// Call a command the restore focus to the edit box
	void CallCommand(const char *cmd_name);

	void SetDurationField();

	SubsTextEditCtrl *edit_ctrl;
	wxTextCtrl *secondary_editor;

public:
	/// @brief Constructor
	/// @param parent Parent window
	SubsEditBox(wxWindow *parent, agi::Context *context);
	~SubsEditBox();

	/// Insert text into the main edit control, honouring the current selection.
	/// @return True if text was inserted.
	bool InsertTextAtCaret(wxString const& text);
	void FocusTextCtrl();
};
