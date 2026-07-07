// Copyright (c) 2011, Thomas Goyne <plorkyeran@aegisub.org>
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
//
// Aegisub Project http://www.aegisub.org/

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

#include <boost/range/algorithm/copy.hpp>
#include <boost/range/adaptor/filtered.hpp>
#include <boost/range/adaptor/sliced.hpp>
#include <algorithm>
#include <cstdlib>
#include <wx/intl.h>

/// @class KaraokeMarker
/// @brief AudioMarker implementation for AudioTimingControllerKaraoke
class KaraokeMarker final : public AudioMarker {
	int position;
	Pen *pen = nullptr;
	FeetStyle style = Feet_None;
public:

	int GetPosition() const override { return position; }
	wxPen GetStyle() const override { return *pen; }
	FeetStyle GetFeet() const override { return style; }

	void Move(int new_pos) { position = new_pos; }

	KaraokeMarker(int position) : position(position) { }

	KaraokeMarker(int position, Pen *pen, FeetStyle style)
	: position(position)
	, pen(pen)
	, style(style)
	{
	}

	operator int() const { return position; }
};

/// @class AudioTimingControllerKaraoke
/// @brief Karaoke timing mode for timing subtitles
///
/// Displays the active line with draggable markers between each pair of
/// adjacent syllables, along with the text of each syllable.
///
/// This does not support \kt, as it inherently requires that the end time of
/// one syllable be the same as the start time of the next one.
class AudioTimingControllerKaraoke final : public AudioTimingController {
	std::vector<agi::signal::Connection> connections;
	agi::signal::Connection& file_changed_slot;

	agi::Context *c;          ///< Project context
	AssDialogue *active_line; ///< Currently active line
	AssKaraoke *kara;         ///< Parsed karaoke model provided by karaoke controller

	size_t cur_syl = 0; ///< Index of currently selected syllable in the line
	bool spectrogram_timing = false; ///< Assign boundaries by ordered audio clicks
	size_t spectrogram_next_boundary = 0; ///< Next boundary to assign in spectrogram timing mode
	std::vector<int> spectrogram_boundaries; ///< Explicitly assigned boundaries
	bool reloading_karaoke = false; ///< Suppress refresh signal while reparsing on revert

	/// Pen used for the mid-syllable markers
	Pen separator_pen{"Colour/Audio Display/Syllable Boundaries", "Audio/Line Boundaries Thickness", wxPENSTYLE_DOT};
	/// Pen used for the start-of-line marker
	Pen start_pen{"Colour/Audio Display/Line boundary Start", "Audio/Line Boundaries Thickness"};
	/// Pen used for the end-of-line marker
	Pen end_pen{"Colour/Audio Display/Line boundary End", "Audio/Line Boundaries Thickness"};

	/// Immobile marker for the beginning of the line
	KaraokeMarker start_marker;
	/// Immobile marker for the end of the line
	KaraokeMarker end_marker;
	/// Mobile markers between each pair of syllables
	std::vector<KaraokeMarker> markers;

	/// Marker provider for video keyframes
	AudioMarkerProviderKeyframes keyframes_provider;

	/// Marker provider for video playback position
	VideoPositionMarkerProvider video_position_provider;

	/// Labels containing the stripped text of each syllable
	std::vector<AudioLabel> labels;

	 /// Should changes be automatically commited?
	bool auto_commit = OPT_GET("Audio/Auto/Commit")->GetBool();
	int commit_id = -1;   ///< Last commit id used for an autocommit
	bool pending_changes; ///< Are there any pending changes to be committed?

	void DoCommit();
	void ApplyLead(bool announce_primary);
	int MoveMarker(KaraokeMarker *marker, int new_position);
	void AnnounceChanges(int syl);
	void RebuildMarkersAndLabels();
	void UpdateSpectrogramLabelsFromBoundaries();
	void OnKaraokeSyllablesChanged();
	void ResetSpectrogramBoundaryStateFromKaraoke();
	void ApplyBoundaryVector(std::vector<int> const& boundaries, int selected_syl);
	bool AssignSpectrogramBoundary(int ms);
	int FindNearbyMarker(int ms, int sensitivity) const;
	bool ShouldNoOpSingleSlotCommit() const;

public:
	// AudioTimingController implementation
	void GetMarkers(const TimeRange &range, AudioMarkerVector &out_markers) const override;
	wxString GetWarningMessage() const override { return ""; }
	TimeRange GetIdealVisibleTimeRange() const override;
	void GetRenderingStyles(AudioRenderingStyleRanges &ranges) const override;
	TimeRange GetPrimaryPlaybackRange() const override;
	TimeRange GetActiveLineRange() const override;
	void GetLabels(const TimeRange &range, std::vector<AudioLabel> &out_labels) const override;
	void Next(NextMode mode) override;
	void Prev() override;
	void Commit() override;
	void Revert() override;
	void AddLeadIn() override;
	void AddLeadOut() override;
	void ModifyLength(int delta, bool shift_following) override;
	void ModifyStart(int delta) override;
	bool IsNearbyMarker(int ms, int sensitivity, bool) const override;
	std::vector<AudioMarker*> OnLeftClick(int ms, bool, bool, int sensitivity, int) override;
	std::vector<AudioMarker*> OnRightClick(int ms, bool, int, int) override;
	void OnMarkerDrag(std::vector<AudioMarker*> const& marker, int new_position, int) override;
	void SetSpectrogramKaraokeTiming(bool enabled) override;

	AudioTimingControllerKaraoke(agi::Context *c, AssKaraoke *kara, agi::signal::Connection& file_changed);
};

std::unique_ptr<AudioTimingController> CreateKaraokeTimingController(agi::Context *c, AssKaraoke *kara, agi::signal::Connection& file_changed)
{
	return agi::make_unique<AudioTimingControllerKaraoke>(c, kara, file_changed);
}

AudioTimingControllerKaraoke::AudioTimingControllerKaraoke(agi::Context *c, AssKaraoke *kara, agi::signal::Connection& file_changed)
: file_changed_slot(file_changed)
, c(c)
, active_line(c->selectionController->GetActiveLine())
, kara(kara)
, start_marker(active_line ? static_cast<int>(active_line->Start) : 0, &start_pen, AudioMarker::Feet_Right)
, end_marker(active_line ? static_cast<int>(active_line->End) : 0, &end_pen, AudioMarker::Feet_Left)
, keyframes_provider(c, "Audio/Display/Draw/Keyframes in Karaoke Mode")
, video_position_provider(c)
{
	connections.push_back(kara->AddSyllablesChangedListener(&AudioTimingControllerKaraoke::OnKaraokeSyllablesChanged, this));
	connections.push_back(OPT_SUB("Audio/Auto/Commit", [=](agi::OptionValue const& opt) { auto_commit = opt.GetBool(); }));

	keyframes_provider.AddMarkerMovedListener([=]{ AnnounceMarkerMoved(); });
	video_position_provider.AddMarkerMovedListener([=]{ AnnounceMarkerMoved(); });

	Revert();
}

void AudioTimingControllerKaraoke::Next(NextMode mode) {
	// Don't create new lines since it's almost never useful to k-time a line
	// before dialogue timing it
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

void AudioTimingControllerKaraoke::Prev() {
	if (cur_syl == 0) {
		AssDialogue *old_line = active_line;
		c->selectionController->PrevLine();
		if (old_line != active_line) {
			cur_syl = markers.size();
			AnnounceUpdatedPrimaryRange();
			AnnounceUpdatedStyleRanges();
		}
	}
	else {
		--cur_syl;
		AnnounceUpdatedPrimaryRange();
		AnnounceUpdatedStyleRanges();
	}

	c->audioController->PlayPrimaryRange();
}

void AudioTimingControllerKaraoke::GetRenderingStyles(AudioRenderingStyleRanges &ranges) const
{
	if (spectrogram_timing) {
		for (size_t i = 0; i < labels.size() && i < spectrogram_next_boundary; ++i) {
			auto style = (kara->IsEmptySyllable(i) || kara->IsWhitespaceSyllable(i)) ? AudioStyle_Inactive : AudioStyle_Selected;
			ranges.AddRange(labels[i].range.begin(), labels[i].range.end(), style);
		}

		if (cur_syl < labels.size()) {
			auto style = (kara->IsEmptySyllable(cur_syl) || kara->IsWhitespaceSyllable(cur_syl)) ? AudioStyle_Selected : AudioStyle_Primary;
			ranges.AddRange(labels[cur_syl].range.begin(), labels[cur_syl].range.end(), style);
		}
		return;
	}

	TimeRange sr = GetPrimaryPlaybackRange();
	ranges.AddRange(sr.begin(), sr.end(), AudioStyle_Primary);
	ranges.AddRange(start_marker, end_marker, AudioStyle_Selected);
}

TimeRange AudioTimingControllerKaraoke::GetPrimaryPlaybackRange() const {
	return TimeRange(
		cur_syl > 0 ? markers[cur_syl - 1] : start_marker,
		cur_syl < markers.size() ? markers[cur_syl] : end_marker);
}

TimeRange AudioTimingControllerKaraoke::GetActiveLineRange() const {
	return TimeRange(start_marker, end_marker);
}

TimeRange AudioTimingControllerKaraoke::GetIdealVisibleTimeRange() const {
	return GetActiveLineRange();
}

void AudioTimingControllerKaraoke::GetMarkers(TimeRange const& range, AudioMarkerVector &out) const {
	size_t i;
	for (i = 0; i < markers.size() && markers[i] < range.begin(); ++i) ;
	for (; i < markers.size() && markers[i] < range.end(); ++i)
		out.push_back(&markers[i]);

	if (range.contains(start_marker)) out.push_back(&start_marker);
	if (range.contains(end_marker)) out.push_back(&end_marker);

	keyframes_provider.GetMarkers(range, out);
	video_position_provider.GetMarkers(range, out);
}

void AudioTimingControllerKaraoke::DoCommit() {
	active_line->Text = kara->GetText(!ShouldNoOpSingleSlotCommit());
	file_changed_slot.Block();
	commit_id = c->ass->Commit(_("karaoke timing"), AssFile::COMMIT_DIAG_TEXT, commit_id, active_line);
	file_changed_slot.Unblock();
	pending_changes = false;
}

void AudioTimingControllerKaraoke::Commit() {
	if (spectrogram_timing) {
		if (ShouldNoOpSingleSlotCommit()) {
			if (active_line->Text != kara->GetText(false))
				DoCommit();
			else
				pending_changes = false;
			return;
		}
		if (kara->size() > 1 && spectrogram_next_boundary < kara->size() - 1)
			return;
		if (kara->size() == 1)
			kara->SetTimingBoundaries(start_marker, end_marker, {}, false);
	}

	if ((spectrogram_timing || !auto_commit) && pending_changes)
		DoCommit();
}

void AudioTimingControllerKaraoke::Revert() {
	active_line = c->selectionController->GetActiveLine();
	if (!active_line) {
		cur_syl = 0;
		spectrogram_next_boundary = 0;
		spectrogram_boundaries.clear();
		commit_id = -1;
		pending_changes = false;
		start_marker.Move(0);
		end_marker.Move(0);
		markers.clear();
		labels.clear();
		AnnounceUpdatedPrimaryRange();
		AnnounceUpdatedStyleRanges();
		AnnounceMarkerMoved();
		AnnounceLabelChanged();
		return;
	}

	reloading_karaoke = true;
	kara->SetLine(active_line, !spectrogram_timing, !spectrogram_timing);
	if (spectrogram_timing && kara->HasKaraokeTags())
		kara->SetLine(active_line, false, true);
	if (spectrogram_timing)
		ResetSpectrogramBoundaryStateFromKaraoke();
	else {
		spectrogram_next_boundary = 0;
		spectrogram_boundaries.clear();
	}
	reloading_karaoke = false;

	cur_syl = 0;
	commit_id = -1;
	pending_changes = false;

	start_marker.Move(active_line->Start);
	end_marker.Move(active_line->End);

	RebuildMarkersAndLabels();

	AnnounceUpdatedPrimaryRange();
	AnnounceUpdatedStyleRanges();
	AnnounceMarkerMoved();
}

void AudioTimingControllerKaraoke::OnKaraokeSyllablesChanged() {
	if (reloading_karaoke) return;

	active_line = c->selectionController->GetActiveLine();
	if (!active_line) {
		Revert();
		return;
	}
	start_marker.Move(active_line->Start);
	end_marker.Move(active_line->End);
	cur_syl = std::min(cur_syl, kara->size() ? kara->size() - 1 : 0);
	if (spectrogram_timing)
		ResetSpectrogramBoundaryStateFromKaraoke();
	else {
		spectrogram_next_boundary = 0;
		spectrogram_boundaries.clear();
	}
	if (spectrogram_timing) {
		pending_changes = true;
		commit_id = -1;
	}
	RebuildMarkersAndLabels();
	AnnounceUpdatedPrimaryRange();
	AnnounceUpdatedStyleRanges();
	AnnounceMarkerMoved();
	AnnounceLabelChanged();
}

void AudioTimingControllerKaraoke::ResetSpectrogramBoundaryStateFromKaraoke() {
	spectrogram_boundaries.clear();

	if (!kara->HasKaraokeTags() && !kara->HasTiming()) {
		spectrogram_next_boundary = 0;
		kara->ClearTiming();
		return;
	}

	for (auto it = kara->begin(); it != kara->end(); ++it) {
		if (it != kara->begin())
			spectrogram_boundaries.push_back(it->start_time);
	}
	spectrogram_next_boundary = spectrogram_boundaries.size();
}

void AudioTimingControllerKaraoke::RebuildMarkersAndLabels() {
	markers.clear();
	labels.clear();

	markers.reserve(kara->size());
	labels.reserve(kara->size());

	size_t idx = 0;
	for (auto it = kara->begin(); it != kara->end(); ++it) {
		if (spectrogram_timing) {
			if (idx > 0 && idx - 1 < spectrogram_boundaries.size())
				markers.emplace_back(spectrogram_boundaries[idx - 1], &separator_pen, AudioMarker::Feet_None);

			int label_start = end_marker.GetPosition();
			int label_end = end_marker.GetPosition();
			if (idx == 0)
				label_start = start_marker.GetPosition();
			else if (idx - 1 < spectrogram_boundaries.size())
				label_start = spectrogram_boundaries[idx - 1];

			if (idx < spectrogram_boundaries.size())
				label_end = spectrogram_boundaries[idx];
			else if (idx == spectrogram_boundaries.size())
				label_end = end_marker.GetPosition();

			wxString label_text = it->text.empty() ? wxString(wxS("rest")) :
				kara->IsWhitespaceSyllable(idx) ? wxString(wxS("space")) : to_wx(it->text);
			labels.push_back(AudioLabel{label_text, TimeRange(label_start, label_end)});
			++idx;
			continue;
		}

		if (it != kara->begin())
			markers.emplace_back(it->start_time, &separator_pen, AudioMarker::Feet_None);
		wxString label_text = it->text.empty() ? wxString(wxS("rest")) :
			kara->IsWhitespaceSyllable(idx) ? wxString(wxS("space")) : to_wx(it->text);
		labels.push_back(AudioLabel{label_text, TimeRange(it->start_time, it->start_time + it->duration)});
		++idx;
	}
}

void AudioTimingControllerKaraoke::UpdateSpectrogramLabelsFromBoundaries() {
	if (!spectrogram_timing) return;

	for (size_t idx = 0; idx < labels.size(); ++idx) {
		int label_start = idx == 0 ? start_marker.GetPosition() :
			idx - 1 < spectrogram_boundaries.size() ? spectrogram_boundaries[idx - 1] : end_marker.GetPosition();
		int label_end = idx < spectrogram_boundaries.size() ? spectrogram_boundaries[idx] : end_marker.GetPosition();
		labels[idx].range = TimeRange(label_start, label_end);
	}
}

void AudioTimingControllerKaraoke::ApplyBoundaryVector(std::vector<int> const& boundaries, int selected_syl) {
	kara->SetTimingBoundaries(start_marker, end_marker, boundaries, false);
	RebuildMarkersAndLabels();
	cur_syl = labels.empty() ? 0 : mid<size_t>(0, static_cast<size_t>(std::max(0, selected_syl)), labels.size() - 1);
	AnnounceChanges(cur_syl);
}

bool AudioTimingControllerKaraoke::AssignSpectrogramBoundary(int ms) {
	size_t syl_count = kara->size();
	if (syl_count == 0) return false;

	int end_time = end_marker.GetPosition();
	int position = (ms + 5) / 10 * 10;

	if (spectrogram_next_boundary >= syl_count - 1)
		return false;

	int prev = spectrogram_next_boundary == 0 ? start_marker.GetPosition() : spectrogram_boundaries[spectrogram_next_boundary - 1];
	int min_pos = prev;
	int max_pos = end_time;
	if (max_pos < min_pos)
		return false;

	position = mid(min_pos, position, max_pos);
	if (spectrogram_next_boundary == spectrogram_boundaries.size())
		spectrogram_boundaries.push_back(position);
	else
		spectrogram_boundaries[spectrogram_next_boundary] = position;
	++spectrogram_next_boundary;

	ApplyBoundaryVector(spectrogram_boundaries, spectrogram_next_boundary);
	return true;
}

int AudioTimingControllerKaraoke::FindNearbyMarker(int ms, int sensitivity) const {
	int best = -1;
	int best_distance = sensitivity + 1;
	for (size_t i = 0; i < markers.size(); ++i) {
		int distance = std::abs(markers[i].GetPosition() - ms);
		if (distance <= sensitivity && distance < best_distance) {
			best = static_cast<int>(i);
			best_distance = distance;
		}
	}
	return best;
}

bool AudioTimingControllerKaraoke::ShouldNoOpSingleSlotCommit() const {
	return spectrogram_timing &&
		active_line &&
		!kara->HasKaraokeTags() &&
		kara->size() == 1 &&
		!kara->IsEmptySyllable(0) &&
		!kara->IsWhitespaceSyllable(0);
}

void AudioTimingControllerKaraoke::AddLeadIn() {
	start_marker.Move(start_marker - OPT_GET("Audio/Lead/IN")->GetInt());
	labels.front().range = TimeRange(start_marker, labels.front().range.end());
	ApplyLead(cur_syl == 0);
}

void AudioTimingControllerKaraoke::AddLeadOut() {
	end_marker.Move(end_marker + OPT_GET("Audio/Lead/OUT")->GetInt());
	labels.back().range = TimeRange(labels.back().range.begin(), end_marker);
	ApplyLead(cur_syl == markers.size());
}

void AudioTimingControllerKaraoke::ApplyLead(bool announce_primary) {
	active_line->Start = (int)start_marker;
	active_line->End = (int)end_marker;
	kara->SetLineTimes(start_marker, end_marker);
	if (!announce_primary)
		AnnounceUpdatedStyleRanges();
	AnnounceChanges(announce_primary ? cur_syl : cur_syl + 2);
}

void AudioTimingControllerKaraoke::ModifyLength(int delta, bool shift_following) {
	if (cur_syl == markers.size()) return;

	int cur, end, step;
	if (delta < 0) {
		cur = cur_syl;
		end = shift_following ? markers.size() : cur_syl + 1;
		step = 1;
	}
	else {
		cur = shift_following ? markers.size() - 1 : cur_syl;
		end = cur_syl - 1;
		step = -1;
	}

	for (; cur != end; cur += step) {
		MoveMarker(&markers[cur], markers[cur] + delta * 10);
	}
	AnnounceChanges(cur_syl);
}

void AudioTimingControllerKaraoke::ModifyStart(int delta) {
	if (cur_syl == 0) return;
	MoveMarker(&markers[cur_syl - 1], markers[cur_syl - 1] + delta * 10);
	AnnounceChanges(cur_syl);
}

bool AudioTimingControllerKaraoke::IsNearbyMarker(int ms, int sensitivity, bool) const {
	if (spectrogram_timing)
		return FindNearbyMarker(ms, sensitivity) >= 0;

	TimeRange range(ms - sensitivity, ms + sensitivity);
	return any_of(markers.begin(), markers.end(), [&](KaraokeMarker const& km) {
		return range.contains(km);
	});
}

template<typename Out, typename In>
static std::vector<Out *> copy_ptrs(In &vec, size_t start, size_t end) {
	std::vector<Out *> ret;
	ret.reserve(end - start);
	for (; start < end; ++start)
		ret.push_back(&vec[start]);
	return ret;
}

std::vector<AudioMarker*> AudioTimingControllerKaraoke::OnLeftClick(int ms, bool ctrl_down, bool, int sensitivity, int) {
	TimeRange range(ms - sensitivity, ms + sensitivity);

	if (spectrogram_timing) {
		int marker_idx = FindNearbyMarker(ms, sensitivity);
		if (marker_idx >= 0) {
			cur_syl = static_cast<size_t>(marker_idx) + 1;
			AnnounceUpdatedPrimaryRange();
			AnnounceUpdatedStyleRanges();
			return copy_ptrs<AudioMarker>(markers, marker_idx, marker_idx + 1);
		}

		if (AssignSpectrogramBoundary(ms))
			return {};
	}

	size_t syl = distance(markers.begin(), lower_bound(markers.begin(), markers.end(), ms));
	if (syl < markers.size() && range.contains(markers[syl])) {
		return copy_ptrs<AudioMarker>(markers, syl, ctrl_down ? markers.size() : syl + 1);
	}
	if (syl > 0 && range.contains(markers[syl - 1])) {
		return copy_ptrs<AudioMarker>(markers, syl - 1, ctrl_down ? markers.size() : syl);
	}

	cur_syl = syl;

	AnnounceUpdatedPrimaryRange();
	AnnounceUpdatedStyleRanges();

	return {};
}

std::vector<AudioMarker*> AudioTimingControllerKaraoke::OnRightClick(int ms, bool, int, int) {
	cur_syl = distance(markers.begin(), lower_bound(markers.begin(), markers.end(), ms));

	AnnounceUpdatedPrimaryRange();
	AnnounceUpdatedStyleRanges();
	c->audioController->PlayPrimaryRange();

	return {};
}

int AudioTimingControllerKaraoke::MoveMarker(KaraokeMarker *marker, int new_position) {
	if (spectrogram_timing) {
		if (markers.empty()) return -1;

		size_t marker_idx = marker - &markers.front();
		if (marker_idx >= markers.size() || marker_idx >= spectrogram_boundaries.size())
			return -1;

		new_position = (new_position + 5) / 10 * 10;
		new_position = mid(
			marker_idx == 0 ? start_marker.GetPosition() : markers[marker_idx - 1].GetPosition(),
			new_position,
			marker_idx + 1 == markers.size() ? end_marker.GetPosition() : markers[marker_idx + 1].GetPosition());

		if (new_position == marker->GetPosition())
			return -1;

		marker->Move(new_position);
		spectrogram_boundaries[marker_idx] = new_position;
		spectrogram_next_boundary = std::max(spectrogram_next_boundary, marker_idx + 1);
		kara->SetTimingBoundaries(start_marker, end_marker, spectrogram_boundaries, false);
		UpdateSpectrogramLabelsFromBoundaries();

		return static_cast<int>(marker_idx + 1);
	}

	// No rearranging of syllables allowed
	new_position = mid(
		marker == &markers.front() ? start_marker.GetPosition() : (marker - 1)->GetPosition(),
		new_position,
		marker == &markers.back() ? end_marker.GetPosition() : (marker + 1)->GetPosition());

	if (new_position == marker->GetPosition())
		return -1;

	size_t marker_idx = marker - &markers.front();
	marker->Move(new_position);

	size_t syl = marker_idx + 1;
	kara->SetStartTime(syl, (new_position + 5) / 10 * 10);

	labels[syl - 1].range = TimeRange(labels[syl - 1].range.begin(), new_position);
	labels[syl].range = TimeRange(new_position, labels[syl].range.end());

	return syl;
}

void AudioTimingControllerKaraoke::AnnounceChanges(int syl) {
	if (syl < 0) return;

	if (syl == cur_syl || syl == cur_syl + 1) {
		AnnounceUpdatedPrimaryRange();
		AnnounceUpdatedStyleRanges();
	}
	AnnounceMarkerMoved();
	AnnounceLabelChanged();

	if (auto_commit && !spectrogram_timing)
		DoCommit();
	else {
		pending_changes = true;
		commit_id = -1;
	}
}

void AudioTimingControllerKaraoke::OnMarkerDrag(std::vector<AudioMarker*> const& m, int new_position, int) {
	if (m.empty()) return;

	int old_position = m[0]->GetPosition();
	int syl = MoveMarker(static_cast<KaraokeMarker *>(m[0]), new_position);
	if (syl < 0) return;

	if (m.size() > 1) {
		int delta = m[0]->GetPosition() - old_position;
		for (AudioMarker *marker : m | boost::adaptors::sliced(1, m.size()))
			MoveMarker(static_cast<KaraokeMarker *>(marker), marker->GetPosition() + delta);
		syl = cur_syl;
	}

	AnnounceChanges(syl);
}

void AudioTimingControllerKaraoke::SetSpectrogramKaraokeTiming(bool enabled) {
	if (spectrogram_timing == enabled) return;

	spectrogram_timing = enabled;
	spectrogram_next_boundary = 0;
	spectrogram_boundaries.clear();

	if (spectrogram_timing && active_line)
		Revert();

	AnnounceUpdatedPrimaryRange();
	AnnounceUpdatedStyleRanges();
	AnnounceMarkerMoved();
}

void AudioTimingControllerKaraoke::GetLabels(TimeRange const& range, std::vector<AudioLabel> &out) const {
	copy(labels | boost::adaptors::filtered([&](AudioLabel const& l) {
		return range.overlaps(l.range);
	}), back_inserter(out));
}
