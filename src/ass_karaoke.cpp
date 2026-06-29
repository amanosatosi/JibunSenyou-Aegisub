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

#include "ass_karaoke.h"

#include "ass_dialogue.h"

#include <libaegisub/format.h>
#include <libaegisub/karaoke_split.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/locale/boundary.hpp>
#include <algorithm>
#include <cctype>

namespace {
uint32_t utf8_codepoint(std::string const& chr) {
	const unsigned char *s = reinterpret_cast<const unsigned char *>(chr.data());
	if (chr.empty()) return 0;
	if (s[0] < 0x80) return s[0];
	if ((s[0] & 0xE0) == 0xC0 && chr.size() >= 2)
		return ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
	if ((s[0] & 0xF0) == 0xE0 && chr.size() >= 3)
		return ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
	if ((s[0] & 0xF8) == 0xF0 && chr.size() >= 4)
		return ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
	return 0;
}

bool is_hiragana(uint32_t c) { return c >= 0x3040 && c <= 0x309F; }
bool is_katakana(uint32_t c) { return c >= 0x30A0 && c <= 0x30FF; }
bool is_katakana_phonetic_extension(uint32_t c) { return c >= 0x31F0 && c <= 0x31FF; }
bool is_halfwidth_katakana(uint32_t c) { return c >= 0xFF66 && c <= 0xFF9F; }
bool is_kana(uint32_t c) { return is_hiragana(c) || is_katakana(c) || is_katakana_phonetic_extension(c) || is_halfwidth_katakana(c); }
bool is_combining_voicing_mark(uint32_t c) { return c == 0x3099 || c == 0x309A; }
bool is_long_vowel_mark(uint32_t c) { return c == 0x30FC || c == 0xFF70; }
bool is_small_tsu(uint32_t c) { return c == 0x3063 || c == 0x30C3 || c == 0xFF6F; }

bool is_small_kana_modifier(uint32_t c) {
	switch (c) {
	case 0x3041: case 0x3043: case 0x3045: case 0x3047: case 0x3049:
	case 0x3083: case 0x3085: case 0x3087: case 0x308E:
	case 0x30A1: case 0x30A3: case 0x30A5: case 0x30A7: case 0x30A9:
	case 0x30E3: case 0x30E5: case 0x30E7: case 0x30EE:
	case 0xFF67: case 0xFF68: case 0xFF69: case 0xFF6A: case 0xFF6B:
	case 0xFF6C: case 0xFF6D: case 0xFF6E:
		return true;
	default:
		return false;
	}
}

bool is_space_cp(uint32_t c) {
	return c == ' ' || c == '\t' || c == 0x3000;
}

bool is_repeat_marker(uint32_t c) {
	return c == '#' || c == 0xFF03;
}

bool is_cjk_ideograph(uint32_t c) {
	return (c >= 0x3400 && c <= 0x4DBF) ||
	       (c >= 0x4E00 && c <= 0x9FFF) ||
	       (c >= 0xF900 && c <= 0xFAFF) ||
	       (c >= 0x20000 && c <= 0x2EBEF);
}

bool is_japanese_text_cp(uint32_t c) {
	return is_kana(c) || is_cjk_ideograph(c);
}

bool is_roman_text(uint32_t c) {
	return c < 0x80 && std::isalnum(static_cast<unsigned char>(c));
}

bool is_ascii_punctuation(uint32_t c) {
	return c < 0x80 && !std::isalnum(static_cast<unsigned char>(c)) && !is_space_cp(c);
}

bool is_japanese_punctuation(uint32_t c) {
	return (c >= 0x3001 && c <= 0x303F) || c == 0xFF01 || c == 0xFF0C || c == 0xFF0E || c == 0xFF1F;
}

bool is_punctuation(uint32_t c) {
	return is_ascii_punctuation(c) || is_japanese_punctuation(c);
}

bool is_kana_unit_starter(uint32_t c) {
	return is_small_tsu(c) || is_long_vowel_mark(c) || is_kana(c);
}

bool is_fallback_run(uint32_t c) {
	return is_roman_text(c);
}

uint32_t last_codepoint(std::string const& text) {
	uint32_t last = 0;
	using namespace boost::locale::boundary;
	const ssegment_index characters(character, text.begin(), text.end());
	for (auto chr : characters)
		last = utf8_codepoint(chr.str());
	return last;
}

bool is_hiragana_o_row(uint32_t c) {
	switch (c) {
	case 0x3053: case 0x3054: case 0x305D: case 0x305E: case 0x3068: case 0x3069:
	case 0x306E: case 0x307B: case 0x307C: case 0x307D: case 0x3082: case 0x3088:
	case 0x3087: case 0x308D: case 0x3092:
		return true;
	default:
		return false;
	}
}

bool is_katakana_o_row(uint32_t c) {
	switch (c) {
	case 0x30B3: case 0x30B4: case 0x30BD: case 0x30BE: case 0x30C8: case 0x30C9:
	case 0x30CE: case 0x30DB: case 0x30DC: case 0x30DD: case 0x30E2: case 0x30E8:
	case 0x30E7: case 0x30ED: case 0x30F2:
		return true;
	default:
		return false;
	}
}

bool attaches_in_song_sane_mode(std::string const& previous, uint32_t current) {
	uint32_t last = last_codepoint(previous);
	return is_small_tsu(current) ||
	       is_long_vowel_mark(current) ||
	       (current == 0x3046 && (is_hiragana_o_row(last) || is_small_kana_modifier(last))) ||
	       (current == 0x30A6 && (is_katakana_o_row(last) || is_small_kana_modifier(last)));
}
} // namespace

std::string AssKaraoke::Syllable::GetText(bool k_tag) const {
	std::string ret;

	if (k_tag)
		ret = agi::format("{%s%d}", tag_type, ((duration + 5) / 10));

	size_t idx = 0;
	for (auto const& ovr : ovr_tags) {
		ret += text.substr(idx, ovr.first - idx);
		ret += ovr.second;
		idx = ovr.first;
	}
	ret += text.substr(idx);
	return ret;
}

AssKaraoke::AssKaraoke(const AssDialogue *line, bool auto_split, bool normalize) {
	if (line) SetLine(line, auto_split, normalize);
}

void AssKaraoke::SetLine(const AssDialogue *line, bool auto_split, bool normalize) {
	syls.clear();
	has_karaoke_tags = false;
	line_start_time = line->Start;
	line_end_time = line->End;
	Syllable syl;
	syl.start_time = line->Start;
	syl.duration = 0;
	syl.tag_type = "\\k";

	ParseSyllables(line, syl);

	if (normalize) {
		// Normalize the syllables so that the total duration is equal to the line length
		int end_time = line->End;
		int last_end = syl.start_time + syl.duration;

		// Total duration is shorter than the line length so just extend the last
		// syllable; this has no effect on rendering but is easier to work with
		if (last_end < end_time)
			syls.back().duration += end_time - last_end;
		else if (last_end > end_time) {
			// Truncate any syllables that extend past the end of the line
			for (auto& syl : syls) {
				if (syl.start_time > end_time) {
					syl.start_time = end_time;
					syl.duration = 0;
				}
				else {
					syl.duration = std::min(syl.duration, end_time - syl.start_time);
				}
			}
		}
	}

	// Add karaoke splits at each space
	if (auto_split && syls.size() == 1) {
		size_t pos;
		no_announce = true;
		while ((pos = syls.back().text.find(' ')) != std::string::npos)
			AddSplit(syls.size() - 1, pos + 1);
		no_announce = false;
	}

	AnnounceSyllablesChanged();
}

void AssKaraoke::ParseSyllables(const AssDialogue *line, Syllable &syl) {
	for (auto& block : line->ParseTags()) {
		std::string text = block->GetText();

		switch (block->GetType()) {
		case AssBlockType::PLAIN:
			syl.text += text;
			break;
		case AssBlockType::COMMENT:
		// drawings aren't override tags but they shouldn't show up in the
		// stripped text so pretend they are
		case AssBlockType::DRAWING:
			syl.ovr_tags[syl.text.size()] += text;
			break;
		case AssBlockType::OVERRIDE:
			auto ovr = static_cast<AssDialogueBlockOverride*>(block.get());
			bool in_tag = false;
			for (auto& tag : ovr->Tags) {
				if (tag.IsValid() && boost::istarts_with(tag.Name, "\\k")) {
					has_karaoke_tags = true;
					if (in_tag) {
						syl.ovr_tags[syl.text.size()] += "}";
						in_tag = false;
					}

					// Dealing with both \K and \kf is mildly annoying so just
					// convert them both to \kf
					if (tag.Name == "\\K") tag.Name = "\\kf";

					// Don't bother including zero duration zero length syls
					if (syl.duration > 0 || !syl.text.empty()) {
						syls.push_back(syl);
						syl.text.clear();
						syl.ovr_tags.clear();
					}

					syl.tag_type = tag.Name;
					syl.start_time += syl.duration;
					syl.duration = tag.Params[0].Get(0) * 10;
				}
				else {
					std::string& otext = syl.ovr_tags[syl.text.size()];
					// Merge adjacent override tags
					boost::trim_right_if(text, [](char c) { return c == '}'; });
					if (!in_tag)
						otext += "{";

					in_tag = true;
					otext += tag;
				}
			}

			if (in_tag)
				syl.ovr_tags[syl.text.size()] += "}";
			break;
		}
	}

	syls.push_back(syl);
}

std::string AssKaraoke::GetText() const {
	std::string text;
	text.reserve(size() * 10);

	for (auto const& syl : syls)
		text += syl.GetText(true);

	return text;
}

std::string AssKaraoke::GetTagType() const {
	return begin()->tag_type;
}

void AssKaraoke::SetTagType(std::string const& new_type) {
	for (auto& syl : syls)
		syl.tag_type = new_type;
}

void AssKaraoke::AddSplit(size_t syl_idx, size_t pos) {
	syls.insert(syls.begin() + syl_idx + 1, Syllable());
	Syllable &syl = syls[syl_idx];
	Syllable &new_syl = syls[syl_idx + 1];

	// If the syl is empty or the user is adding a syllable past the last
	// character then pos will be out of bounds. Doing this is a bit goofy,
	// but it's sometimes required for complex karaoke scripts
	if (pos < syl.text.size()) {
		new_syl.text = syl.text.substr(pos);
		syl.text = syl.text.substr(0, pos);
	}

	if (new_syl.text.empty())
		new_syl.duration = 0;
	else if (syl.text.empty()) {
		new_syl.duration = syl.duration;
		syl.duration = 0;
	}
	else {
		new_syl.duration = (syl.duration * new_syl.text.size() / (syl.text.size() + new_syl.text.size()) + 5) / 10 * 10;
		syl.duration -= new_syl.duration;
	}

	assert(syl.duration >= 0);

	new_syl.start_time = syl.start_time + syl.duration;
	new_syl.tag_type = syl.tag_type;

	// Move all override tags after the split to the new syllable and fix the indices
	size_t text_len = syl.text.size();
	for (auto it = syl.ovr_tags.begin(); it != syl.ovr_tags.end(); ) {
		if (it->first < text_len)
			++it;
		else {
			new_syl.ovr_tags[it->first - text_len] = it->second;
			syl.ovr_tags.erase(it++);
		}
	}

	if (!no_announce) AnnounceSyllablesChanged();
}

void AssKaraoke::AddSplitPreserveTimes(size_t syl_idx, size_t pos) {
	// [Satoshi preserve timings on cut]
	// Split a syllable into left/right at `pos` while preserving the original
	// syllable timing in centiseconds:
	//   D1 = round(D * len(left) / len(total))
	//   D2 = D - D1
	// This keeps the total duration identical and only affects the split syllable.
	if (syl_idx >= syls.size()) return;

	// pos has the same semantics as AddSplit: a 0-based byte offset into the
	// syllable text where the new syllable begins (i.e. the split point).
	const auto& orig = syls[syl_idx].text;
	const size_t split_at = std::min(pos, orig.size());

	// Calculate the split position as a Unicode character boundary.
	// This avoids crashing on invalid UTF-8 and keeps the split aligned to the
	// same character segmentation used by the karaoke split UI.
	size_t total_chars = 0;
	size_t left_chars = 0;
	bool split_at_boundary = (split_at == 0 || split_at == orig.size());
	using namespace boost::locale::boundary;
	const ssegment_index characters(character, orig.begin(), orig.end());
	for (auto chr : characters) {
		if (!split_at_boundary && static_cast<size_t>(chr.begin() - orig.begin()) == split_at) {
			left_chars = total_chars;
			split_at_boundary = true;
		}
		++total_chars;
	}

	if (split_at == orig.size())
		left_chars = total_chars;

	if (!split_at_boundary) return;

	int total_cs = (syls[syl_idx].duration + 5) / 10;
	syls.insert(syls.begin() + syl_idx + 1, Syllable());
	Syllable &syl = syls[syl_idx];
	Syllable &new_syl = syls[syl_idx + 1];

	// Split text
	if (split_at < syl.text.size()) {
		new_syl.text = syl.text.substr(split_at);
		syl.text = syl.text.substr(0, split_at);
	}

	// Split duration proportionally by character count (not bytes).
	// If the syllable has no characters, preserve existing timing and just add
	// another zero-duration empty syllable (legacy behavior).
	if (total_chars == 0) {
		new_syl.duration = 0;
	}
	else {
		auto split = agi::SplitKaraokeDurationCs(total_cs, left_chars, total_chars);
		syl.duration = split.first * 10;
		new_syl.duration = split.second * 10;
	}

	new_syl.start_time = syl.start_time + syl.duration;
	new_syl.tag_type = syl.tag_type;

	// Move all override tags after the split to the new syllable and fix the indices
	size_t text_len = syl.text.size();
	for (auto it = syl.ovr_tags.begin(); it != syl.ovr_tags.end(); ) {
		if (it->first < text_len)
			++it;
		else {
			new_syl.ovr_tags[it->first - text_len] = it->second;
			syl.ovr_tags.erase(it++);
		}
	}

	if (!no_announce) AnnounceSyllablesChanged();
}

void AssKaraoke::AddSplitKTiming(size_t syl_idx, size_t pos) {
	if (syl_idx >= syls.size()) return;

	const auto& orig = syls[syl_idx].text;
	const size_t split_at = std::min(pos, orig.size());

	bool split_at_boundary = (split_at == 0 || split_at == orig.size());
	using namespace boost::locale::boundary;
	const ssegment_index characters(character, orig.begin(), orig.end());
	for (auto chr : characters) {
		if (static_cast<size_t>(chr.begin() - orig.begin()) == split_at) {
			split_at_boundary = true;
			break;
		}
	}
	if (!split_at_boundary) return;

	syls.insert(syls.begin() + syl_idx + 1, Syllable());
	Syllable &syl = syls[syl_idx];
	Syllable &new_syl = syls[syl_idx + 1];

	if (split_at < syl.text.size()) {
		new_syl.text = syl.text.substr(split_at);
		syl.text = syl.text.substr(0, split_at);
	}

	new_syl.duration = 0;
	new_syl.start_time = syl.start_time + syl.duration;
	new_syl.tag_type = syl.tag_type;

	size_t text_len = syl.text.size();
	for (auto it = syl.ovr_tags.begin(); it != syl.ovr_tags.end(); ) {
		if (it->first < text_len)
			++it;
		else {
			new_syl.ovr_tags[it->first - text_len] = it->second;
			syl.ovr_tags.erase(it++);
		}
	}

	if (!no_announce) AnnounceSyllablesChanged();
}

void AssKaraoke::RemoveSplit(size_t syl_idx) {
	// Don't allow removing the first syllable
	if (syl_idx == 0) return;

	Syllable &syl = syls[syl_idx];
	Syllable &prev = syls[syl_idx - 1];

	prev.duration += syl.duration;
	for (auto const& tag : syl.ovr_tags)
		prev.ovr_tags[tag.first + prev.text.size()] = tag.second;
	prev.text += syl.text;

	syls.erase(syls.begin() + syl_idx);

	if (!no_announce) AnnounceSyllablesChanged();
}

void AssKaraoke::InsertEmptySyllable(size_t syl_idx) {
	if (syls.empty()) return;

	syl_idx = std::min(syl_idx, syls.size());
	Syllable rest;
	if (syl_idx > 0) {
		auto const& prev = syls[syl_idx - 1];
		rest.start_time = prev.start_time + prev.duration;
		rest.tag_type = prev.tag_type;
	}
	else {
		rest.start_time = syls.front().start_time;
		rest.tag_type = syls.front().tag_type;
	}
	rest.duration = 0;

	syls.insert(syls.begin() + syl_idx, rest);

	if (!no_announce) AnnounceSyllablesChanged();
}

void AssKaraoke::AppendEmptySyllable(bool announce) {
	if (syls.empty()) return;

	Syllable rest;
	auto const& prev = syls.back();
	rest.start_time = prev.start_time + prev.duration;
	rest.duration = 0;
	rest.tag_type = prev.tag_type;
	syls.push_back(rest);

	if (announce && !no_announce) AnnounceSyllablesChanged();
}

void AssKaraoke::RemoveEmptySyllable(size_t syl_idx) {
	if (!IsEmptySyllable(syl_idx) || syls.size() <= 1) return;

	if (syl_idx == 0) {
		syls[1].start_time = syls[0].start_time;
		syls[1].duration += syls[0].duration;
	}
	else
		syls[syl_idx - 1].duration += syls[syl_idx].duration;

	syls.erase(syls.begin() + syl_idx);

	if (!no_announce) AnnounceSyllablesChanged();
}

bool AssKaraoke::IsEmptySyllable(size_t syl_idx) const {
	if (syl_idx >= syls.size()) return false;
	return syls[syl_idx].text.empty() && syls[syl_idx].ovr_tags.empty();
}

bool AssKaraoke::IsWhitespaceSyllable(size_t syl_idx) const {
	if (syl_idx >= syls.size() || syls[syl_idx].text.empty() || !syls[syl_idx].ovr_tags.empty())
		return false;

	using namespace boost::locale::boundary;
	const auto& text = syls[syl_idx].text;
	const ssegment_index characters(character, text.begin(), text.end());
	for (auto chr : characters) {
		if (!is_space_cp(utf8_codepoint(chr.str())))
			return false;
	}
	return true;
}

bool AssKaraoke::HasTiming() const {
	return std::any_of(syls.begin(), syls.end(), [](Syllable const& syl) {
		return syl.duration > 0;
	});
}

void AssKaraoke::SetTimingBoundaries(int start_time, int end_time, std::vector<int> const& boundaries, bool announce) {
	if (syls.empty()) return;

	int prev = start_time;
	for (size_t i = 0; i < syls.size(); ++i) {
		int next = i < boundaries.size() ? boundaries[i] : end_time;
		next = std::max(prev, std::min(next, end_time));
		syls[i].start_time = prev;
		syls[i].duration = next - prev;
		prev = next;
	}

	if (announce && !no_announce) AnnounceSyllablesChanged();
}

void AssKaraoke::ClearTiming() {
	for (auto& syl : syls) {
		syl.start_time = line_start_time;
		syl.duration = 0;
	}
}

void AssKaraoke::AutoSplitJapaneseKana(bool distribute_timings, bool spaces_as_slots, bool song_sane) {
	if (syls.empty()) return;

	std::string source_text;
	std::string tag_type = GetTagType();
	int start_time = line_start_time;
	int end_time = line_end_time;
	for (auto const& syl : syls)
		source_text += syl.text;

	std::vector<std::string> units;
	using namespace boost::locale::boundary;
	const ssegment_index characters(character, source_text.begin(), source_text.end());
	for (auto chr : characters) {
		std::string text = chr.str();
		uint32_t cp = utf8_codepoint(text);

		if (text == "|") {
			if (!units.empty() && units.back() == "|") {
				units.back().clear();
				continue;
			}
			units.push_back(text);
			continue;
		}

		if (song_sane && is_repeat_marker(cp)) {
			units.push_back(text);
			continue;
		}

		if (is_space_cp(cp)) {
			if (spaces_as_slots)
				units.push_back(text);
			else if (units.empty() || units.back().empty())
				units.push_back(text);
			else
				units.back() += text;
			continue;
		}

		if (is_combining_voicing_mark(cp) || is_small_kana_modifier(cp) || is_punctuation(cp)) {
			if (units.empty() || units.back().empty())
				units.push_back(text);
			else
				units.back() += text;
			continue;
		}

		if (is_kana_unit_starter(cp)) {
			if (song_sane && !units.empty() && !units.back().empty() && attaches_in_song_sane_mode(units.back(), cp))
				units.back() += text;
			else
				units.push_back(text);
			continue;
		}

		if (song_sane && is_cjk_ideograph(cp)) {
			units.push_back(text);
			continue;
		}

		if (!is_fallback_run(cp)) {
			if (units.empty() || units.back().empty())
				units.push_back(text);
			else if (song_sane)
				units.back() += text;
			else if (!is_kana(last_codepoint(units.back())) && !is_long_vowel_mark(last_codepoint(units.back())) && !is_small_tsu(last_codepoint(units.back())))
				units.back() += text;
			else
				units.push_back(text);
			continue;
		}

		if (units.empty() || units.back().empty())
			units.push_back(text);
		else if (is_fallback_run(utf8_codepoint(units.back())))
			units.back() += text;
		else
			units.push_back(text);
	}

	units.erase(std::remove(units.begin(), units.end(), "|"), units.end());
	if (units.empty())
		units.push_back("");

	has_karaoke_tags = false;
	syls.clear();
	syls.reserve(units.size());
	for (auto const& unit : units) {
		Syllable syl;
		syl.text = unit;
		syl.tag_type = tag_type;
		syl.start_time = start_time;
		syls.push_back(syl);
	}

	if (distribute_timings) {
		std::vector<int> boundaries;
		boundaries.reserve(syls.size() - 1);
		int duration = std::max(0, end_time - start_time);
		for (size_t i = 1; i < syls.size(); ++i)
			boundaries.push_back((start_time + duration * static_cast<int>(i) / static_cast<int>(syls.size()) + 5) / 10 * 10);

		SetTimingBoundaries(start_time, end_time, boundaries, false);
	}
	else
		ClearTiming();

	if (!no_announce) AnnounceSyllablesChanged();
}

void AssKaraoke::AutoSplitWords(bool distribute_timings) {
	if (syls.empty()) return;

	std::string source_text;
	std::string tag_type = GetTagType();
	int start_time = line_start_time;
	int end_time = line_end_time;
	for (auto const& syl : syls)
		source_text += syl.text;

	std::vector<std::string> units;
	std::string current;
	using namespace boost::locale::boundary;
	const ssegment_index characters(character, source_text.begin(), source_text.end());
	for (auto chr : characters) {
		std::string text = chr.str();
		uint32_t cp = utf8_codepoint(text);

		if (text == "|") {
			if (!current.empty()) {
				units.push_back(current);
				current.clear();
			}
			if (!units.empty() && units.back() == "|") {
				units.back().clear();
				continue;
			}
			units.push_back(text);
			continue;
		}

		if (is_repeat_marker(cp)) {
			if (!current.empty()) {
				units.push_back(current);
				current.clear();
			}
			units.push_back(text);
			continue;
		}

		if (is_space_cp(cp)) {
			if (!current.empty()) {
				units.push_back(current);
				current.clear();
			}
			continue;
		}

		current += text;
	}
	if (!current.empty())
		units.push_back(current);

	units.erase(std::remove(units.begin(), units.end(), "|"), units.end());
	if (units.empty())
		units.push_back("");

	has_karaoke_tags = false;
	syls.clear();
	syls.reserve(units.size());
	for (auto const& unit : units) {
		Syllable syl;
		syl.text = unit;
		syl.tag_type = tag_type;
		syl.start_time = start_time;
		syls.push_back(syl);
	}

	if (distribute_timings) {
		std::vector<int> boundaries;
		boundaries.reserve(syls.size() - 1);
		int duration = std::max(0, end_time - start_time);
		for (size_t i = 1; i < syls.size(); ++i)
			boundaries.push_back((start_time + duration * static_cast<int>(i) / static_cast<int>(syls.size()) + 5) / 10 * 10);

		SetTimingBoundaries(start_time, end_time, boundaries, false);
	}
	else
		ClearTiming();

	if (!no_announce) AnnounceSyllablesChanged();
}

bool AssKaraoke::ContainsJapaneseText() const {
	using namespace boost::locale::boundary;
	for (auto const& syl : syls) {
		const ssegment_index characters(character, syl.text.begin(), syl.text.end());
		for (auto chr : characters) {
			if (is_japanese_text_cp(utf8_codepoint(chr.str())))
				return true;
		}
	}
	return false;
}

void AssKaraoke::SetStartTime(size_t syl_idx, int time) {
	// Don't allow moving the first syllable
	if (syl_idx == 0) return;

	Syllable &syl = syls[syl_idx];
	Syllable &prev = syls[syl_idx - 1];

	assert(time >= prev.start_time);
	assert(time <= syl.start_time + syl.duration);

	int delta = time - syl.start_time;
	syl.start_time = time;
	syl.duration -= delta;
	prev.duration += delta;
}

void AssKaraoke::SetLineTimes(int start_time, int end_time) {
	assert(end_time >= start_time);
	line_start_time = start_time;
	line_end_time = end_time;

	size_t idx = 0;
	// Chop off any portion of syllables starting before the new start_time
	do {
		int delta = start_time - syls[idx].start_time;
		syls[idx].start_time = start_time;
		syls[idx].duration = std::max(0, syls[idx].duration - delta);
	} while (++idx < syls.size() && syls[idx].start_time < start_time);

	// And truncate any syllables ending after the new end_time
	idx = syls.size() - 1;
	while (syls[idx].start_time > end_time) {
		syls[idx].start_time = end_time;
		syls[idx].duration = 0;
		--idx;
	}
	syls[idx].duration = end_time - syls[idx].start_time;
}
