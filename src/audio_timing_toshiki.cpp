// Copyright (c) 2026, JibunSenyou contributors
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#include <libaegisub/signal.h>

#include "ass_dialogue.h"
#include "ass_file.h"
#include "ass_karaoke.h"
#include "audio_controller.h"
#include "audio_marker.h"
#include "audio_rendering_style.h"
#include "audio_timing.h"
#include "compat.h"
#include "include/aegisub/context.h"
#include "options.h"
#include "pen.h"
#include "selection_controller.h"
#include "utils.h"

#include <libaegisub/make_unique.h>

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <wx/intl.h>

/// Marker implementation owned by Toshiki K-Timing.
///
/// This is intentionally separate from the Original K-Timing controller's
/// marker state. A marker returned for a drag must remain valid while the
/// preview layout is updated, so drag updates move these objects in place.
class ToshikiKTimingMarker final : public AudioMarker {
	int position;
	Pen *pen;
	FeetStyle feet;

public:
	ToshikiKTimingMarker(int position, Pen *pen, FeetStyle feet)
	: position(position), pen(pen), feet(feet) { }

	int GetPosition() const override { return position; }
	wxPen GetStyle() const override { return *pen; }
	FeetStyle GetFeet() const override { return feet; }

	void Move(int new_position) { position = new_position; }
};

/// @class AudioTimingControllerToshiki
/// @brief Slot-plan and preview timing controller for Toshiki K-Timing
///
/// Original K-Timing edits the karaoke syllable timings directly as the
/// normal karaoke interaction proceeds. Toshiki stores only boundaries the
/// user explicitly placed or loaded from existing karaoke. Text splitting
/// never invents timing; Commit() is the only operation which serializes it.
class AudioTimingControllerToshiki final : public AudioTimingController {
	std::vector<agi::signal::Connection> connections;
	agi::signal::Connection& file_changed_slot;

	agi::Context *c;
	AssDialogue *active_line;
	AssKaraoke *kara;

	size_t cur_syl = 0;
	size_t assigned_boundary_count = 0;
	std::vector<int> display_boundaries; ///< Explicitly assigned only; never provisional
	int pending_split_syl = -1;
	int pending_remove_syl = -1;
	bool reloading_karaoke = false;
	bool pending_changes = false;
	int commit_id = -1;
	bool had_existing_karaoke = false;
	bool had_committed_timing = false;
	int selected_tag_syl = -1;

	Pen separator_pen{"Colour/Audio Display/Syllable Boundaries", "Audio/Line Boundaries Thickness", wxPENSTYLE_DOT};
	Pen start_pen{"Colour/Audio Display/Line boundary Start", "Audio/Line Boundaries Thickness"};
	Pen end_pen{"Colour/Audio Display/Line boundary End", "Audio/Line Boundaries Thickness"};

	ToshikiKTimingMarker start_marker;
	ToshikiKTimingMarker end_marker;
	std::vector<ToshikiKTimingMarker> markers;

	AudioMarkerProviderKeyframes keyframes_provider;
	VideoPositionMarkerProvider video_position_provider;
	std::vector<AudioLabel> labels;

	void DoCommit();
	void AnnounceChanges(int syl);
	void RebuildMarkersAndLabels();
	void ApplyDisplayBoundaries(bool rebuild = true);
	void OnKaraokeSyllablesChanged();
	void ResetFromKaraokeLine();
	int AssignBoundary(int ms);
	int FindNearbyBoundary(int ms, int sensitivity, bool assigned_only) const;
	int MoveBoundary(ToshikiKTimingMarker *marker, int new_position);
	bool IsSingleUntimedSlot() const;
	size_t AssignedSlotCount() const;

	static int RoundToCentisecond(int position) {
		return (position + 5) / 10 * 10;
	}

public:
	AudioTimingControllerToshiki(agi::Context *c, AssKaraoke *kara, agi::signal::Connection& file_changed);

	void GetMarkers(TimeRange const& range, AudioMarkerVector &out) const override;
	void GetLabels(TimeRange const& range, std::vector<AudioLabel> &out) const override;
	wxString GetWarningMessage() const override { return wxString(); }
	TimeRange GetIdealVisibleTimeRange() const override;
	void GetRenderingStyles(AudioRenderingStyleRanges &ranges) const override;
	void GetToshikiKTimingPreviewRanges(std::vector<ToshikiKTimingPreviewRange> &ranges) const override;
	TimeRange GetPrimaryPlaybackRange() const override;
	TimeRange GetActiveLineRange() const override;
	void Next(NextMode mode) override;
	void Prev() override;
	void Commit() override;
	void Revert() override;
	void AddLeadIn() override;
	void AddLeadOut() override;
	void ModifyLength(int delta, bool shift_following) override;
	void ModifyStart(int delta) override;
	bool IsNearbyMarker(int ms, int sensitivity, bool alt_down) const override;
	std::vector<AudioMarker*> OnLeftClick(int ms, bool ctrl_down, bool alt_down, int sensitivity, int snap_range) override;
	std::vector<AudioMarker*> OnRightClick(int ms, bool ctrl_down, int sensitivity, int snap_range) override;
	void OnMarkerDrag(std::vector<AudioMarker*> const& marker, int new_position, int snap_range) override;
	void PrepareKaraokeSplit(size_t syl_idx) override;
	void PrepareKaraokeRemove(size_t syl_idx) override;
	void SetKaraokeTagType(std::string const& new_type) override;
};

std::unique_ptr<AudioTimingController> CreateToshikiKTimingController(agi::Context *c, AssKaraoke *kara, agi::signal::Connection& file_changed)
{
	return agi::make_unique<AudioTimingControllerToshiki>(c, kara, file_changed);
}

AudioTimingControllerToshiki::AudioTimingControllerToshiki(agi::Context *c, AssKaraoke *kara, agi::signal::Connection& file_changed)
: file_changed_slot(file_changed)
, c(c)
, active_line(c->selectionController->GetActiveLine())
, kara(kara)
, start_marker(active_line ? static_cast<int>(active_line->Start) : 0, &start_pen, AudioMarker::Feet_Right)
, end_marker(active_line ? static_cast<int>(active_line->End) : 0, &end_pen, AudioMarker::Feet_Left)
, keyframes_provider(c, "Audio/Display/Draw/Keyframes in Karaoke Mode")
, video_position_provider(c)
{
	connections.push_back(kara->AddSyllablesChangedListener(&AudioTimingControllerToshiki::OnKaraokeSyllablesChanged, this));
	keyframes_provider.AddMarkerMovedListener([=]{ AnnounceMarkerMoved(); });
	video_position_provider.AddMarkerMovedListener([=]{ AnnounceMarkerMoved(); });
	Revert();
}

TimeRange AudioTimingControllerToshiki::GetActiveLineRange() const {
	return TimeRange(start_marker.GetPosition(), end_marker.GetPosition());
}

TimeRange AudioTimingControllerToshiki::GetIdealVisibleTimeRange() const {
	return GetActiveLineRange();
}

TimeRange AudioTimingControllerToshiki::GetPrimaryPlaybackRange() const {
	if (labels.empty())
		return GetActiveLineRange();

	size_t syl = std::min(cur_syl, labels.size() - 1);
	if (syl >= AssignedSlotCount()) {
		int pending_start = assigned_boundary_count ? display_boundaries.back() : start_marker.GetPosition();
		return TimeRange(pending_start, end_marker.GetPosition());
	}
	return labels[syl].range;
}

void AudioTimingControllerToshiki::GetRenderingStyles(AudioRenderingStyleRanges &ranges) const {
	size_t assigned_slots = AssignedSlotCount();
	for (size_t i = 0; i < assigned_slots; ++i) {
		bool rest = kara->IsEmptySyllable(i) || kara->IsWhitespaceSyllable(i);
		AudioRenderingStyle style = rest ? AudioStyle_Inactive : AudioStyle_Selected;
		if (i == cur_syl)
			style = rest ? AudioStyle_Selected : AudioStyle_Primary;
		ranges.AddRange(labels[i].range.begin(), labels[i].range.end(), style);
	}
}

void AudioTimingControllerToshiki::GetToshikiKTimingPreviewRanges(std::vector<ToshikiKTimingPreviewRange> &ranges) const {
	size_t assigned_slots = AssignedSlotCount();
	// Pending slots stay in the splitter bar. Showing ranges for them here
	// would look like Auto Cut had already generated timing.
	for (size_t i = 0; i < assigned_slots; ++i) {
		ToshikiKTimingPreviewRange::State state = ToshikiKTimingPreviewRange::Assigned;
		if (i == cur_syl)
			state = ToshikiKTimingPreviewRange::Active;

		ranges.push_back(ToshikiKTimingPreviewRange{
			labels[i].range.begin(),
			labels[i].range.end(),
			state,
			kara->IsEmptySyllable(i)
		});
	}
}

size_t AudioTimingControllerToshiki::AssignedSlotCount() const {
	if (labels.empty())
		return 0;
	if (labels.size() == 1)
		return had_existing_karaoke || had_committed_timing ? 1 : 0;
	if (assigned_boundary_count >= labels.size() - 1)
		return labels.size();
	return std::min(assigned_boundary_count, labels.size());
}

void AudioTimingControllerToshiki::GetMarkers(TimeRange const& range, AudioMarkerVector &out) const {
	for (auto const& marker : markers) {
		if (range.contains(marker.GetPosition()))
			out.push_back(&marker);
	}

	if (range.contains(start_marker.GetPosition())) out.push_back(&start_marker);
	if (range.contains(end_marker.GetPosition())) out.push_back(&end_marker);

	keyframes_provider.GetMarkers(range, out);
	video_position_provider.GetMarkers(range, out);
}

void AudioTimingControllerToshiki::GetLabels(TimeRange const& range, std::vector<AudioLabel> &out) const {
	for (auto const& label : labels) {
		if (range.overlaps(label.range))
			out.push_back(label);
	}
}

void AudioTimingControllerToshiki::DoCommit() {
	if (!active_line) return;

	active_line->Start = start_marker.GetPosition();
	active_line->End = end_marker.GetPosition();
	active_line->Text = kara->GetText(!IsSingleUntimedSlot());
	file_changed_slot.Block();
	commit_id = c->ass->Commit(_("Toshiki K-Timing"), AssFile::COMMIT_DIAG_TEXT, commit_id, active_line);
	file_changed_slot.Unblock();
	pending_changes = false;
	had_committed_timing = !IsSingleUntimedSlot();
}

bool AudioTimingControllerToshiki::IsSingleUntimedSlot() const {
	return active_line && !had_existing_karaoke && !had_committed_timing &&
		kara->size() == 1 && !kara->IsEmptySyllable(0) && !kara->IsWhitespaceSyllable(0);
}

void AudioTimingControllerToshiki::Commit() {
	if (!active_line) return;

	if (IsSingleUntimedSlot()) {
		pending_changes = false;
		return;
	}

	// Only explicitly assigned boundaries are stored during editing. Commit
	// gives the first pending slot the remaining line duration and leaves any
	// later pending slots at zero, without inventing intermediate cuts.
	kara->SetTimingBoundaries(start_marker.GetPosition(), end_marker.GetPosition(), display_boundaries, false);
	DoCommit();
	display_boundaries.clear();
	for (auto it = kara->begin(); it != kara->end(); ++it) {
		if (it != kara->begin())
			display_boundaries.push_back(it->start_time);
	}
	assigned_boundary_count = display_boundaries.size();
	had_existing_karaoke = true;
	ApplyDisplayBoundaries();
	AnnounceUpdatedStyleRanges();
	AnnounceMarkerMoved();
	AnnounceLabelChanged();
}

void AudioTimingControllerToshiki::Revert() {
	active_line = c->selectionController->GetActiveLine();
	commit_id = -1;
	pending_changes = false;
	pending_split_syl = -1;
	pending_remove_syl = -1;
	selected_tag_syl = -1;
	had_committed_timing = false;

	if (!active_line) {
		start_marker.Move(0);
		end_marker.Move(0);
		assigned_boundary_count = 0;
		display_boundaries.clear();
		markers.clear();
		labels.clear();
		AnnounceUpdatedPrimaryRange();
		AnnounceUpdatedStyleRanges();
		AnnounceMarkerMoved();
		AnnounceLabelChanged();
		return;
	}

	start_marker.Move(active_line->Start);
	end_marker.Move(active_line->End);
	had_existing_karaoke = false;
	reloading_karaoke = true;
	kara->SetLine(active_line, false, false);
	had_existing_karaoke = kara->HasKaraokeTags();
	if (had_existing_karaoke)
		kara->SetLine(active_line, false, true);
	reloading_karaoke = false;

	ResetFromKaraokeLine();
	cur_syl = 0;
	AnnounceUpdatedPrimaryRange();
	AnnounceUpdatedStyleRanges();
	AnnounceMarkerMoved();
	AnnounceLabelChanged();
}

void AudioTimingControllerToshiki::ResetFromKaraokeLine() {
	display_boundaries.clear();
	assigned_boundary_count = 0;

	if (had_existing_karaoke) {
		for (auto it = kara->begin(); it != kara->end(); ++it) {
			if (it != kara->begin())
				display_boundaries.push_back(it->start_time);
		}
		assigned_boundary_count = display_boundaries.size();
	}

	ApplyDisplayBoundaries();
}

void AudioTimingControllerToshiki::OnKaraokeSyllablesChanged() {
	if (reloading_karaoke) return;

	active_line = c->selectionController->GetActiveLine();
	if (!active_line) {
		Revert();
		return;
	}

	start_marker.Move(active_line->Start);
	end_marker.Move(active_line->End);

	if (pending_remove_syl >= 0) {
		size_t remove_syl = static_cast<size_t>(pending_remove_syl);
		if (remove_syl > 0) {
			size_t boundary = remove_syl - 1;
			if (boundary < display_boundaries.size())
				display_boundaries.erase(display_boundaries.begin() + boundary);
			if (boundary < assigned_boundary_count)
				--assigned_boundary_count;
			if (remove_syl <= cur_syl && cur_syl > 0)
				--cur_syl;
		}
		pending_remove_syl = -1;
	}

	if (pending_split_syl >= 0) {
		size_t split_syl = static_cast<size_t>(pending_split_syl);
		if (split_syl < cur_syl)
			++cur_syl;
		if (split_syl < AssignedSlotCount()) {
			int inserted = split_syl < display_boundaries.size() ? display_boundaries[split_syl] : end_marker.GetPosition();
			display_boundaries.insert(display_boundaries.begin() + split_syl, inserted);
			++assigned_boundary_count;
		}
		pending_split_syl = -1;
	}

	assigned_boundary_count = std::min(assigned_boundary_count, kara->size() ? kara->size() - 1 : size_t(0));
	if (display_boundaries.size() > assigned_boundary_count)
		display_boundaries.resize(assigned_boundary_count);
	ApplyDisplayBoundaries();
	cur_syl = std::min(cur_syl, labels.empty() ? size_t(0) : labels.size() - 1);
	pending_changes = true;
	commit_id = -1;
	AnnounceUpdatedPrimaryRange();
	AnnounceUpdatedStyleRanges();
	AnnounceMarkerMoved();
	AnnounceLabelChanged();
}

void AudioTimingControllerToshiki::RebuildMarkersAndLabels() {
	markers.clear();
	labels.clear();
	markers.reserve(assigned_boundary_count);
	labels.reserve(kara->size());
	size_t assigned_slots = assigned_boundary_count;
	if (kara->size() == 1)
		assigned_slots = had_existing_karaoke || had_committed_timing ? 1 : 0;
	else if (assigned_boundary_count >= kara->size() - 1)
		assigned_slots = kara->size();

	for (size_t idx = 0; idx < kara->size(); ++idx) {
		if (idx > 0 && idx - 1 < assigned_boundary_count)
			markers.emplace_back(display_boundaries[idx - 1], &separator_pen, AudioMarker::Feet_None);

		// Unassigned labels use an empty range and therefore are not painted in
		// the audio display. Their text remains visible in the splitter bar.
		int label_start = end_marker.GetPosition();
		int label_end = end_marker.GetPosition();
		if (idx < assigned_slots) {
			label_start = idx == 0 ? start_marker.GetPosition() : display_boundaries[idx - 1];
			label_end = idx < assigned_boundary_count ? display_boundaries[idx] : end_marker.GetPosition();
		}
		auto it = kara->begin();
		std::advance(it, idx);
		wxString label_text = it->text.empty() ? wxString(wxS("rest")) :
			kara->IsWhitespaceSyllable(idx) ? wxString(wxS("space")) : to_wx(it->text);
		labels.push_back(AudioLabel{label_text, TimeRange(label_start, label_end)});
	}
}

void AudioTimingControllerToshiki::ApplyDisplayBoundaries(bool rebuild) {
	// This updates preview geometry only. Do not write these boundaries into
	// AssKaraoke here; text cuts and preview edits stay uncommitted.
	if (rebuild || markers.size() != assigned_boundary_count || labels.size() != kara->size()) {
		RebuildMarkersAndLabels();
		return;
	}

	for (size_t i = 0; i < assigned_boundary_count; ++i)
		markers[i].Move(display_boundaries[i]);

	size_t assigned_slots = AssignedSlotCount();
	for (size_t i = 0; i < labels.size(); ++i) {
		int begin = end_marker.GetPosition();
		int end = end_marker.GetPosition();
		if (i < assigned_slots) {
			begin = i == 0 ? start_marker.GetPosition() : display_boundaries[i - 1];
			end = i < assigned_boundary_count ? display_boundaries[i] : end_marker.GetPosition();
		}
		labels[i].range = TimeRange(begin, end);
	}
}

int AudioTimingControllerToshiki::AssignBoundary(int ms) {
	if (kara->size() < 2 || assigned_boundary_count >= kara->size() - 1)
		return -1;

	size_t index = assigned_boundary_count;
	int minimum = index ? display_boundaries[index - 1] : start_marker.GetPosition();
	int position = mid(minimum, RoundToCentisecond(ms), end_marker.GetPosition());
	display_boundaries.push_back(position);
	++assigned_boundary_count;
	ApplyDisplayBoundaries();

	cur_syl = std::min(index + 1, labels.empty() ? size_t(0) : labels.size() - 1);
	AnnounceChanges(static_cast<int>(cur_syl));
	return static_cast<int>(index);
}

int AudioTimingControllerToshiki::FindNearbyBoundary(int ms, int sensitivity, bool assigned_only) const {
	int result = -1;
	int best_distance = sensitivity + 1;
	size_t limit = assigned_only ? std::min(assigned_boundary_count, markers.size()) : markers.size();
	for (size_t i = 0; i < limit; ++i) {
		int distance = std::abs(markers[i].GetPosition() - ms);
		// Prefer the trailing boundary when zero-duration slots place two
		// handles at the same position. Dragging it expands the zero slot.
		if (distance <= sensitivity && distance <= best_distance) {
			result = static_cast<int>(i);
			best_distance = distance;
		}
	}
	return result;
}

bool AudioTimingControllerToshiki::IsNearbyMarker(int ms, int sensitivity, bool) const {
	return FindNearbyBoundary(ms, sensitivity, true) >= 0;
}

template<typename Marker>
static std::vector<AudioMarker*> one_marker(Marker &marker) {
	return { &marker };
}

std::vector<AudioMarker*> AudioTimingControllerToshiki::OnLeftClick(int ms, bool, bool, int sensitivity, int) {
	int marker_index = FindNearbyBoundary(ms, sensitivity, true);
	if (marker_index < 0)
		marker_index = AssignBoundary(ms);

	if (marker_index >= 0) {
		cur_syl = std::min(static_cast<size_t>(marker_index) + 1, labels.empty() ? size_t(0) : labels.size() - 1);
		AnnounceUpdatedPrimaryRange();
		AnnounceUpdatedStyleRanges();
		return one_marker(markers[marker_index]);
	}

	cur_syl = std::min(static_cast<size_t>(std::lower_bound(display_boundaries.begin(), display_boundaries.end(), ms) - display_boundaries.begin()), labels.empty() ? size_t(0) : labels.size() - 1);
	AnnounceUpdatedPrimaryRange();
	AnnounceUpdatedStyleRanges();
	return {};
}

std::vector<AudioMarker*> AudioTimingControllerToshiki::OnRightClick(int ms, bool, int sensitivity, int) {
	int marker_index = FindNearbyBoundary(ms, sensitivity, true);
	if (marker_index >= 0)
		selected_tag_syl = marker_index;
	else {
		selected_tag_syl = -1;
		for (size_t i = 0; i < labels.size(); ++i) {
			if (labels[i].range.contains(ms)) {
				selected_tag_syl = static_cast<int>(i);
				break;
			}
		}
	}

	cur_syl = selected_tag_syl >= 0 ? static_cast<size_t>(selected_tag_syl) : cur_syl;
	AnnounceUpdatedPrimaryRange();
	AnnounceUpdatedStyleRanges();
	return {};
}

int AudioTimingControllerToshiki::MoveBoundary(ToshikiKTimingMarker *marker, int new_position) {
	if (markers.empty()) return -1;

	size_t index = static_cast<size_t>(marker - &markers.front());
	if (index >= markers.size() || index >= assigned_boundary_count)
		return -1;

	int minimum = index == 0 ? start_marker.GetPosition() : markers[index - 1].GetPosition();
	int maximum = index + 1 < assigned_boundary_count ? markers[index + 1].GetPosition() : end_marker.GetPosition();
	new_position = mid(minimum, RoundToCentisecond(new_position), maximum);
	if (new_position == marker->GetPosition())
		return -1;

	display_boundaries[index] = new_position;
	// Do not rebuild here. AudioDisplay owns the marker pointer captured at
	// mouse-down; moving the existing objects keeps that pointer valid.
	ApplyDisplayBoundaries(false);
	return static_cast<int>(index + 1);
}

void AudioTimingControllerToshiki::AnnounceChanges(int syl) {
	if (syl >= 0 && (static_cast<size_t>(syl) == cur_syl || static_cast<size_t>(syl) == cur_syl + 1)) {
		AnnounceUpdatedPrimaryRange();
		AnnounceUpdatedStyleRanges();
	}
	pending_changes = true;
	commit_id = -1;
	AnnounceMarkerMoved();
	AnnounceLabelChanged();
}

void AudioTimingControllerToshiki::OnMarkerDrag(std::vector<AudioMarker*> const& dragged, int new_position, int) {
	if (dragged.empty()) return;

	int syl = MoveBoundary(static_cast<ToshikiKTimingMarker *>(dragged.front()), new_position);
	if (syl >= 0)
		AnnounceChanges(syl);
}

void AudioTimingControllerToshiki::AddLeadIn() {
	start_marker.Move(start_marker.GetPosition() - OPT_GET("Audio/Lead/IN")->GetInt());
	if (!labels.empty())
		labels.front().range = TimeRange(start_marker.GetPosition(), labels.front().range.end());
	kara->SetLineTimes(start_marker.GetPosition(), end_marker.GetPosition());
	ApplyDisplayBoundaries(false);
	AnnounceChanges(static_cast<int>(cur_syl));
}

void AudioTimingControllerToshiki::AddLeadOut() {
	end_marker.Move(end_marker.GetPosition() + OPT_GET("Audio/Lead/OUT")->GetInt());
	if (!labels.empty())
		labels.back().range = TimeRange(labels.back().range.begin(), end_marker.GetPosition());
	kara->SetLineTimes(start_marker.GetPosition(), end_marker.GetPosition());
	ApplyDisplayBoundaries(false);
	AnnounceChanges(static_cast<int>(cur_syl));
}

void AudioTimingControllerToshiki::ModifyLength(int delta, bool) {
	if (cur_syl < markers.size())
		OnMarkerDrag(one_marker(markers[cur_syl]), markers[cur_syl].GetPosition() + delta * 10, 0);
}

void AudioTimingControllerToshiki::ModifyStart(int delta) {
	if (cur_syl > 0 && cur_syl - 1 < markers.size())
		OnMarkerDrag(one_marker(markers[cur_syl - 1]), markers[cur_syl - 1].GetPosition() + delta * 10, 0);
}

void AudioTimingControllerToshiki::Next(NextMode mode) {
	if (mode != TIMING_UNIT)
		cur_syl = markers.size();
	++cur_syl;
	if (cur_syl > markers.size()) {
		--cur_syl;
		c->selectionController->NextLine();
	}
	else {
		AnnounceUpdatedPrimaryRange();
		AnnounceUpdatedStyleRanges();
	}
	c->audioController->PlayPrimaryRange();
}

void AudioTimingControllerToshiki::Prev() {
	if (cur_syl == 0) {
		c->selectionController->PrevLine();
		return;
	}
	--cur_syl;
	AnnounceUpdatedPrimaryRange();
	AnnounceUpdatedStyleRanges();
	c->audioController->PlayPrimaryRange();
}

void AudioTimingControllerToshiki::PrepareKaraokeSplit(size_t syl_idx) {
	pending_split_syl = static_cast<int>(syl_idx);
}

void AudioTimingControllerToshiki::PrepareKaraokeRemove(size_t syl_idx) {
	pending_remove_syl = static_cast<int>(syl_idx);
}

void AudioTimingControllerToshiki::SetKaraokeTagType(std::string const& new_type) {
	if (selected_tag_syl >= 0 && static_cast<size_t>(selected_tag_syl) < kara->size())
		kara->SetSyllableTagType(static_cast<size_t>(selected_tag_syl), new_type, false);
	else
		kara->SetTagType(new_type, false);
	selected_tag_syl = -1;
}
