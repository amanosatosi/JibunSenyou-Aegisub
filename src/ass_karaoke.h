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

#include <map>
#include <string>
#include <vector>

#include <libaegisub/signal.h>

namespace agi { struct Context; }
class AssDialogue;

/// @class AssKaraoke
/// @brief Karaoke parser and parsed karaoke data model
class AssKaraoke {
public:
	// [Satoshi preserve timings on cut] Structured per-syllable karaoke tag type
	enum class TagType {
		k,
		kf,
		ko
	};

	/// Parsed syllable data
	struct Syllable {
		int start_time; ///< Start time relative to time zero (not line start) in milliseconds
		int duration;   ///< Duration in milliseconds
		std::string text; ///< Stripped syllable text
		std::string tag_type; ///< \k, \kf or \ko
		/// Non-karaoke override tags in this syllable. Key is an index in text
		/// before which the value should be inserted
		std::map<size_t, std::string> ovr_tags;

		/// Get the text of this line with override tags and optionally the karaoke tag
		std::string GetText(bool k_tag) const;

		// [Satoshi preserve timings on cut] Convenience accessors for a compact model
		int GetDurationCs() const { return (duration + 5) / 10; }
		TagType GetTagType() const {
			if (tag_type == "\\kf") return TagType::kf;
			if (tag_type == "\\ko") return TagType::ko;
			return TagType::k;
		}
	};
private:
	std::vector<Syllable> syls;
	int line_start_time = 0;
	int line_end_time = 0;
	bool has_karaoke_tags = false;

	bool no_announce = false;

	agi::signal::Signal<> AnnounceSyllablesChanged;
	void ParseSyllables(const AssDialogue *line, Syllable &syl);

public:
	/// Constructor
	/// @param line Initial line
	/// @param auto_split Should the line automatically be split on spaces if there are no k tags?
	/// @param normalize Should the total duration of the syllables be forced to equal the line duration?
	AssKaraoke(const AssDialogue *line = nullptr, bool auto_split = false, bool normalize = true);

	/// Parse a dialogue line
	void SetLine(const AssDialogue *line, bool auto_split = false, bool normalize = true);

	/// Add a split before character pos in syllable syl_idx
	void AddSplit(size_t syl_idx, size_t pos);
	/// Add a split before character pos in syllable syl_idx, preserving existing timings
	/// [Satoshi preserve timings on cut]
	void AddSplitPreserveTimes(size_t syl_idx, size_t pos);
	/// Add a Toshiki K-Timing split while preserving existing timing and giving the new slot 0 duration
	void AddSplitKTiming(size_t syl_idx, size_t pos);
	/// Remove the split at the given index
	void RemoveSplit(size_t syl_idx);
	/// Insert an empty rest syllable before the given syllable index
	void InsertEmptySyllable(size_t syl_idx);
	/// Append an empty rest syllable to the end of the line
	void AppendEmptySyllable(bool announce = true);
	/// Remove an empty rest syllable
	void RemoveEmptySyllable(size_t syl_idx);
	/// Is the given syllable an empty rest?
	bool IsEmptySyllable(size_t syl_idx) const;
	/// Is the given syllable a literal whitespace slot?
	bool IsWhitespaceSyllable(size_t syl_idx) const;
	/// Did the parsed line contain explicit karaoke timing tags?
	bool HasKaraokeTags() const { return has_karaoke_tags; }
	/// Does the current model have any assigned timing?
	bool HasTiming() const;
	/// Rebuild syllable timings from ordered boundary positions
	void SetTimingBoundaries(int start_time, int end_time, std::vector<int> const& boundaries, bool announce = true);
	/// Clear all syllable timing while preserving the current slot text/order
	void ClearTiming();
	/// Recut the current text into Japanese kana timing slots
	void AutoSplitJapaneseKana(bool distribute_timings = true, bool spaces_as_slots = false, bool song_sane = false);
	/// Recut the current text into space-separated word timing slots
	void AutoSplitWords(bool distribute_timings = true);
	/// Does the current stripped text contain kana or CJK ideographs?
	bool ContainsJapaneseText() const;
	/// Set the start time of a syllable in ms
	void SetStartTime(size_t syl_idx, int time);
	/// Adjust the line's start and end times without shifting the syllables
	void SetLineTimes(int start_time, int end_time);

	typedef std::vector<Syllable>::const_iterator iterator;

	iterator begin() const { return syls.begin(); }
	iterator end() const { return syls.end(); }
	size_t size() const { return syls.size(); }

	/// Get the line's text, optionally with karaoke tags
	std::string GetText(bool k_tags = true) const;

	/// Get the karaoke tag type used, with leading slash
	/// @returns "\k", "\kf", or "\ko"
	std::string GetTagType() const;
	/// Set the tag type for all karaoke tags in this line
	void SetTagType(std::string const& new_type, bool announce = true);

	DEFINE_SIGNAL_ADDERS(AnnounceSyllablesChanged, AddSyllablesChangedListener)
};
