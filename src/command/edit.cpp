// Copyright (c) 2005-2010, Niels Martin Hansen
// Copyright (c) 2005-2010, Rodrigo Braz Monteiro
// Copyright (c) 2010, Amar Takhar
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

#include "command.h"

#include "../ass_dialogue.h"
#include "../ass_file.h"
#include "../ass_karaoke.h"
#include "../ass_style.h"
#include "../compat.h"
#include "../dialog_search_replace.h"
#include "../dialogs.h"
#include "../vcva_tag_gui.h"
#include "../format.h"
#include "../include/aegisub/context.h"
#include "../initial_line_state.h"
#include "../libresrc/libresrc.h"
#include "../options.h"
#include "../project.h"
#include "../selection_controller.h"
#include "../subs_controller.h"
#include "../subs_edit_box.h"
#include "../text_selection_controller.h"
#include "../utils.h"
#include "../video_controller.h"

#include <libaegisub/address_of_adaptor.h>
#include <libaegisub/of_type_adaptor.h>
#include <libaegisub/make_unique.h>

#include <algorithm>
#include <array>
#include <boost/algorithm/string.hpp>
#include <boost/range/algorithm.hpp>
#include <boost/range/adaptor/filtered.hpp>
#include <boost/range/adaptor/indirected.hpp>
#include <boost/range/adaptor/reversed.hpp>
#include <boost/range/adaptor/sliced.hpp>
#include <boost/range/adaptor/transformed.hpp>
#include <boost/regex.hpp>
#include <boost/tokenizer.hpp>

#include <cctype>
#include <functional>
#include <cstring>

#include <optional>

#include <wx/clipbrd.h>
#include <wx/fontdlg.h>
#include <wx/textentry.h>

namespace {
	using namespace boost::adaptors;
	using cmd::Command;
	using ColorPickerInvoker = bool (*)(wxWindow*, agi::Color, bool, std::function<void (agi::Color)>);

int GetChannelFromTags(const char *tag, const char *alt) {
	auto extract = [](const char *name) -> int {
		if (!name) return 0;
		for (const char *ptr = name; *ptr; ++ptr) {
			if (std::isdigit(static_cast<unsigned char>(*ptr)))
				return *ptr - '0';
		}
		return 0;
	};

	int channel = extract(tag);
	if (!channel)
		channel = extract(alt);
	if (!channel && tag && std::strcmp(tag, "\\c") == 0)
		channel = 1;
	return channel ? channel : 1;
}

std::string FormatGradientColors(std::array<agi::Color, 4> const& colors) {
	std::string value("(");
	for (size_t i = 0; i < colors.size(); ++i) {
		if (i) value += ", ";
		value += colors[i].GetAssOverrideFormatted();
	}
	value += ")";
	return value;
}

std::string FormatGradientAlphas(std::array<uint8_t, 4> const& alphas) {
	std::string value("(");
	for (size_t i = 0; i < alphas.size(); ++i) {
		if (i) value += ", ";
		value += agi::format("&H%02X&", (int)alphas[i]);
	}
	value += ")";
	return value;
}

bool IsValidUtf8(const std::string& text) {
	const unsigned char *data = reinterpret_cast<const unsigned char*>(text.data());
	size_t len = text.size();
	size_t i = 0;
	while (i < len) {
		unsigned char byte = data[i];
		size_t extra = 0;
		if ((byte & 0x80) == 0) {
			++i;
			continue;
		}
		else if ((byte & 0xE0) == 0xC0) {
			extra = 1;
			if ((byte & 0xFE) == 0xC0)
				return false;
		}
		else if ((byte & 0xF0) == 0xE0) {
			extra = 2;
		}
		else if ((byte & 0xF8) == 0xF0) {
			extra = 3;
		}
		else {
			return false;
		}

		if (i + extra >= len)
			return false;
		for (size_t j = 1; j <= extra; ++j) {
			if ((data[i + j] & 0xC0) != 0x80)
				return false;
		}
		i += extra + 1;
	}
	return true;
}

struct ScopeInfo {
	bool in_t = false;
	bool in_block = false;
	int scope_start = 0;
	int scope_end = 0;
	int insert_pos = 0;
};

struct OverrideBlockInfo {
	bool in_block = false;
	int start = -1;
	int end = -1;
};

static OverrideBlockInfo FindOverrideBlock(const std::string& text, int pos) {
	OverrideBlockInfo info;
	pos = std::clamp(pos, 0, (int)text.size());
	int open = -1;
	for (int i = pos - 1; i >= 0; --i) {
		if (text[i] == '}')
			break;
		if (text[i] == '{') {
			open = i;
			break;
		}
	}
	if (open == -1) return info;
	int close = -1;
	for (int i = open + 1; i < (int)text.size(); ++i) {
		if (text[i] == '}') {
			close = i;
			break;
		}
	}
	if (close == -1 || close < pos) return info;
	info.in_block = true;
	info.start = open;
	info.end = close;
	return info;
}

struct TransformBounds {
	int tag_start = -1;
	int paren_open = -1;
	int paren_close = -1;
};

static int FindMatchingParen(const std::string& text, int open_pos, int limit) {
	int depth = 0;
	limit = std::clamp(limit, 0, (int)text.size());
	for (int i = open_pos; i < limit; ++i) {
		char ch = text[i];
		if (ch == '(') ++depth;
		else if (ch == ')') {
			--depth;
			if (depth == 0)
				return i;
		}
	}
	return -1;
}

static std::optional<TransformBounds> FindEnclosingTransform(const std::string& text, const OverrideBlockInfo& block, int pos) {
	if (!block.in_block)
		return std::nullopt;
	int clamp_high = block.end - 1;
	if (clamp_high < block.start + 1)
		clamp_high = block.start + 1;
	pos = std::clamp(pos, block.start + 1, clamp_high);
	int depth = 0;
	for (int i = pos - 1; i >= block.start + 1; --i) {
		char ch = text[i];
		if (ch == ')')
			++depth;
		else if (ch == '(') {
			if (depth > 0) {
				--depth;
				continue;
			}
			if (i >= 2 && text[i - 1] == 't' && text[i - 2] == '\\') {
				int close = FindMatchingParen(text, i, block.end);
				if (close != -1 && pos > i && pos <= close)
					return TransformBounds{i - 2, i, close};
			}
		}
		else if (ch == '{')
			break;
	}
	return std::nullopt;
}

static std::pair<int, int> SegmentForPos(const std::string& text, const OverrideBlockInfo& block, int pos) {
	if (!block.in_block) return {pos, pos};
	int seg_start = block.start + 1;
	int seg_end = block.end;
	for (int i = block.start; i < pos - 1 && i >= 0; ++i) {
		if (text[i] == '\\' && text[i + 1] == 'N') {
			seg_start = i + 2;
			break;
		}
	}
	for (int i = pos; i + 1 < block.end; ++i) {
		if (text[i] == '\\' && text[i + 1] == 'N') {
			seg_end = i;
			break;
		}
	}
	return {seg_start, seg_end};
}

static bool IsInsideTParens(const std::string& text, const OverrideBlockInfo& block, int pos) {
	return FindEnclosingTransform(text, block, pos).has_value();
}

static int SmartInsertionPos(const std::string& text, int pos, bool inside_t) {
	OverrideBlockInfo block = FindOverrideBlock(text, pos);
	if (!block.in_block || inside_t)
		return pos;
	for (int i = pos; i + 1 < block.end; ++i) {
		if (text[i] == '\\' && text[i + 1] == 'N')
			return i + 2;
	}
	return pos;
}

static int SnapOutOfToken(const std::string& text, int pos, const OverrideBlockInfo& block) {
	if (!block.in_block) return pos;
	// Generic tag token: find last '\' before pos, compute token end, snap if inside.
	int last_backslash = -1;
	for (int i = pos - 1; i >= block.start + 1; --i) {
		if (text[i] == '\\') {
			last_backslash = i;
			break;
		}
		if (text[i] == '{' || text[i] == '}')
			break;
	}
	if (last_backslash != -1) {
		int name_end = last_backslash + 1;
		while (name_end < block.end && (std::isalnum(static_cast<unsigned char>(text[name_end])) || text[name_end] == '-'))
			++name_end;
		std::string name = text.substr(last_backslash, name_end - last_backslash);
		int token_end = name_end;
		if (name == "\\N" || name == "\\n" || name == "\\h") {
			// Divider tokens have no arguments; keep token_end local so the caret never
			// snaps past the enclosing transform.
			token_end = name_end;
		}
		else if (token_end < block.end && text[token_end] == '(') {
			int depth = 0;
			for (int j = token_end; j < block.end; ++j) {
				if (text[j] == '(') ++depth;
				else if (text[j] == ')') {
					--depth;
					if (depth == 0) {
						token_end = j + 1;
						break;
					}
				}
			}
		}
		else {
			while (token_end < block.end && text[token_end] != '\\' && text[token_end] != '}')
				++token_end;
		}
		if (pos > last_backslash && pos < token_end)
			return token_end;
	}

	// hex
	for (int i = pos - 1; i >= block.start + 1; --i) {
		if (text[i] == '&' && i + 1 < (int)text.size() && (text[i + 1] == 'H' || text[i + 1] == 'h')) {
			for (int j = i + 2; j < block.end; ++j) {
				if (text[j] == '&') {
					if (pos > i && pos < j + 1)
						return j + 1;
					break;
				}
			}
			break;
		}
		if (text[i] == '\\' || text[i] == '{' || text[i] == '}')
			break;
	}
	// numeric/decimal
	if (pos > block.start && (std::isdigit(static_cast<unsigned char>(text[pos - 1])) || text[pos - 1] == '.')) {
		int end = pos;
		while (end < block.end && (std::isdigit(static_cast<unsigned char>(text[end])) || text[end] == '.'))
			++end;
		return end;
	}
	return pos;
}

static int FindFirstTopLevelDividerN(const std::string& text, int start, int end) {
	start = std::max(0, start);
	end = std::clamp(end, start, (int)text.size());
	int depth = 0;
	for (int i = start; i + 1 < end; ++i) {
		char ch = text[i];
		if (ch == '(') ++depth;
		else if (ch == ')') --depth;
		if (depth == 0 && ch == '\\' && text[i + 1] == 'N')
			return i;
	}
	return -1;
}

static int FindPrevTopLevelDividerN(const std::string& text, int start, int end, int pos) {
	start = std::max(0, start);
	end = std::clamp(end, start, (int)text.size());
	pos = std::clamp(pos, start, end);
	int depth = 0;
	for (int i = pos - 1; i >= start; --i) {
		char ch = text[i];
		if (ch == ')') ++depth;
		else if (ch == '(') --depth;
		if (depth == 0 && ch == '\\' && i + 1 < end && text[i + 1] == 'N')
			return i;
	}
	return -1;
}

static int FindNextTopLevelDividerN(const std::string& text, int start, int end, int pos) {
	start = std::max(0, start);
	end = std::clamp(end, start, (int)text.size());
	pos = std::clamp(pos, start, end);
	int depth = 0;
	for (int i = pos; i + 1 < end; ++i) {
		char ch = text[i];
		if (ch == '(') ++depth;
		else if (ch == ')') --depth;
		if (depth == 0 && ch == '\\' && text[i + 1] == 'N')
			return i;
	}
	return -1;
}

static std::vector<int> FindTopLevelCommas(const std::string& text, int start, int end) {
	std::vector<int> commas;
	int depth = 0;
	for (int i = start; i < end; ++i) {
		char ch = text[i];
		if (ch == '(') ++depth;
		else if (ch == ')') --depth;
		else if (depth == 0 && ch == ',')
			commas.push_back(i);
	}
	return commas;
}

static ScopeInfo ComputeScope(const std::string& text, int caret_raw) {
	ScopeInfo info;
	OverrideBlockInfo block = FindOverrideBlock(text, caret_raw);
	if (!block.in_block) {
		info.scope_start = info.scope_end = info.insert_pos = caret_raw;
		return info;
	}
	info.in_block = true;

	int clamp_high = block.end - 1;
	if (clamp_high < block.start + 1)
		clamp_high = block.start + 1;
	int caret_for_transform = std::clamp(caret_raw, block.start + 1, clamp_high);
	auto transform_bounds = FindEnclosingTransform(text, block, caret_for_transform);

	int anchor = SnapOutOfToken(text, caret_for_transform, block);

	if (transform_bounds) {
		int t_open = transform_bounds->paren_open;
		int t_close = transform_bounds->paren_close;
		info.in_t = true;
		auto commas = FindTopLevelCommas(text, t_open + 1, t_close);
		int taglist_start = t_open + 1;
		if (commas.size() >= 2)
			taglist_start = commas[1] + 1;
		if (commas.size() >= 3)
			taglist_start = commas[2] + 1;
		while (taglist_start < t_close && std::isspace(static_cast<unsigned char>(text[taglist_start])))
			++taglist_start;
		int anchor_clamped = std::clamp(anchor, taglist_start, t_close);
		int prev_div = FindPrevTopLevelDividerN(text, taglist_start, t_close, anchor_clamped);
		int next_div = FindNextTopLevelDividerN(text, taglist_start, t_close, anchor_clamped);
		int seg_start = prev_div != -1 ? prev_div + 2 : taglist_start;
		int seg_end = next_div != -1 ? next_div : t_close;
		seg_start = std::clamp(seg_start, taglist_start, t_close);
		seg_end = std::clamp(seg_end, seg_start, t_close);
		info.scope_start = seg_start;
		info.scope_end = seg_end;
		info.insert_pos = seg_end;
		return info;
	}

	auto seg = SegmentForPos(text, block, anchor);
	info.scope_start = seg.first;
	info.scope_end = seg.second;
	int insert_pos = std::clamp(anchor, info.scope_start, info.scope_end);
	int divider_pos = FindFirstTopLevelDividerN(text, info.scope_start, info.scope_end);
	if (divider_pos != -1) {
		insert_pos = std::min(insert_pos, divider_pos);
		info.scope_end = divider_pos;
	}
	info.insert_pos = insert_pos;
	return info;
}

static int ReplaceOrInsertInRange(std::string& text, int scope_start, int scope_end, int insert_pos, const std::vector<std::string>& tag_names, const std::string& value) {
	scope_start = std::clamp(scope_start, 0, (int)text.size());
	scope_end = std::clamp(scope_end, scope_start, (int)text.size());
	insert_pos = std::clamp(insert_pos, scope_start, scope_end);

	struct Match { int start; int name_len; int token_end; };
	std::vector<Match> matches;
	int depth = 0;
	for (int i = scope_start; i < scope_end; ++i) {
		char ch = text[i];
		if (ch == '(') ++depth;
		else if (ch == ')') --depth;
		if (depth == 0 && ch == '\\') {
			int name_end = i + 1;
			while (name_end < scope_end && (std::isalnum(static_cast<unsigned char>(text[name_end])) || text[name_end] == '-'))
				++name_end;
			std::string name = text.substr(i, name_end - i);
			bool matched = false;
			for (auto const& n : tag_names) {
				if (name == n) { matched = true; break; }
			}
			int token_end = name_end;
			if (token_end < scope_end && text[token_end] == '(') {
				int pd = 0;
				for (int j = token_end; j < scope_end; ++j) {
					if (text[j] == '(') ++pd;
					else if (text[j] == ')') {
						--pd;
						if (pd == 0) { token_end = j + 1; break; }
					}
				}
			}
			else {
				while (token_end < scope_end && text[token_end] != '\\' && text[token_end] != '}')
					++token_end;
			}
			if (matched)
				matches.push_back({i, name_end - i, token_end});
			i = token_end - 1;
		}
	}

	if (!matches.empty()) {
		auto m = matches.back();
		int value_start = m.start + m.name_len;
		int old_len = m.token_end - value_start;
		text.replace(value_start, old_len, value);
		return (int)value.size() - old_len;
	}

	text.insert(insert_pos, tag_names.front() + value);
	return (int)(tag_names.front().size() + value.size());
}

struct validate_sel_nonempty : public Command {
	CMD_TYPE(COMMAND_VALIDATE)
	bool Validate(const agi::Context *c) override {
		return c->selectionController->GetSelectedSet().size() > 0;
	}
};

struct validate_video_and_sel_nonempty : public Command {
	CMD_TYPE(COMMAND_VALIDATE)
	bool Validate(const agi::Context *c) override {
		return c->project->VideoProvider() && !c->selectionController->GetSelectedSet().empty();
	}
};

struct validate_sel_multiple : public Command {
	CMD_TYPE(COMMAND_VALIDATE)
	bool Validate(const agi::Context *c) override {
		return c->selectionController->GetSelectedSet().size() > 1;
	}
};

template<typename String>
AssDialogue *get_dialogue(String data) {
	boost::trim(data);
	try {
		// Try to interpret the line as an ASS line
		return new AssDialogue(data);
	}
	catch (...) {
		// Line didn't parse correctly, assume it's plain text that
		// should be pasted in the Text field only
		auto d = new AssDialogue;
		d->End = 0;
		d->Text = data;
		return d;
	}
}

template<typename Paster>
void paste_lines(agi::Context *c, bool paste_over, Paster&& paste_line) {
	std::string data = GetClipboard();
	if (data.empty()) return;

	AssDialogue *first = nullptr;
	Selection newsel;

	boost::char_separator<char> sep("\r\n");
	for (auto curdata : boost::tokenizer<boost::char_separator<char>>(data, sep)) {
		AssDialogue *inserted = paste_line(get_dialogue(curdata));
		if (!inserted)
			break;

		newsel.insert(inserted);
		if (!first)
			first = inserted;
	}

	if (first) {
		c->ass->Commit(_("paste"), paste_over ? AssFile::COMMIT_DIAG_FULL : AssFile::COMMIT_DIAG_ADDREM);

		if (!paste_over)
			c->selectionController->SetSelectionAndActive(std::move(newsel), first);
	}
}

AssDialogue *paste_over(wxWindow *parent, std::vector<bool>& pasteOverOptions, AssDialogue *new_line, AssDialogue *old_line) {
	if (pasteOverOptions.empty()) {
		if (!ShowPasteOverDialog(parent)) return nullptr;
		pasteOverOptions = OPT_GET("Tool/Paste Lines Over/Fields")->GetListBool();
	}

	if (pasteOverOptions[0])  old_line->Comment   = new_line->Comment;
	if (pasteOverOptions[1])  old_line->Layer     = new_line->Layer;
	if (pasteOverOptions[2])  old_line->Start     = new_line->Start;
	if (pasteOverOptions[3])  old_line->End       = new_line->End;
	if (pasteOverOptions[4])  old_line->Style     = new_line->Style;
	if (pasteOverOptions[5])  old_line->Actor     = new_line->Actor;
	if (pasteOverOptions[6])  old_line->Margin[0] = new_line->Margin[0];
	if (pasteOverOptions[7])  old_line->Margin[1] = new_line->Margin[1];
	if (pasteOverOptions[8])  old_line->Margin[2] = new_line->Margin[2];
	if (pasteOverOptions[9])  old_line->Effect    = new_line->Effect;
	if (pasteOverOptions[10]) old_line->Text      = new_line->Text;

	return old_line;
}

struct parsed_line {
	AssDialogue *line;
	std::vector<std::unique_ptr<AssDialogueBlock>> blocks;

	parsed_line(AssDialogue *line) : line(line), blocks(line->ParseTags()) { }
	parsed_line(parsed_line&& r) = default;
	parsed_line(parsed_line const& r) : line(r.line), blocks(r.line->ParseTags()) { }
	parsed_line& operator=(parsed_line const& r) {
		if (this == &r) return *this;
		line = r.line;
		blocks = line->ParseTags();
		return *this;
	}
	parsed_line& operator=(parsed_line&&) = default;

	const AssOverrideTag *find_tag(int blockn, std::string const& tag_name, std::string const& alt) const {
		for (auto ovr : blocks | sliced(0, blockn + 1) | reversed | agi::of_type<AssDialogueBlockOverride>()) {
			for (auto const& tag : ovr->Tags | reversed) {
				if (tag.Name == tag_name || tag.Name == alt)
					return &tag;
			}
		}
		return nullptr;
	}

	template<typename T>
	T get_value(int blockn, T initial, std::string const& tag_name, std::string const& alt = "") const {
		auto tag = find_tag(blockn, tag_name, alt);
		if (tag)
			return tag->Params[0].template Get<T>(initial);
		return initial;
	}

	int block_at_pos(int pos) const {
		auto const& text = line->Text.get();
		int n = 0;
		int max = text.size() - 1;
		bool in_block = false;

		for (int i = 0; i <= max; ++i) {
			if (text[i] == '{') {
				if (!in_block && i > 0 && pos >= 0)
					++n;
				in_block = true;
			}
			else if (text[i] == '}' && in_block) {
				in_block = false;
				if (pos > 0 && (i + 1 == max || text[i + 1] != '{'))
					n++;
			}
			else if (!in_block) {
				if (--pos == 0)
					return n + (i < max && text[i + 1] == '{');
			}
		}

		return n - in_block;
	}

	int set_tag(std::string const& tag, std::string const& value, int norm_pos, int orig_pos) {
		int blockn = block_at_pos(norm_pos);

		AssDialogueBlockPlain *plain = nullptr;
		AssDialogueBlockOverride *ovr = nullptr;
		while (blockn >= 0 && !plain && !ovr) {
			AssDialogueBlock *block = blocks[blockn].get();
			switch (block->GetType()) {
			case AssBlockType::PLAIN:
				plain = static_cast<AssDialogueBlockPlain *>(block);
				break;
			case AssBlockType::DRAWING:
				--blockn;
				break;
			case AssBlockType::COMMENT:
				--blockn;
				orig_pos = line->Text.get().rfind('{', orig_pos);
				break;
			case AssBlockType::OVERRIDE:
				ovr = static_cast<AssDialogueBlockOverride*>(block);
				break;
			}
		}

		// If we didn't hit a suitable block for inserting the override just put
		// it at the beginning of the line
		if (blockn < 0)
			orig_pos = 0;

		std::string insert(tag + value);
		int shift = insert.size();
		if (plain || blockn < 0) {
			line->Text = line->Text.get().substr(0, orig_pos) + "{" + insert + "}" + line->Text.get().substr(orig_pos);
			shift += 2;
			blocks = line->ParseTags();
		}
		else if (ovr) {
			std::string alt;
			if (tag == "\\c") alt = "\\1c";
			// Remove old of same
			bool found = false;
			for (size_t i = 0; i < ovr->Tags.size(); i++) {
				std::string const& name = ovr->Tags[i].Name;
				if (tag == name || alt == name) {
					shift -= ((std::string)ovr->Tags[i]).size();
					if (found) {
						ovr->Tags.erase(ovr->Tags.begin() + i);
						i--;
					}
					else {
						ovr->Tags[i].Params[0].Set(value);
						found = true;
					}
				}
			}
			if (!found)
				ovr->AddTag(insert);

			line->UpdateText(blocks);
		}
		else
			assert(false);

		return shift;
	}

	int remove_tag(std::string const& tag, int norm_pos, int orig_pos) {
		int blockn = block_at_pos(norm_pos);

		AssDialogueBlockOverride *ovr = nullptr;
		while (blockn >= 0 && !ovr) {
			AssDialogueBlock *block = blocks[blockn].get();
			switch (block->GetType()) {
			case AssBlockType::PLAIN:
				--blockn;
				break;
			case AssBlockType::DRAWING:
				--blockn;
				break;
			case AssBlockType::COMMENT:
				--blockn;
				orig_pos = line->Text.get().rfind('{', orig_pos);
				break;
			case AssBlockType::OVERRIDE:
				ovr = static_cast<AssDialogueBlockOverride*>(block);
				break;
			}
		}

		if (!ovr)
			return 0;

		int shift = 0;
		for (size_t i = 0; i < ovr->Tags.size(); ++i) {
			if (ovr->Tags[i].Name == tag) {
				shift -= ((std::string)ovr->Tags[i]).size();
				ovr->Tags.erase(ovr->Tags.begin() + i);
				--i;
			}
		}

		if (shift)
			line->UpdateText(blocks);
		return shift;
	}
};

int normalize_pos(std::string const& text, int pos) {
	int plain_len = 0;
	bool in_block = false;

	for (int i = 0, max = text.size() - 1; i < pos && i <= max; ++i) {
		if (text[i] == '{')
			in_block = true;
		if (!in_block)
			++plain_len;
		if (text[i] == '}' && in_block)
			in_block = false;
	}

	return plain_len;
}

template<typename Func>
void update_lines(const agi::Context *c, wxString const& undo_msg, Func&& f) {
	const auto active_line = c->selectionController->GetActiveLine();
	const int sel_start = c->textSelectionController->GetSelectionStart();
	const int sel_end = c->textSelectionController->GetSelectionEnd();
	const int norm_sel_start = normalize_pos(active_line->Text, sel_start);
	const int norm_sel_end = normalize_pos(active_line->Text, sel_end);
	int active_sel_shift = 0;

	for (const auto line : c->selectionController->GetSelectedSet()) {
		int shift = f(line, sel_start, sel_end, norm_sel_start, norm_sel_end);
		if (line == active_line)
			active_sel_shift = shift;
	}

	auto const& sel = c->selectionController->GetSelectedSet();
	c->ass->Commit(undo_msg, AssFile::COMMIT_DIAG_TEXT, -1, sel.size() == 1 ? *sel.begin() : nullptr);
	if (active_sel_shift != 0)
		c->textSelectionController->SetSelection(sel_start + active_sel_shift, sel_end + active_sel_shift);
}

// Manual test (Better View): enable Better View, use a long line with \N and Burmese text, apply BIUS or font changes at a caret and across a selection spanning a displayed newline; tags must align with the visual caret/selection.
template<typename Func>
void update_lines_mapped(const agi::Context *c, wxString const& undo_msg, int sel_start, int sel_end, bool better_view, Func&& f) {
	const auto active_line = c->selectionController->GetActiveLine();
	if (!active_line) return;

	const int norm_sel_start = normalize_pos(active_line->Text, sel_start);
	const int norm_sel_end = normalize_pos(active_line->Text, sel_end);
	int active_sel_shift = 0;

	for (const auto line : c->selectionController->GetSelectedSet()) {
		int shift = f(line, sel_start, sel_end, norm_sel_start, norm_sel_end);
		if (line == active_line)
			active_sel_shift = shift;
	}

	auto const& sel = c->selectionController->GetSelectedSet();
	c->ass->Commit(undo_msg, AssFile::COMMIT_DIAG_TEXT, -1, sel.size() == 1 ? *sel.begin() : nullptr);

	int new_start = sel_start + active_sel_shift;
	int new_end = sel_end + active_sel_shift;
	if (better_view && c->subsEditBox) {
		int disp_start = c->subsEditBox->MapRawToDisplay(new_start, active_line->Text.get());
		int disp_end = c->subsEditBox->MapRawToDisplay(new_end, active_line->Text.get());
		c->textSelectionController->SetSelection(disp_start, disp_end);
	}
	else if (active_sel_shift != 0) {
		c->textSelectionController->SetSelection(new_start, new_end);
	}
}

void toggle_override_tag(const agi::Context *c, bool (AssStyle::*field), const char *tag, wxString const& undo_msg) {
	const auto active_line = c->selectionController->GetActiveLine();
	if (!active_line) return;

	int disp_sel_start = c->textSelectionController->GetSelectionStart();
	int disp_sel_end = c->textSelectionController->GetSelectionEnd();
	int sel_start = disp_sel_start;
	int sel_end = disp_sel_end;
	bool better_view = c->subsEditBox && c->subsEditBox->BetterViewEnabled();
	std::string raw_before = active_line->Text.get();
	if (better_view && c->subsEditBox)
		c->subsEditBox->MapDisplayRangeToRaw(disp_sel_start, disp_sel_end, raw_before, sel_start, sel_end);

	if (sel_start == sel_end) {
		OverrideBlockInfo block = FindOverrideBlock(raw_before, sel_start);
		int smart = SmartInsertionPos(raw_before, sel_start, false);
		if (block.in_block)
			sel_start = sel_end = smart;
	}

	update_lines_mapped(c, undo_msg, sel_start, sel_end, better_view, [&](AssDialogue *line, int sel_start_raw, int sel_end_raw, int norm_sel_start, int norm_sel_end) {
		AssStyle const* const style = c->ass->GetStyle(line->Style);
		bool state = style ? style->*field : AssStyle().*field;

		parsed_line parsed(line);
		int blockn = parsed.block_at_pos(norm_sel_start);

		state = parsed.get_value(blockn, state, tag);

		int shift = parsed.set_tag(tag, state ? "0" : "1", norm_sel_start, sel_start_raw);
		if (sel_start_raw != sel_end_raw)
			parsed.set_tag(tag, state ? "1" : "0", norm_sel_end, sel_end_raw + shift);
		return shift;
	});
}

enum class ColorRestoreKind {
	RestoreNone,
	RestoreBare,
	RestoreValue
};

struct ColorRestoreInfo {
	ColorRestoreKind kind = ColorRestoreKind::RestoreNone;
	std::string value;
};

struct OverrideBlockRange {
	int start = -1;
	int end = -1;
};

static std::vector<OverrideBlockRange> FindOverrideBlocks(const std::string& text) {
	std::vector<OverrideBlockRange> blocks;
	int depth = 0;
	int block_start = -1;

	for (int i = 0; i < static_cast<int>(text.size()); ++i) {
		if (text[i] == '{') {
			if (depth == 0)
				block_start = i;
			++depth;
		}
		else if (text[i] == '}' && depth > 0) {
			--depth;
			if (depth == 0 && block_start >= 0) {
				blocks.push_back({block_start, i});
				block_start = -1;
			}
		}
	}

	return blocks;
}

static std::optional<OverrideBlockRange> GetEnclosingBlock(const std::vector<OverrideBlockRange>& blocks, int pos) {
	for (auto const& block : blocks) {
		if (pos >= block.start && pos <= block.end)
			return block;
	}
	return std::nullopt;
}

static int FindOverrideSpanIndexContainingPos(const std::vector<OverrideBlockRange>& blocks, int pos) {
	for (size_t i = 0; i < blocks.size(); ++i) {
		if (pos > blocks[i].start && pos < blocks[i].end)
			return static_cast<int>(i);
	}
	return -1;
}

static bool SelectionHasNormalText(int sel_start, int sel_end, const std::vector<OverrideBlockRange>& blocks) {
	sel_start = std::max(sel_start, 0);
	sel_end = std::max(sel_end, sel_start);
	if (sel_start == sel_end)
		return false;

	int cursor = sel_start;
	for (auto const& block : blocks) {
		if (block.end < cursor)
			continue;
		if (block.start > cursor)
			return true;
		cursor = std::max(cursor, block.end + 1);
		if (cursor >= sel_end)
			break;
	}
	return cursor < sel_end;
}

static int SafeInsertPosForEndpoint(int pos, const std::vector<OverrideBlockRange>& blocks) {
	int idx = FindOverrideSpanIndexContainingPos(blocks, pos);
	if (idx == -1)
		return pos;
	return blocks[static_cast<size_t>(idx)].end;
}

struct TInsertionPoint {
	bool valid = false;
	bool replace = false;
	int insert_pos = -1;
	int replace_start = -1;
	int replace_end = -1;
};

// Manual caret test cases (| = caret):
// A) {\\t(0,100,\\fs50|)}          -> insert before closing ')'
// B) {\\t(0,100,\\fs50|)\\Nnext}   -> insert before ')'
// C) {\\t(0,100,\\fs50|\\Nnext)}   -> insert before '\N'
// D) {\\t(0,100,\\clip(m 0 0 l 10 10)|\\bord2)} -> insert before the ')' closing \t, not inside clip(...)
// E) malformed {\\t(0,100,\\fs50|} -> fall back to legacy behavior
static bool FindTInsertionPoint(const std::string& text, int caret, const std::vector<std::string>& tag_names, TInsertionPoint& out_point) {
	out_point = TInsertionPoint();
	if (tag_names.empty())
		return false;

	OverrideBlockInfo block = FindOverrideBlock(text, caret);
	if (!block.in_block)
		return false;

	// Find nearest \t( to the left within the block
	int t_open = -1;
	int scan_start = std::min(caret, block.end);
	for (int i = scan_start; i >= block.start + 2; --i) {
		if (text[i] == '(' && (text[i - 1] == 't' || text[i - 1] == 'T') && text[i - 2] == '\\') {
			t_open = i - 2;
			break;
		}
		if (text[i] == '{' || text[i] == '}')
			break;
	}
	if (t_open == -1)
		return false;

	int args_start = t_open + 2; // position of '('
	if (caret < args_start)
		return false;

	// Find closing ')' for this \t( ... )
	int depth = 0;
	int t_close = -1;
	for (int i = args_start; i <= block.end; ++i) {
		char ch = text[i];
		if (ch == '(') {
			++depth;
		}
		else if (ch == ')') {
			if (depth == 0) {
				t_close = i;
				break;
			}
			else {
				--depth;
			}
		}
	}
	if (t_close == -1 || caret > t_close)
		return false;

	// Compute depth at caret so nested (...) are skipped naturally
	int depth_at_caret = 0;
	for (int i = args_start; i < caret && i < static_cast<int>(text.size()); ++i) {
		char ch = text[i];
		if (ch == '(')
			++depth_at_caret;
		else if (ch == ')' && depth_at_caret > 0)
			--depth_at_caret;
	}
	if (depth_at_caret < 0)
		depth_at_caret = 0;

	// Try replace-mode: find an existing matching tag inside this \t(...) at depth 0
	struct Candidate {
		int start = -1;
		int value_start = -1;
		int value_end = -1;
		bool contains_caret = false;
	};
	std::optional<Candidate> best;
	std::optional<Candidate> left_best;
	depth = 0;
	for (int i = args_start; i < t_close; ++i) {
		char ch = text[i];
		if (ch == '(') {
			++depth;
			continue;
		}
		if (ch == ')') {
			if (depth > 0)
				--depth;
			continue;
		}
		if (ch != '\\' || depth != 0)
			continue;

		for (auto const& name : tag_names) {
			auto name_len = static_cast<int>(name.size());
			if (i + name_len > t_close)
				continue;
			if (text.compare(i, name_len, name) != 0)
				continue;

			int value_start = i + name_len;
			int val_depth = 0;
			int j = value_start;
			for (; j < t_close; ++j) {
				char vch = text[j];
				if (vch == '(')
					++val_depth;
				else if (vch == ')') {
					if (val_depth == 0)
						break;
					--val_depth;
				}
				else if (vch == '\\' && val_depth == 0 && j != value_start) {
					break;
				}
			}
			int value_end = j;
			bool contains = caret >= value_start && caret <= value_end;
			Candidate cand{i, value_start, value_end, contains};
			if (contains) {
				best = cand;
				break;
			}
			if (i <= caret) {
				if (!left_best || i > left_best->start)
					left_best = cand;
			}
			break;
		}
		if (best)
			break;
	}
	if (!best && left_best)
		best = left_best;
	if (best) {
		out_point.valid = true;
		out_point.replace = true;
		out_point.insert_pos = best->value_start;
		out_point.replace_start = best->value_start;
		out_point.replace_end = best->value_end;
		return true;
	}

	// No replace: compute insertion point using depth-aware forward scan
	int ins_pos = -1;
	int scan_depth = depth_at_caret;
	for (int i = caret; i <= t_close; ++i) {
		char ch = text[i];
		if (ch == '(') {
			++scan_depth;
		}
		else if (ch == ')') {
			if (scan_depth == 0) {
				ins_pos = i;
				break;
			}
			if (scan_depth > 0)
				--scan_depth;
		}
		else if (ch == '\\' && i + 1 <= t_close && text[i + 1] == 'N' && scan_depth == 0) {
			ins_pos = i;
			break;
		}
	}
	if (ins_pos == -1)
		return false;

	out_point.valid = true;
	out_point.insert_pos = ins_pos;
	return true;
}

static std::string MakeChannelColorTag(int channel) {
	return agi::format("\\%dc", channel);
}

static ColorRestoreInfo FindColorRestoreInfo(const std::string& text, int sel_start, int channel) {
	ColorRestoreInfo info;
	bool in_override = false;
	bool in_transform = false;
	int paren_depth = 0;
	const std::string channel_tag = MakeChannelColorTag(channel).substr(1); // drop leading backslash

	auto is_target_tag = [&](const std::string& name) {
		if (channel == 1)
			return name == "c" || name == "1c";
		return name == channel_tag;
	};

	for (int i = 0; i < static_cast<int>(text.size()) && i < sel_start; ++i) {
		char ch = text[i];
		if (ch == '{') {
			in_override = true;
			continue;
		}
		if (ch == '}' && in_override) {
			in_override = false;
			in_transform = false;
			paren_depth = 0;
			continue;
		}
		if (!in_override)
			continue;

		if (in_transform) {
			if (ch == '(')
				++paren_depth;
			else if (ch == ')' && paren_depth > 0) {
				--paren_depth;
				if (paren_depth == 0)
					in_transform = false;
			}
			continue;
		}

		if (ch != '\\')
			continue;

		int tag_start = i + 1;
		int tag_end = tag_start;
		while (tag_end < static_cast<int>(text.size()) && (std::isalnum(static_cast<unsigned char>(text[tag_end])) || text[tag_end] == '-'))
			++tag_end;
		if (tag_end == tag_start)
			continue;

		std::string name = text.substr(tag_start, tag_end - tag_start);
		i = tag_end - 1;

		if (name == "t") {
			while (tag_end < static_cast<int>(text.size()) && std::isspace(static_cast<unsigned char>(text[tag_end])))
				++tag_end;
			if (tag_end < static_cast<int>(text.size()) && text[tag_end] == '(') {
				in_transform = true;
				paren_depth = 1;
				i = tag_end;
			}
			continue;
		}

		if (!is_target_tag(name))
			continue;

		int value_start = tag_end;
		while (value_start < static_cast<int>(text.size()) && std::isspace(static_cast<unsigned char>(text[value_start])))
			++value_start;

		if (value_start + 1 < static_cast<int>(text.size()) && text[value_start] == '&' && (text[value_start + 1] == 'H' || text[value_start + 1] == 'h')) {
			int value_end = value_start + 2;
			while (value_end < static_cast<int>(text.size()) && text[value_end] != '&')
				++value_end;
			if (value_end < static_cast<int>(text.size())) {
				info.kind = ColorRestoreKind::RestoreValue;
				info.value = text.substr(value_start, value_end - value_start + 1);
				i = value_end;
				continue;
			}
		}

		info.kind = ColorRestoreKind::RestoreBare;
		info.value.clear();
	}

	return info;
}

struct SelectionShift {
	int start = 0;
	int end = 0;
};

struct ColorWrapResult {
	SelectionShift shift;
	int start_pos = -1;
	int start_len = 0;
	int restore_pos = -1;
	int restore_len = 0;
};

enum class ChannelTagType {
	TagNone,
	TagColor,
	TagGradient
};

struct ChannelTagState {
	ChannelTagType type = ChannelTagType::TagNone;
	std::string name;
	std::string value;
};

struct AlphaTagState {
	bool has = false;
	bool gradient = false;
	std::string name;
	std::string value;
	int simple_value = -1;
};

struct SelectionContext {
	ChannelTagState channels[4];
	AlphaTagState alphas[4];
};

struct SelectionApplyOptions {
	int channel = 1;
	bool use_gradient = false;
	bool add_color_tag = true;
	bool apply_alpha_gradient = false;
	std::string new_color_value;
	std::string new_alpha_gradient_value;
	std::string color_tag_name;
	std::string gradient_tag_name;
	std::string alpha_tag_name;
	std::string alpha_gradient_tag_name;
};

struct SelectionApplyResult {
	std::string text;
	SelectionShift shift;
};

static ColorWrapResult InsertTagPairAtSelection(
	AssDialogue *line,
	int sel_start,
	int sel_end,
	std::string start_tag,
	std::string end_tag)
{
	std::string text = line->Text;
	int selection_start = sel_start;
	int selection_end = sel_end;
	auto blocks = FindOverrideBlocks(text);

	auto refresh_blocks = [&](const std::string& updated) {
		return FindOverrideBlocks(updated);
	};

	auto find_close = [&](int pos) -> std::optional<int> {
		auto block = GetEnclosingBlock(blocks, pos);
		if (block)
			return block->end;
		return std::nullopt;
	};

	ColorWrapResult local_result;

	auto insert_text = [&](int pos, const std::string& content, bool wrap) {
		if (content.empty())
			return;
		const std::string insertion = wrap ? "{" + content + "}" : content;
		text.insert(pos, insertion);

		if (pos <= selection_start) {
			selection_start += insertion.size();
			selection_end += insertion.size();
		}
		else if (pos < selection_end) {
			selection_end += insertion.size();
		}

		blocks = refresh_blocks(text);
	};

	int start_insert_pos = selection_start;
	bool start_inside_block = false;
	if (auto close = find_close(selection_start)) {
		start_insert_pos = *close;
		start_inside_block = true;
	}
	insert_text(start_insert_pos, start_tag, !start_inside_block);
	if (!start_tag.empty()) {
		local_result.start_pos = start_insert_pos + (start_inside_block ? 0 : 1);
		local_result.start_len = start_tag.size();
	}

	int end_insert_pos = selection_end;
	bool end_inside_block = false;
	if (auto close = find_close(selection_end)) {
		end_insert_pos = *close;
		end_inside_block = true;
	}
	else if (end_insert_pos < static_cast<int>(text.size()) && text[end_insert_pos] == '}') {
		end_inside_block = true;
	}
	insert_text(end_insert_pos, end_tag, !end_inside_block);
	if (!end_tag.empty()) {
		local_result.restore_pos = end_insert_pos + (end_inside_block ? 0 : 1);
		local_result.restore_len = end_tag.size();
	}

	line->Text = text;
	local_result.shift = {selection_start - sel_start, selection_end - sel_end};
	return local_result;
}

static std::string BuildColorTagString(int channel, const agi::Color& color, const std::string& alpha_tag) {
	std::string tag = MakeChannelColorTag(channel) + color.GetAssOverrideFormatted();
	if (!alpha_tag.empty())
		tag += alpha_tag + agi::format("&H%02X&", (int)color.a);
	return tag;
}

static std::string BuildRestoreTagString(int channel, ColorRestoreInfo const& restore, const std::string& alpha_tag, int original_alpha) {
	std::string tag = MakeChannelColorTag(channel);

	if (restore.kind == ColorRestoreKind::RestoreValue)
		tag += restore.value;
	else if (restore.kind == ColorRestoreKind::RestoreBare || restore.kind == ColorRestoreKind::RestoreNone)
		tag = MakeChannelColorTag(channel);

	if (!alpha_tag.empty())
		tag += alpha_tag + agi::format("&H%02X&", original_alpha);

	return tag;
}

static ColorWrapResult ApplyColorWrapToLine(
	AssDialogue *line,
	int sel_start,
	int sel_end,
	int channel,
	const agi::Color& new_color,
	ColorRestoreInfo const& restore,
	std::string const& alpha_tag,
	int original_alpha)
{
	std::string color_tag = BuildColorTagString(channel, new_color, alpha_tag);
	std::string restore_tag = BuildRestoreTagString(channel, restore, alpha_tag, original_alpha);
	return InsertTagPairAtSelection(line, sel_start, sel_end, color_tag, restore_tag);
}

static std::string StripBackslash(std::string tag) {
	if (!tag.empty() && tag[0] == '\\')
		tag.erase(tag.begin());
	return tag;
}

static std::string FormatAlphaValue(int value) {
	return agi::format("&H%02X&", value);
}

static bool IsWholeLineSelection(const std::string& text, int sel_start, int sel_end) {
	bool in_block = false;
	int first_visible = -1;
	int last_visible = -1;

	for (int i = 0; i < static_cast<int>(text.size()); ++i) {
		char ch = text[i];
		if (ch == '{') {
			in_block = true;
			continue;
		}
		if (ch == '}') {
			in_block = false;
			continue;
		}
		if (in_block)
			continue;

		if (first_visible == -1)
			first_visible = i;
		last_visible = i;
	}

	if (first_visible == -1)
		return true;

	return sel_start <= first_visible && sel_end >= last_visible + 1;
}

static std::optional<int> ParseAlphaValue(const std::string& value) {
	auto pos = value.find("&H");
	if (pos == std::string::npos)
		pos = value.find("&h");
	if (pos == std::string::npos)
		return std::nullopt;

	pos += 2;
	size_t end = pos;
	while (end < value.size() && isxdigit(static_cast<unsigned char>(value[end])))
		++end;
	if (end == pos)
		return std::nullopt;

	try {
		int parsed = std::stoi(value.substr(pos, end - pos), nullptr, 16);
		return std::clamp(parsed, 0, 255);
	}
	catch (...) {
		return std::nullopt;
	}
}

static std::string ExtractTagValue(const std::string& text, int start, int limit) {
	int value_start = start;
	while (value_start < limit && std::isspace(static_cast<unsigned char>(text[value_start])))
		++value_start;

	int value_end = value_start;
	if (value_start < limit && text[value_start] == '(') {
		int depth = 0;
		while (value_end < limit) {
			char ch = text[value_end];
			if (ch == '(')
				++depth;
			else if (ch == ')') {
				if (--depth == 0) {
					++value_end;
					break;
				}
			}
			else if (ch == '}' && depth == 0) {
				break;
			}
			++value_end;
		}
	}
	else {
		while (value_end < limit && text[value_end] != '\\' && text[value_end] != '}')
			++value_end;
	}

	std::string value = text.substr(value_start, value_end - value_start);
	boost::trim(value);
	return value;
}

static int GetChannelFromName(const std::string& name, const std::string& base) {
	if (name == base)
		return 1;
	if (name.size() == base.size() + 1 && std::isdigit(static_cast<unsigned char>(name[0])) && name.substr(1) == base) {
		int channel = name[0] - '0';
		if (channel >= 1 && channel <= 4)
			return channel;
	}
	return 0;
}

static SelectionContext ScanSelectionContext(const std::string& text, int sel_start) {
	SelectionContext ctx;
	bool in_override = false;
	bool in_transform = false;
	int paren_depth = 0;
	int limit = std::clamp(sel_start, 0, static_cast<int>(text.size()));

	for (int i = 0; i < limit; ++i) {
		char ch = text[i];
		if (ch == '{') {
			in_override = true;
			continue;
		}
		if (ch == '}' && in_override) {
			in_override = false;
			in_transform = false;
			paren_depth = 0;
			continue;
		}
		if (!in_override)
			continue;

		if (in_transform) {
			if (ch == '(')
				++paren_depth;
			else if (ch == ')' && paren_depth > 0 && --paren_depth == 0)
				in_transform = false;
			continue;
		}

		if (ch != '\\')
			continue;

		int tag_start = i + 1;
		int tag_end = tag_start;
		while (tag_end < limit && (std::isalnum(static_cast<unsigned char>(text[tag_end])) || text[tag_end] == '-'))
			++tag_end;
		if (tag_end == tag_start)
			continue;

		std::string name = text.substr(tag_start, tag_end - tag_start);
		i = tag_end - 1;

		if (name == "t") {
			while (tag_end < limit && std::isspace(static_cast<unsigned char>(text[tag_end])))
				++tag_end;
			if (tag_end < limit && text[tag_end] == '(') {
				in_transform = true;
				paren_depth = 1;
				i = tag_end;
			}
			continue;
		}

		bool alpha_tag = false;
		bool gradient_tag = false;
		int channel = 0;

		if ((channel = GetChannelFromName(name, "vc")) != 0) {
			gradient_tag = true;
		}
		else if ((channel = GetChannelFromName(name, "c")) != 0) {
			gradient_tag = false;
		}
		else if ((channel = GetChannelFromName(name, "va")) != 0) {
			alpha_tag = true;
			gradient_tag = true;
		}
		else if ((channel = GetChannelFromName(name, "a")) != 0 || name == "alpha") {
			alpha_tag = true;
			gradient_tag = false;
			if (!channel)
				channel = 1;
		}
		else {
			continue;
		}

		std::string value = ExtractTagValue(text, tag_end, limit);
		if (alpha_tag) {
			auto& state = ctx.alphas[channel - 1];
			state.has = true;
			state.gradient = gradient_tag;
			state.name = name;
			state.value = value;
			state.simple_value = -1;
			if (!gradient_tag) {
				if (auto parsed = ParseAlphaValue(value))
					state.simple_value = *parsed;
			}
		}
		else {
			auto& state = ctx.channels[channel - 1];
			state.type = gradient_tag ? ChannelTagType::TagGradient : ChannelTagType::TagColor;
			state.name = name;
			state.value = value;
		}
	}

	return ctx;
}

static SelectionApplyResult InsertBoundaryTags(
	const std::string& base_text,
	int sel_start,
	int sel_end,
	const std::string& start_content,
	const std::string& end_content,
	bool start_inside_override,
	bool end_inside_override)
{
	SelectionApplyResult result;
	std::string text = base_text;
	int selection_start = sel_start;
	int selection_end = sel_end;

	if (!end_content.empty()) {
		bool inside_block = end_inside_override || (selection_end < static_cast<int>(text.size()) && text[selection_end] == '}');
		if (inside_block) {
			text.insert(selection_end, end_content);
		}
		else if (selection_end < static_cast<int>(text.size()) && text[selection_end] == '{') {
			text.insert(selection_end + 1, end_content);
		}
		else {
			text.insert(selection_end, "{" + end_content + "}");
		}
	}

	if (!start_content.empty()) {
		bool inside_prev_block =
			start_inside_override ||
			(selection_start > 0 && text[selection_start - 1] == '}') ||
			(selection_start < static_cast<int>(text.size()) && text[selection_start] == '}');
		if (inside_prev_block) {
			text.insert(selection_start, start_content);
			selection_start += static_cast<int>(start_content.size());
			selection_end += static_cast<int>(start_content.size());
		}
		else {
			const std::string insertion = "{" + start_content + "}";
			text.insert(selection_start, insertion);
			selection_start += static_cast<int>(insertion.size());
			selection_end += static_cast<int>(insertion.size());
		}
	}

	result.text = std::move(text);
	result.shift = {selection_start - sel_start, selection_end - sel_end};
	return result;
}

static SelectionApplyResult ApplyColorOrGradientToRange(
	const std::string& text,
	int sel_start,
	int sel_end,
	const SelectionApplyOptions& opts)
{
	// Unified, selection-safe application path used by both preview and commit.
	SelectionApplyOptions params = opts;
	if (params.color_tag_name.empty())
		params.color_tag_name = StripBackslash(MakeChannelColorTag(params.channel));
	if (params.gradient_tag_name.empty())
		params.gradient_tag_name = agi::format("%dvc", params.channel);
	if (params.alpha_tag_name.empty())
		params.alpha_tag_name = agi::format("%da", params.channel);
	if (params.alpha_gradient_tag_name.empty())
		params.alpha_gradient_tag_name = agi::format("%dva", params.channel);

	int selection_start = std::clamp(sel_start, 0, static_cast<int>(text.size()));
	int selection_end = std::clamp(sel_end, 0, static_cast<int>(text.size()));
	selection_end = std::max(selection_end, selection_start);

	const int original_start = selection_start;
	const int original_end = selection_end;

	std::vector<OverrideBlockRange> spans = FindOverrideBlocks(text);
	int apply_start = selection_start;
	int apply_end = selection_end;
	if (selection_start < selection_end) {
		apply_start = SafeInsertPosForEndpoint(selection_start, spans);
		int end_probe = selection_end > selection_start ? selection_end - 1 : selection_end;
		apply_end = SafeInsertPosForEndpoint(end_probe, spans);
		apply_end = std::max(apply_end, apply_start);
	}

	const bool whole_line = IsWholeLineSelection(text, original_start, original_end);

	SelectionContext start_ctx = ScanSelectionContext(text, apply_start);
	SelectionContext end_ctx = ScanSelectionContext(text, apply_end);
	ChannelTagState start_prev_color = start_ctx.channels[params.channel - 1];
	AlphaTagState start_prev_alpha = start_ctx.alphas[params.channel - 1];
	ChannelTagState restore_prev_color = end_ctx.channels[params.channel - 1];
	AlphaTagState restore_prev_alpha = end_ctx.alphas[params.channel - 1];

	if (whole_line) {
		start_prev_color = ChannelTagState();
		restore_prev_color = ChannelTagState();
		start_prev_alpha = AlphaTagState();
		restore_prev_alpha = AlphaTagState();
	}

	std::vector<std::string> start_alpha_tags;
	std::vector<std::string> start_color_tags;
	std::vector<std::string> end_alpha_tags;
	std::vector<std::string> end_color_tags;

	auto make_tag = [](const std::string& name, const std::string& value) {
		if (name.empty())
			return std::string();
		return "\\" + name + value;
	};

	if (params.add_color_tag) {
		if (params.use_gradient) {
			if (auto tag = make_tag(params.gradient_tag_name, params.new_color_value); !tag.empty())
				start_color_tags.push_back(tag);

			bool alpha_ff = start_prev_alpha.has && !start_prev_alpha.gradient && start_prev_alpha.simple_value == 0xFF;
			if (params.channel <= 2 && !params.alpha_tag_name.empty() && alpha_ff &&
								(start_prev_color.type == ChannelTagType::TagColor || start_prev_color.type == ChannelTagType::TagNone)) {
				if (auto tag = make_tag(params.alpha_tag_name, FormatAlphaValue(0)); !tag.empty())
					start_alpha_tags.push_back(tag);
				if (!whole_line) {
					const std::string restore_value = restore_prev_alpha.gradient ? restore_prev_alpha.value : FormatAlphaValue(restore_prev_alpha.simple_value);
					const std::string restore_name = restore_prev_alpha.name.empty() ? params.alpha_tag_name : restore_prev_alpha.name;
					if (auto tag = make_tag(restore_name, restore_value); !tag.empty())
						end_alpha_tags.push_back(tag);
				}
			}

			if (!whole_line) {
				if (restore_prev_color.type == ChannelTagType::TagGradient) {
					if (auto tag = make_tag(restore_prev_color.name, restore_prev_color.value); !tag.empty())
						end_color_tags.push_back(tag);
				}
				else if (restore_prev_color.type == ChannelTagType::TagColor) {
					if (auto tag = make_tag(restore_prev_color.name, restore_prev_color.value); !tag.empty())
						end_color_tags.push_back(tag);
				}
				else {
					if (auto tag = make_tag(params.color_tag_name, ""); !tag.empty())
						end_color_tags.push_back(tag);
				}
			}
		}
		else {
			if (auto tag = make_tag(params.color_tag_name, params.new_color_value); !tag.empty())
				start_color_tags.push_back(tag);

			bool start_is_gradient = start_prev_color.type == ChannelTagType::TagGradient;
			bool restore_is_gradient = restore_prev_color.type == ChannelTagType::TagGradient;
			bool alpha_ff = start_prev_alpha.has && !start_prev_alpha.gradient && start_prev_alpha.simple_value == 0xFF;

			if (params.channel <= 2 && !params.alpha_tag_name.empty()) {
				if (start_is_gradient && !alpha_ff) {
					if (auto tag = make_tag(params.alpha_tag_name, FormatAlphaValue(0xFF)); !tag.empty())
						start_alpha_tags.push_back(tag);
					if (!whole_line)
						if (auto tag = make_tag(params.alpha_tag_name, FormatAlphaValue(0x00)); !tag.empty())
							end_alpha_tags.push_back(tag);
				}

				if ((start_prev_color.type == ChannelTagType::TagColor || start_prev_color.type == ChannelTagType::TagNone) && alpha_ff) {
					if (auto tag = make_tag(params.alpha_tag_name, FormatAlphaValue(0)); !tag.empty())
						start_alpha_tags.push_back(tag);
					if (!whole_line) {
						const std::string restore_value = restore_prev_alpha.gradient ? restore_prev_alpha.value : FormatAlphaValue(restore_prev_alpha.simple_value);
						const std::string restore_name = restore_prev_alpha.name.empty() ? params.alpha_tag_name : restore_prev_alpha.name;
						if (auto tag = make_tag(restore_name, restore_value); !tag.empty())
							end_alpha_tags.push_back(tag);
					}
				}
			}

			if (!whole_line) {
				if (restore_is_gradient) {
					if (auto tag = make_tag(restore_prev_color.name, restore_prev_color.value); !tag.empty())
						end_color_tags.push_back(tag);
				}
				else if (restore_prev_color.type == ChannelTagType::TagColor) {
					if (auto tag = make_tag(restore_prev_color.name, restore_prev_color.value); !tag.empty())
						end_color_tags.push_back(tag);
				}
				else {
					if (auto tag = make_tag(params.color_tag_name, ""); !tag.empty())
						end_color_tags.push_back(tag);
				}
			}
		}
	}

	if (params.apply_alpha_gradient && !params.alpha_gradient_tag_name.empty()) {
		if (!params.alpha_gradient_tag_name.empty() && !params.new_alpha_gradient_value.empty())
			if (auto tag = make_tag(params.alpha_gradient_tag_name, params.new_alpha_gradient_value); !tag.empty())
				start_alpha_tags.push_back(tag);

		if (!whole_line) {
			if (restore_prev_alpha.has && !restore_prev_alpha.name.empty())
				if (auto tag = make_tag(restore_prev_alpha.name, restore_prev_alpha.value); !tag.empty())
					end_alpha_tags.push_back(tag);
			else if (!params.alpha_tag_name.empty())
				if (auto tag = make_tag(params.alpha_tag_name, ""); !tag.empty())
					end_alpha_tags.push_back(tag);
		}
	}

	auto combine_tags = [](const std::vector<std::string>& alphas, const std::vector<std::string>& colors) {
		std::string content;
		for (auto const& tag : alphas)
			content += tag;
		for (auto const& tag : colors)
			content += tag;
		return content;
	};

	std::string start_content = combine_tags(start_alpha_tags, start_color_tags);
	std::string end_content = combine_tags(end_alpha_tags, end_color_tags);

	auto inside_or_closing = [&](int pos) {
		if (FindOverrideSpanIndexContainingPos(spans, pos) != -1)
			return true;
		for (auto const& span : spans) {
			if (pos == span.end)
				return true;
		}
		return false;
	};
	bool start_inside_block = inside_or_closing(apply_start);
	bool end_inside_block = inside_or_closing(apply_end);

	SelectionApplyResult wrapped = InsertBoundaryTags(text, apply_start, apply_end, start_content, end_content, start_inside_block, end_inside_block);
	wrapped.shift.start += apply_start - original_start;
	wrapped.shift.end += apply_end - original_end;
	return wrapped;
}

void show_color_picker(const agi::Context *c, agi::Color (AssStyle::*field), const char *tag, const char *alt, const char *alpha, ColorPickerInvoker picker = GetColorFromUser) {
	agi::Color initial_color;
	const auto active_line = c->selectionController->GetActiveLine();
	const int disp_sel_start = c->textSelectionController->GetSelectionStart();
	const int disp_sel_end = c->textSelectionController->GetSelectionEnd();
	int sel_start = disp_sel_start;
	int sel_end = disp_sel_end;

	const bool better_view = c->subsEditBox && c->subsEditBox->BetterViewEnabled();
	std::string active_raw_text = active_line ? active_line->Text.get() : std::string();
	if (better_view && active_line && c->subsEditBox)
		c->subsEditBox->MapDisplayRangeToRaw(disp_sel_start, disp_sel_end, active_raw_text, sel_start, sel_end);

	bool has_selection = sel_end > sel_start;
	if (has_selection && active_line) {
		auto spans = FindOverrideBlocks(active_raw_text);
		bool selection_has_normal = SelectionHasNormalText(sel_start, sel_end, spans);
		// Selection rules sanity checks:
		// - {\i1}word{\i0} selecting "word" -> wrap normally.
		// - {\fad(200,200)} selecting inside params -> collapse to caret (no wrap).
		// - {\i1}word{\i0} selecting "1}word{\\i" -> move to safe } boundaries, keep wrap.
		// - a{\i1}b{\i0}c selecting across text -> wrap normally.
		auto enclosing_span = [&]() -> int {
			for (size_t i = 0; i < spans.size(); ++i) {
				if (sel_start >= spans[i].start && sel_end <= spans[i].end + 1)
					return static_cast<int>(i);
			}
			return -1;
		};
		int span_idx = enclosing_span();
		if (!selection_has_normal && span_idx != -1) {
			sel_start = sel_end;
			has_selection = false;
		}
	}
	if (!has_selection && active_line) {
		OverrideBlockInfo block = FindOverrideBlock(active_raw_text, sel_start);
		bool inside_t = IsInsideTParens(active_raw_text, block, sel_start);
		sel_start = sel_end = SmartInsertionPos(active_raw_text, sel_start, inside_t);
	}

	const int norm_sel_start = normalize_pos(active_line->Text, sel_start);
	has_selection = sel_end > sel_start;
	const bool shin_requested = picker == GetColorFromUserShin;
	const bool force_legacy_dialog = shin_requested && has_selection;
	ColorPickerInvoker effective_picker = force_legacy_dialog ? GetColorFromUser : picker;
	const bool using_shin = effective_picker == GetColorFromUserShin;
	const bool allow_gradient = using_shin && !has_selection;
	const bool use_selection_wrap = has_selection && !allow_gradient;
	const int channel = GetChannelFromTags(tag, alt);
	auto set_selection_display = [&](int raw_start, int raw_end, const std::string& raw_text) {
		if (better_view && c->subsEditBox) {
			int disp_start = c->subsEditBox->MapRawToDisplay(raw_start, raw_text);
			int disp_end = c->subsEditBox->MapRawToDisplay(raw_end, raw_text);
			c->textSelectionController->SetSelection(disp_start, disp_end);
		}
		else {
			c->textSelectionController->SetSelection(raw_start, raw_end);
		}
	};
	auto reset_selection = [&]() {
		if (better_view && c->subsEditBox)
			c->textSelectionController->SetSelection(disp_sel_start, disp_sel_end);
		else
			c->textSelectionController->SetSelection(sel_start, sel_end);
	};

	auto const& sel = c->selectionController->GetSelectedSet();
	struct line_info {
		agi::Color color;
		parsed_line parsed;
	ColorRestoreInfo restore;
	ColorWrapResult wrap_result;
	std::string original_text;

	line_info(agi::Color c, parsed_line&& p, ColorRestoreInfo r, std::string original)
	: color(c)
	, parsed(std::move(p))
	, restore(std::move(r))
	, wrap_result()
	, original_text(std::move(original))
	{
	}
	};
	std::vector<line_info> lines;
	for (auto line : sel) {
		AssStyle const* const style = c->ass->GetStyle(line->Style);
		agi::Color color = (style ? style->*field : AssStyle().*field);

		parsed_line parsed(line);
		int blockn = parsed.block_at_pos(norm_sel_start);

		int a = parsed.get_value(blockn, (int)color.a, alpha, "\\alpha");
		color = parsed.get_value(blockn, color, tag, alt);
		color.a = a;

		if (line == active_line)
			initial_color = color;

		ColorRestoreInfo restore_info;
		if (use_selection_wrap)
			restore_info = FindColorRestoreInfo(line->Text, sel_start, channel);

		lines.emplace_back(color, std::move(parsed), restore_info, line->Text.get());
	}

	auto restore_original_texts = [&]() {
		for (auto& entry : lines) {
			entry.parsed.line->Text = entry.original_text;
			entry.parsed = parsed_line(entry.parsed.line);
		}
	};

	auto lines_are_valid_utf8 = [&]() {
		for (auto& entry : lines) {
			if (!IsValidUtf8(entry.parsed.line->Text.get()))
				return false;
		}
		return true;
	};

	int active_shift = 0;
	int commit_id = -1;

	const std::string color_tag_str = tag ? std::string(tag) : std::string();
	const std::string color_tag_alt_str = alt ? std::string(alt) : std::string();
	const std::string alpha_tag_str = alpha ? std::string(alpha) : std::string();

	std::function<void(wxWindow*)> gradient_handler;
	bool gradient_used = false;
	const std::string color_tag_name = StripBackslash(MakeChannelColorTag(channel));
	const std::string gradient_tag_name = StripBackslash(agi::format("\\%dvc", channel));
	const std::string alpha_tag_name = StripBackslash(alpha_tag_str);
	const std::string gradient_alpha_tag_name = StripBackslash(agi::format("\\%dva", channel));

	if (allow_gradient) {
		const std::string gradient_tag = agi::format("\\%dvc", channel);
		const std::string gradient_alpha_tag = agi::format("\\%dva", channel);
		const std::string gradient_tag_alt = channel == 1 ? "\\vc" : std::string();
		const std::string gradient_alpha_tag_alt = channel == 1 ? "\\va" : std::string();

		gradient_handler = [=, &lines, &sel, &gradient_used, &commit_id](wxWindow *owner) mutable {
			if (!owner || !active_line) return;
			gradient_used = true;

			for (auto& entry : lines)
				entry.parsed = parsed_line(entry.parsed.line);

			std::vector<std::string> original_texts;
			original_texts.reserve(lines.size());
			for (auto& entry : lines)
				original_texts.push_back(entry.parsed.line->Text.get());

			auto it = std::find_if(lines.begin(), lines.end(), [&](line_info& info) {
				return info.parsed.line == active_line;
			});
			if (it == lines.end()) return;

			parsed_line *active_parsed = &it->parsed;
			int blockn = active_parsed->block_at_pos(norm_sel_start);

			AssStyle const* style = c->ass->GetStyle(active_line->Style);
			agi::Color base = (style ? style->*field : AssStyle().*field);
			base = active_parsed->get_value(blockn, base, tag, alt);
			int base_alpha = active_parsed->get_value(blockn, (int)base.a, alpha, "\\alpha");
			base.a = base_alpha;

			VcVaGradientState gradient_state;
			for (int i = 0; i < 4; ++i) {
				gradient_state.colors[i] = base;
				gradient_state.alphas[i] = base_alpha;
				gradient_state.style_colors[i] = base;
				gradient_state.style_alphas[i] = base_alpha;
			}

			if (const AssOverrideTag *grad_tag = active_parsed->find_tag(blockn, gradient_tag, gradient_tag_alt)) {
				for (size_t i = 0; i < grad_tag->Params.size() && i < gradient_state.colors.size(); ++i) {
					gradient_state.colors[i] = grad_tag->Params[i].Get<agi::Color>(gradient_state.colors[i]);
					gradient_state.colors[i].a = gradient_state.alphas[i];
				}
			}
			if (const AssOverrideTag *alpha_tag_ptr = active_parsed->find_tag(blockn, gradient_alpha_tag, gradient_alpha_tag_alt)) {
				for (size_t i = 0; i < alpha_tag_ptr->Params.size() && i < gradient_state.alphas.size(); ++i)
					gradient_state.alphas[i] = alpha_tag_ptr->Params[i].Get<int>(gradient_state.alphas[i]);
			}
			gradient_state.alpha_touched = false;

			int gradient_commit_id = -1;
			auto apply_state = [&](bool use_color, bool use_alpha, const std::array<agi::Color, 4>& colors, const std::array<uint8_t, 4>& alphas) {
				// Gradient currently ignores selection wrapping to avoid crashes; apply at caret only.
				int local_active_shift = 0;
				for (size_t idx = 0; idx < lines.size(); ++idx) {
					auto& entry = lines[idx];
					entry.parsed.line->Text = original_texts[idx];
					entry.parsed = parsed_line(entry.parsed.line);
					int shift = 0;

					if (use_color) {
						shift += entry.parsed.remove_tag(gradient_tag, norm_sel_start, sel_start + shift);
						if (!gradient_tag_alt.empty())
							shift += entry.parsed.remove_tag(gradient_tag_alt, norm_sel_start, sel_start + shift);
						if (!color_tag_str.empty())
							shift += entry.parsed.remove_tag(color_tag_str, norm_sel_start, sel_start + shift);
						if (!color_tag_alt_str.empty())
							shift += entry.parsed.remove_tag(color_tag_alt_str, norm_sel_start, sel_start + shift);
						shift += entry.parsed.set_tag(gradient_tag, FormatGradientColors(colors), norm_sel_start, sel_start + shift);
					}

					if (use_alpha) {
						shift += entry.parsed.remove_tag(gradient_alpha_tag, norm_sel_start, sel_start + shift);
						if (!gradient_alpha_tag_alt.empty())
							shift += entry.parsed.remove_tag(gradient_alpha_tag_alt, norm_sel_start, sel_start + shift);
						if (!alpha_tag_str.empty())
							shift += entry.parsed.remove_tag(alpha_tag_str, norm_sel_start, sel_start + shift);
						shift += entry.parsed.set_tag(gradient_alpha_tag, FormatGradientAlphas(alphas), norm_sel_start, sel_start + shift);
					}

					if (entry.parsed.line == active_line)
						local_active_shift = shift;

					entry.parsed = parsed_line(entry.parsed.line);
				}

				if (!lines_are_valid_utf8()) {
					if (gradient_commit_id != -1)
						c->subsController->Undo();
					restore_original_texts();
					gradient_commit_id = -1;
					return;
				}

				gradient_commit_id = c->ass->Commit(_("set gradient color"), AssFile::COMMIT_DIAG_TEXT, gradient_commit_id, sel.size() == 1 ? *sel.begin() : nullptr);
				if (local_active_shift)
					set_selection_display(sel_start + local_active_shift, sel_start + local_active_shift, active_line->Text.get());
			};

			auto revert_preview = [&]() {
				if (gradient_commit_id != -1) {
					c->subsController->Undo();
					gradient_commit_id = -1;
					for (auto& entry : lines)
						entry.parsed = parsed_line(entry.parsed.line);
					reset_selection();
				}
			};

			auto preview_cb = [&](const VcVaGradientState& state) {
				apply_state(true, state.alpha_touched, state.colors, state.alphas);
			};

			auto result = ShowVcVaGradientDialog(owner, gradient_state, preview_cb, revert_preview);
			if (!result.accepted) {
				revert_preview();
				return;
			}
			if (!result.has_color && !result.has_alpha) {
				revert_preview();
				return;
			}

			apply_state(result.has_color, result.has_alpha, result.colors, result.alphas);
			gradient_commit_id = -1;
		};
		SetShinGradientHandler(gradient_handler);
	}
	else
		SetShinGradientHandler({});

	if (!use_selection_wrap) {
		bool ok = effective_picker(c->parent, initial_color, true, [&](agi::Color new_color) {
			for (auto& line : lines) {
				std::string raw_text = line.parsed.line->Text.get();
				ScopeInfo scope = ComputeScope(raw_text, sel_start);
				int shift = 0;
				int scope_start = scope.scope_start;
				int scope_end = scope.scope_end;
				int insert_pos = scope.insert_pos;

				if (scope.in_t) {
					scope_start = std::max(scope_start, 0);
					auto apply_tag_in_t = [&](const std::vector<std::string>& names, const std::string& value) {
						if (names.empty())
							return;
						int caret_pos = sel_start + shift;
						TInsertionPoint plan;
						if (FindTInsertionPoint(raw_text, caret_pos, names, plan)) {
							if (plan.replace) {
								int old_len = plan.replace_end - plan.replace_start;
								raw_text.replace(plan.replace_start, old_len, value);
								int delta = static_cast<int>(value.size()) - old_len;
								shift += delta;
								if (plan.replace_start <= scope_end)
									scope_end += delta;
								insert_pos = caret_pos + shift;
								return;
							}
							if (!value.empty()) {
								std::string insertion = names.front() + value;
								raw_text.insert(plan.insert_pos, insertion);
								int delta = static_cast<int>(insertion.size());
								shift += delta;
								if (plan.insert_pos <= scope_end)
									scope_end += delta;
								insert_pos = caret_pos + shift;
								return;
							}
						}
						int delta = ReplaceOrInsertInRange(raw_text, scope_start, scope_end, insert_pos, names, value);
						shift += delta;
						scope_end += delta;
						insert_pos += delta;
					};

					std::vector<std::string> tag_names;
					if (tag == "\\c")
						tag_names = {"\\c", "\\1c"};
					else
						tag_names = {tag};
					apply_tag_in_t(tag_names, new_color.GetAssOverrideFormatted());
					if (new_color.a != line.color.a) {
						std::vector<std::string> alpha_names = {alpha};
						apply_tag_in_t(alpha_names, agi::format("&H%02X&", (int)new_color.a));
					}
					line.color.a = new_color.a;
				}
				else {
					parsed_line parsed(line.parsed.line);
					int use_pos = insert_pos;
					int norm_use = normalize_pos(raw_text, use_pos);
					shift += parsed.set_tag(tag, new_color.GetAssOverrideFormatted(), norm_use, use_pos);
					if (new_color.a != line.color.a) {
						shift += parsed.set_tag(alpha, agi::format("&H%02X&", (int)new_color.a), norm_use, use_pos + shift);
						line.color.a = new_color.a;
					}
					raw_text = parsed.line->Text.get();
				}

				if (!IsValidUtf8(raw_text)) {
					restore_original_texts();
					return;
				}

				line.parsed.line->Text = raw_text;

				if (line.parsed.line == active_line)
					active_shift = shift;
			}

			if (!lines_are_valid_utf8()) {
				restore_original_texts();
				return;
			}

			commit_id = c->ass->Commit(_("set color"), AssFile::COMMIT_DIAG_TEXT, commit_id, sel.size() == 1 ? *sel.begin() : nullptr);
			if (active_shift)
				set_selection_display(sel_start + active_shift, sel_start + active_shift, active_line->Text.get());
		});

		if (!ok && commit_id != -1) {
			c->subsController->Undo();
			reset_selection();
		}
		return;
	}

	auto apply_selection_color = [&](const agi::Color& new_color) {
		int start_shift = 0;
		int end_shift = 0;
		for (auto& line : lines) {
			SelectionApplyOptions options;
			options.channel = channel;
			options.use_gradient = false;
			options.add_color_tag = true;
			options.apply_alpha_gradient = false;
			options.new_color_value = new_color.GetAssOverrideFormatted();
			options.color_tag_name = color_tag_name;
			options.gradient_tag_name = gradient_tag_name;
			options.alpha_tag_name = alpha_tag_name;
			options.alpha_gradient_tag_name = gradient_alpha_tag_name;

			SelectionApplyResult res = ApplyColorOrGradientToRange(line.original_text, sel_start, sel_end, options);
			line.parsed.line->Text = res.text;
			if (line.parsed.line == active_line) {
				start_shift = res.shift.start;
				end_shift = res.shift.end;
			}
		}

		if (!lines_are_valid_utf8()) {
			restore_original_texts();
			return;
		}

		commit_id = c->ass->Commit(_("set color"), AssFile::COMMIT_DIAG_TEXT, commit_id, sel.size() == 1 ? *sel.begin() : nullptr);
		if (start_shift || end_shift)
			set_selection_display(sel_start + start_shift, sel_end + end_shift, active_line->Text.get());
	};

	bool ok = effective_picker(c->parent, initial_color, true, [&](agi::Color new_color) {
		apply_selection_color(new_color);
	});

	if (!ok && commit_id != -1) {
		c->subsController->Undo();
		reset_selection();
		return;
	}

	if (gradient_used)
		return;
}

struct edit_color_primary final : public Command {
	CMD_NAME("edit/color/primary")
	CMD_ICON(button_color_one)
	STR_MENU("Primary Color...")
	STR_DISP("Primary Color")
	STR_HELP("Set the primary fill color (\\c) at the cursor position")

	void operator()(agi::Context *c) override {
		show_color_picker(c, &AssStyle::primary, "\\c", "\\1c", "\\1a", GetColorFromUserShin);
	}
};

struct edit_color_secondary final : public Command {
	CMD_NAME("edit/color/secondary")
	CMD_ICON(button_color_two)
	STR_MENU("Secondary Color...")
	STR_DISP("Secondary Color")
	STR_HELP("Set the secondary (karaoke) fill color (\\2c) at the cursor position")

	void operator()(agi::Context *c) override {
		show_color_picker(c, &AssStyle::secondary, "\\2c", "", "\\2a", GetColorFromUserShin);
	}
};

struct edit_color_outline final : public Command {
	CMD_NAME("edit/color/outline")
	CMD_ICON(button_color_three)
	STR_MENU("Outline Color...")
	STR_DISP("Outline Color")
	STR_HELP("Set the outline color (\\3c) at the cursor position")

	void operator()(agi::Context *c) override {
		show_color_picker(c, &AssStyle::outline, "\\3c", "", "\\3a", GetColorFromUserShin);
	}
};

struct edit_color_shadow final : public Command {
	CMD_NAME("edit/color/shadow")
	CMD_ICON(button_color_four)
	STR_MENU("Shadow Color...")
	STR_DISP("Shadow Color")
	STR_HELP("Set the shadow color (\\4c) at the cursor position")

	void operator()(agi::Context *c) override {
		show_color_picker(c, &AssStyle::shadow, "\\4c", "", "\\4a", GetColorFromUserShin);
	}
};

struct edit_style_bold final : public Command {
	CMD_NAME("edit/style/bold")
	CMD_ICON(button_bold)
	STR_MENU("Toggle Bold")
	STR_DISP("Toggle Bold")
	STR_HELP("Toggle bold (\\b) for the current selection or at the current cursor position")

	void operator()(agi::Context *c) override {
		toggle_override_tag(c, &AssStyle::bold, "\\b", _("toggle bold"));
	}
};

struct edit_style_italic final : public Command {
	CMD_NAME("edit/style/italic")
	CMD_ICON(button_italics)
	STR_MENU("Toggle Italics")
	STR_DISP("Toggle Italics")
	STR_HELP("Toggle italics (\\i) for the current selection or at the current cursor position")

	void operator()(agi::Context *c) override {
		toggle_override_tag(c, &AssStyle::italic, "\\i", _("toggle italic"));
	}
};

struct edit_style_underline final : public Command {
	CMD_NAME("edit/style/underline")
	CMD_ICON(button_underline)
	STR_MENU("Toggle Underline")
	STR_DISP("Toggle Underline")
	STR_HELP("Toggle underline (\\u) for the current selection or at the current cursor position")

	void operator()(agi::Context *c) override {
		toggle_override_tag(c, &AssStyle::underline, "\\u", _("toggle underline"));
	}
};

struct edit_style_strikeout final : public Command {
	CMD_NAME("edit/style/strikeout")
	CMD_ICON(button_strikeout)
	STR_MENU("Toggle Strikeout")
	STR_DISP("Toggle Strikeout")
	STR_HELP("Toggle strikeout (\\s) for the current selection or at the current cursor position")

	void operator()(agi::Context *c) override {
		toggle_override_tag(c, &AssStyle::strikeout, "\\s", _("toggle strikeout"));
	}
};

struct edit_font final : public Command {
	CMD_NAME("edit/font")
	CMD_ICON(button_fontname)
	STR_MENU("Font Face...")
	STR_DISP("Font Face")
	STR_HELP("Select a font face and size")

	void operator()(agi::Context *c) override {
		const parsed_line active(c->selectionController->GetActiveLine());
		if (!active.line) return;

		const int disp_sel_start = c->textSelectionController->GetSelectionStart();
		const int disp_sel_end = c->textSelectionController->GetSelectionEnd();
		int sel_start = disp_sel_start;
		int sel_end = disp_sel_end;
		const bool better_view = c->subsEditBox && c->subsEditBox->BetterViewEnabled();
		std::string raw_before = active.line->Text.get();
		if (better_view && c->subsEditBox)
			c->subsEditBox->MapDisplayRangeToRaw(disp_sel_start, disp_sel_end, raw_before, sel_start, sel_end);

		auto reset_selection = [&]() {
			if (better_view && c->subsEditBox)
				c->textSelectionController->SetSelection(disp_sel_start, disp_sel_end);
			else
				c->textSelectionController->SetSelection(sel_start, sel_end);
		};

		const int insertion_point = normalize_pos(active.line->Text, sel_end);

		auto font_for_line = [&](parsed_line const& line) -> wxFont {
			const int blockn = line.block_at_pos(insertion_point);

			const AssStyle *style = c->ass->GetStyle(line.line->Style);
			const AssStyle default_style;
			if (!style)
				style = &default_style;

			return wxFont(
				line.get_value(blockn, (int)style->fontsize, "\\fs"),
				wxFONTFAMILY_DEFAULT,
				line.get_value(blockn, style->italic, "\\i") ? wxFONTSTYLE_ITALIC : wxFONTSTYLE_NORMAL,
				line.get_value(blockn, style->bold, "\\b") ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL,
				line.get_value(blockn, style->underline, "\\u"),
				to_wx(line.get_value(blockn, style->font, "\\fn")));
		};

		const wxFont initial = font_for_line(active);
		const wxFont font = wxGetFontFromUser(c->parent, initial);
		if (!font.Ok() || font == initial) {
			reset_selection();
			return;
		}

		update_lines_mapped(c, _("set font"), sel_start, sel_end, better_view, [&](AssDialogue *line, int sel_start_raw, int sel_end_raw, int norm_sel_start, int norm_sel_end) {
			parsed_line parsed(line);
			const wxFont startfont = font_for_line(parsed);
			int shift = 0;
			auto do_set_tag = [&](const char *tag_name, std::string const& value) {
				shift += parsed.set_tag(tag_name, value, norm_sel_start, sel_start_raw + shift);
			};

			if (font.GetFaceName() != startfont.GetFaceName())
				do_set_tag("\\fn", from_wx(font.GetFaceName()));
			if (font.GetPointSize() != startfont.GetPointSize())
				do_set_tag("\\fs", std::to_string(font.GetPointSize()));
			if (font.GetWeight() != startfont.GetWeight())
				do_set_tag("\\b", std::to_string(font.GetWeight() == wxFONTWEIGHT_BOLD));
			if (font.GetStyle() != startfont.GetStyle())
				do_set_tag("\\i", std::to_string(font.GetStyle() == wxFONTSTYLE_ITALIC));
			if (font.GetUnderlined() != startfont.GetUnderlined())
				do_set_tag("\\i", std::to_string(font.GetUnderlined()));

			return shift;
		});
	}
};

struct edit_find_replace final : public Command {
	CMD_NAME("edit/find_replace")
	CMD_ICON(find_replace_menu)
	STR_MENU("Find and R&eplace...")
	STR_DISP("Find and Replace")
	STR_HELP("Find and replace words in subtitles")

	void operator()(agi::Context *c) override {
		c->videoController->Stop();
		ShowSearchReplaceDialog(c, true);
	}
};

static void copy_lines(agi::Context *c) {
	SetClipboard(join(c->selectionController->GetSortedSelection()
		| transformed(static_cast<std::string(*)(AssDialogue*)>([](AssDialogue *d) { return d->GetEntryData(); })),
		"\r\n"));
}

static void delete_lines(agi::Context *c, wxString const& commit_message) {
	auto const& sel = c->selectionController->GetSelectedSet();

	// Find a line near the active line not being deleted to make the new active line
	AssDialogue *pre_sel = nullptr;
	AssDialogue *post_sel = nullptr;
	bool hit_selection = false;

	for (auto& diag : c->ass->Events) {
		if (sel.count(&diag))
			hit_selection = true;
		else if (hit_selection && !post_sel) {
			post_sel = &diag;
			break;
		}
		else
			pre_sel = &diag;
	}

	// Remove the selected lines, but defer the deletion until after we select
	// different lines. We can't just change the selection first because we may
	// need to create a new dialogue line for it, and we can't select dialogue
	// lines until after they're committed.
	std::vector<std::unique_ptr<AssDialogue>> to_delete;
	c->ass->Events.remove_and_dispose_if([&sel](AssDialogue const& e) {
		return sel.count(const_cast<AssDialogue *>(&e));
	}, [&](AssDialogue *e) {
		to_delete.emplace_back(e);
	});

	AssDialogue *new_active = post_sel;
	if (!new_active)
		new_active = pre_sel;
	// If we didn't get a new active line then we just deleted all the dialogue
	// lines, so make a new one
	if (!new_active) {
		new_active = new AssDialogue;
		c->ass->Events.push_back(*new_active);
	}

	c->ass->Commit(commit_message, AssFile::COMMIT_DIAG_ADDREM);
	c->selectionController->SetSelectionAndActive({ new_active }, new_active);
}

struct edit_line_copy final : public validate_sel_nonempty {
	CMD_NAME("edit/line/copy")
	CMD_ICON(copy_button)
	STR_MENU("&Copy Lines")
	STR_DISP("Copy Lines")
	STR_HELP("Copy subtitles to the clipboard")

	void operator()(agi::Context *c) override {
		// Ideally we'd let the control's keydown handler run and only deal
		// with the events not processed by it, but that doesn't seem to be
		// possible with how wx implements key event handling - the native
		// platform processing is evoked only if the wx event is unprocessed,
		// and there's no way to do something if the native platform code leaves
		// it unprocessed

		if (wxTextEntryBase *ctrl = dynamic_cast<wxTextEntryBase*>(c->parent->FindFocus()))
			ctrl->Copy();
		else {
			copy_lines(c);
		}
	}
};

struct edit_line_cut: public validate_sel_nonempty {
	CMD_NAME("edit/line/cut")
	CMD_ICON(cut_button)
	STR_MENU("Cu&t Lines")
	STR_DISP("Cut Lines")
	STR_HELP("Cut subtitles")

	void operator()(agi::Context *c) override {
		if (wxTextEntryBase *ctrl = dynamic_cast<wxTextEntryBase*>(c->parent->FindFocus()))
			ctrl->Cut();
		else {
			copy_lines(c);
			delete_lines(c, _("cut lines"));
		}
	}
};

struct edit_line_delete final : public validate_sel_nonempty {
	CMD_NAME("edit/line/delete")
	CMD_ICON(delete_button)
	STR_MENU("De&lete Lines")
	STR_DISP("Delete Lines")
	STR_HELP("Delete currently selected lines")

	void operator()(agi::Context *c) override {
		delete_lines(c, _("delete lines"));
	}
};

// Toggle the comment flag for the current selection (or the active line when nothing is selected).
struct edit_line_comment final : public Command {
	CMD_NAME("edit/line/comment")
	STR_MENU("Comment/Uncomment Lines")
	STR_DISP("Comment/Uncomment Lines")
	STR_HELP("Toggle comment state for selected lines")
	CMD_TYPE(COMMAND_VALIDATE)

	bool Validate(const agi::Context *c) override {
		return c->selectionController->GetActiveLine() != nullptr;
	}

	void operator()(agi::Context *c) override {
		auto const& sel = c->selectionController->GetSelectedSet();
		std::vector<AssDialogue*> lines(sel.begin(), sel.end());
		if (lines.empty()) {
			if (auto *active = c->selectionController->GetActiveLine())
				lines.push_back(active);
		}
		if (lines.empty())
			return;

		bool all_commented = std::all_of(lines.begin(), lines.end(), [](AssDialogue const *line) {
			return line->Comment;
		});
		bool new_state = !all_commented;

		for (auto *line : lines)
			line->Comment = new_state;

		c->ass->Commit(_("toggle comment"), AssFile::COMMIT_DIAG_META);
	}
};

// Flip the comment flag for each selected line individually.
struct edit_line_comment_invert final : public Command {
	CMD_NAME("edit/line/comment/invert")
	STR_MENU("Invert Comment Flags")
	STR_DISP("Invert Comment Flags")
	STR_HELP("Invert comment state for each selected line")
	CMD_TYPE(COMMAND_VALIDATE)

	bool Validate(const agi::Context *c) override {
		return c->selectionController->GetActiveLine() != nullptr;
	}

	void operator()(agi::Context *c) override {
		auto const& sel = c->selectionController->GetSelectedSet();
		std::vector<AssDialogue*> lines(sel.begin(), sel.end());
		if (lines.empty()) {
			if (auto *active = c->selectionController->GetActiveLine())
				lines.push_back(active);
		}
		if (lines.empty())
			return;

		for (auto *line : lines)
			line->Comment = !line->Comment;

		c->ass->Commit(_("invert comments"), AssFile::COMMIT_DIAG_META);
	}
};

static void duplicate_lines(agi::Context *c, int shift) {
	auto const& sel = c->selectionController->GetSelectedSet();
	auto in_selection = [&](AssDialogue const& d) { return sel.count(const_cast<AssDialogue *>(&d)); };

	Selection new_sel;
	AssDialogue *new_active = nullptr;

	auto start = c->ass->Events.begin();
	auto end = c->ass->Events.end();
	while (start != end) {
		// Find the first line in the selection
		start = std::find_if(start, end, in_selection);
		if (start == end) break;

		// And the last line in this contiguous selection
		auto insert_pos = std::find_if_not(start, end, in_selection);
		auto last = std::prev(insert_pos);

		// Duplicate each of the selected lines, inserting them in a block
		// after the selected block
		do {
			auto old_diag = &*start;
			auto new_diag = new AssDialogue(*old_diag);

			c->ass->Events.insert(insert_pos, *new_diag);
			new_sel.insert(new_diag);
			if (!new_active)
				new_active = new_diag;

			if (shift) {
				int cur_frame = c->videoController->GetFrameN();
				int old_start = c->videoController->FrameAtTime(new_diag->Start, agi::vfr::START);
				int old_end = c->videoController->FrameAtTime(new_diag->End, agi::vfr::END);

				// If the current frame isn't within the range of the line then
				// splitting doesn't make any sense, so instead just duplicate
				// the line and set the new one to just this frame
				if (cur_frame < old_start || cur_frame > old_end) {
					new_diag->Start = c->videoController->TimeAtFrame(cur_frame, agi::vfr::START);
					new_diag->End = c->videoController->TimeAtFrame(cur_frame, agi::vfr::END);
				}
				/// @todo This does dumb things when old_start == old_end
				else if (shift < 0) {
					old_diag->End = c->videoController->TimeAtFrame(cur_frame - 1, agi::vfr::END);
					new_diag->Start = c->videoController->TimeAtFrame(cur_frame, agi::vfr::START);
				}
				else {
					old_diag->End = c->videoController->TimeAtFrame(cur_frame, agi::vfr::END);
					new_diag->Start = c->videoController->TimeAtFrame(cur_frame + 1, agi::vfr::START);
				}

				/// @todo also split \t and \move?
			}
		} while (start++ != last);

		// Skip over the lines we just made
		start = insert_pos;
	}

	if (new_sel.empty()) return;

	c->ass->Commit(shift ? _("split") : _("duplicate lines"), AssFile::COMMIT_DIAG_ADDREM);

	c->selectionController->SetSelectionAndActive(std::move(new_sel), new_active);
}

struct edit_line_duplicate final : public validate_sel_nonempty {
	CMD_NAME("edit/line/duplicate")
	STR_MENU("&Duplicate Lines")
	STR_DISP("Duplicate Lines")
	STR_HELP("Duplicate the selected lines")

	void operator()(agi::Context *c) override {
		duplicate_lines(c, 0);
	}
};

struct edit_line_duplicate_shift final : public validate_video_and_sel_nonempty {
	CMD_NAME("edit/line/split/after")
	STR_MENU("Split lines after current frame")
	STR_DISP("Split lines after current frame")
	STR_HELP("Split the current line into a line which ends on the current frame and a line which starts on the next frame")
	CMD_TYPE(COMMAND_VALIDATE)

	void operator()(agi::Context *c) override {
		duplicate_lines(c, 1);
	}
};

struct edit_line_duplicate_shift_back final : public validate_video_and_sel_nonempty {
	CMD_NAME("edit/line/split/before")
	STR_MENU("Split lines before current frame")
	STR_DISP("Split lines before current frame")
	STR_HELP("Split the current line into a line which ends on the previous frame and a line which starts on the current frame")
	CMD_TYPE(COMMAND_VALIDATE)

	void operator()(agi::Context *c) override {
		duplicate_lines(c, -1);
	}
};

static void combine_lines(agi::Context *c, void (*combiner)(AssDialogue *, AssDialogue *), wxString const& message) {
	auto sel = c->selectionController->GetSortedSelection();

	AssDialogue *first = sel[0];
	combiner(first, nullptr);
	for (size_t i = 1; i < sel.size(); ++i) {
		combiner(first, sel[i]);
		first->End = std::max(first->End, sel[i]->End);
		delete sel[i];
	}

	c->selectionController->SetSelectionAndActive({first}, first);

	c->ass->Commit(message, AssFile::COMMIT_DIAG_ADDREM | AssFile::COMMIT_DIAG_FULL);
}

static void combine_karaoke(AssDialogue *first, AssDialogue *second) {
	if (second)
		first->Text = first->Text.get() + "{\\k" + std::to_string((second->End - second->Start) / 10) + "}" + second->Text.get();
	else
		first->Text = "{\\k" + std::to_string((first->End - first->Start) / 10) + "}" + first->Text.get();
}

static void combine_concat(AssDialogue *first, AssDialogue *second) {
	if (second)
		first->Text = first->Text.get() + " " + second->Text.get();
}

static void combine_drop(AssDialogue *, AssDialogue *) { }

static AssDialogue *get_adjacent_line(agi::Context const *c, AssDialogue *line, int step) {
	if (!line || step == 0) return line;
	auto it = c->ass->iterator_to(*line);
	if (step > 0) {
		while (step-- > 0) {
			++it;
			if (it == c->ass->Events.end())
				return nullptr;
		}
	}
	else {
		while (step++ < 0) {
			if (it == c->ass->Events.begin())
				return nullptr;
			--it;
		}
	}
	return &*it;
}

static std::string build_joined_text(std::string const& first, std::string const& second) {
	std::string lhs = boost::trim_copy(first);
	std::string rhs = boost::trim_copy(second);
	if (lhs.empty()) return rhs;
	if (rhs.empty()) return lhs;
	return lhs + ' ' + rhs;
}

struct edit_line_join_as_karaoke final : public validate_sel_multiple {
	CMD_NAME("edit/line/join/as_karaoke")
	STR_MENU("As &Karaoke")
	STR_DISP("As Karaoke")
	STR_HELP("Join selected lines in a single one, as karaoke")

	void operator()(agi::Context *c) override {
		combine_lines(c, combine_karaoke, _("join as karaoke"));
	}
};

struct edit_line_join_concatenate final : public validate_sel_multiple {
	CMD_NAME("edit/line/join/concatenate")
	STR_MENU("&Concatenate")
	STR_DISP("Concatenate")
	STR_HELP("Join selected lines in a single one, concatenating text together")

	void operator()(agi::Context *c) override {
		combine_lines(c, combine_concat, _("join lines"));
	}
};

struct edit_line_join_keep_first final : public validate_sel_multiple {
	CMD_NAME("edit/line/join/keep_first")
	STR_MENU("Keep &First")
	STR_DISP("Keep First")
	STR_HELP("Join selected lines in a single one, keeping text of first and discarding remaining")

	void operator()(agi::Context *c) override {
		combine_lines(c, combine_drop, _("join lines"));
	}
};

struct edit_line_join_next final : public Command {
	CMD_NAME("edit/line/join/next")
	STR_MENU("Join Next (normal)")
	STR_DISP("join next")
	STR_HELP("Join the current line with the next line, merging text and timing and removing the next line")
	CMD_TYPE(COMMAND_VALIDATE)

	bool Validate(const agi::Context *c) override {
		return get_adjacent_line(c, c->selectionController->GetActiveLine(), 1) != nullptr;
	}

	void operator()(agi::Context *c) override {
		AssDialogue *line = c->selectionController->GetActiveLine();
		if (!line) return;
		AssDialogue *next = get_adjacent_line(c, line, 1);
		if (!next) return;

		std::string const current_text = line->Text.get();
		std::string const next_text = next->Text.get();
		std::string const joined_text = build_joined_text(current_text, next_text);

		line->Start = std::min(line->Start, next->Start);
		line->End = std::max(line->End, next->End);
		line->Text = joined_text;

		std::string base_original = c->initialLineState->GetInitialText();
		if (base_original.empty())
			base_original = current_text;
		std::string const joined_original = build_joined_text(base_original, next_text);

		Selection new_sel = c->selectionController->GetSelectedSet();
		new_sel.erase(next);
		new_sel.insert(line);

		auto it = c->ass->iterator_to(*next);
		c->ass->Events.erase(it);
		delete next;

		c->selectionController->SetSelectionAndActive(new_sel, line);

		c->initialLineState->SetInitialText(line, joined_original);

		c->ass->Commit(_("join lines"), AssFile::COMMIT_DIAG_ADDREM | AssFile::COMMIT_DIAG_FULL);
	}
};

struct edit_line_join_next_translatormode final : public Command {
	CMD_NAME("edit/line/join/next/translatormode")
	STR_MENU("Join Next")
	STR_DISP("Join Next")
	STR_HELP("Join the current line with the next line, merging timing and storing merged text in the original panel")
	CMD_TYPE(COMMAND_VALIDATE)

	bool Validate(const agi::Context *c) override {
		return get_adjacent_line(c, c->selectionController->GetActiveLine(), 1) != nullptr;
	}

	void operator()(agi::Context *c) override {
		AssDialogue *line = c->selectionController->GetActiveLine();
		if (!line) return;
		AssDialogue *next = get_adjacent_line(c, line, 1);
		if (!next) return;

		line->Start = std::min(line->Start, next->Start);
		line->End = std::max(line->End, next->End);

		std::string base_original = c->initialLineState->GetInitialText();
		if (base_original.empty())
			base_original = line->Text.get();
		std::string joined_original = build_joined_text(base_original, next->Text.get());

		Selection new_sel = c->selectionController->GetSelectedSet();
		new_sel.erase(next);
		new_sel.insert(line);

		auto it = c->ass->iterator_to(*next);
		c->ass->Events.erase(it);
		delete next;

		c->selectionController->SetSelectionAndActive(new_sel, line);

		c->initialLineState->SetInitialText(line, joined_original);

		c->ass->Commit(_("join lines"), AssFile::COMMIT_DIAG_ADDREM | AssFile::COMMIT_DIAG_FULL);
	}
};


struct edit_line_join_next_normal final : public Command {
	CMD_NAME("edit/line/join/next")
	STR_MENU("Join Next (normal)")
	STR_DISP("Join Next (normal)")
	STR_HELP("Join the current line with the next line, merging text and timing and removing the next line")
	CMD_TYPE(COMMAND_VALIDATE)

	bool Validate(const agi::Context *c) override {
		return get_adjacent_line(c, c->selectionController->GetActiveLine(), 1) != nullptr;
	}

	void operator()(agi::Context *c) override {
		AssDialogue *line = c->selectionController->GetActiveLine();
		if (!line) return;
		AssDialogue *next = get_adjacent_line(c, line, 1);
		if (!next) return;

		line->Start = std::min(line->Start, next->Start);
		line->End = std::max(line->End, next->End);
		line->Text = build_joined_text(line->Text.get(), next->Text.get());

		Selection new_sel = c->selectionController->GetSelectedSet();
		new_sel.erase(next);
		new_sel.insert(line);

		auto it = c->ass->iterator_to(*next);
		c->ass->Events.erase(it);
		delete next;

		c->selectionController->SetSelectionAndActive(new_sel, line);

		c->initialLineState->SetInitialText(line, line->Text.get());

		c->ass->Commit(_("join lines"), AssFile::COMMIT_DIAG_ADDREM | AssFile::COMMIT_DIAG_FULL);
	}
};

struct edit_line_join_last final : public Command {
	CMD_NAME("edit/line/join/last")
	STR_MENU("Join Previous (normal)")
	STR_DISP("join last")
	STR_HELP("Join the current line with the previous line, merging text and timing and removing the previous line")
	CMD_TYPE(COMMAND_VALIDATE)

	bool Validate(const agi::Context *c) override {
		return get_adjacent_line(c, c->selectionController->GetActiveLine(), -1) != nullptr;
	}

	void operator()(agi::Context *c) override {
		AssDialogue *line = c->selectionController->GetActiveLine();
		if (!line) return;
		AssDialogue *prev = get_adjacent_line(c, line, -1);
		if (!prev) return;

		line->Start = std::min(line->Start, prev->Start);
		line->End = std::max(line->End, prev->End);
		line->Text = build_joined_text(prev->Text.get(), line->Text.get());

		Selection new_sel = c->selectionController->GetSelectedSet();
		new_sel.erase(prev);
		new_sel.insert(line);

		auto it = c->ass->iterator_to(*prev);
		c->ass->Events.erase(it);
		delete prev;

		c->selectionController->SetSelectionAndActive(new_sel, line);

		c->initialLineState->SetInitialText(line, line->Text.get());

		c->ass->Commit(_("join lines"), AssFile::COMMIT_DIAG_ADDREM | AssFile::COMMIT_DIAG_FULL);
	}
};

static bool try_paste_lines(agi::Context *c) {
	std::string data = GetClipboard();
	boost::trim_left(data);
	if (!boost::starts_with(data, "Dialogue:")) return false;

	EntryList<AssDialogue> parsed;
	boost::char_separator<char> sep("\r\n");
	for (auto curdata : boost::tokenizer<boost::char_separator<char>>(data, sep)) {
		boost::trim(curdata);
		try {
			parsed.push_back(*new AssDialogue(curdata));
		}
		catch (...) {
			parsed.clear_and_dispose([](AssDialogue *e) { delete e; });
			return false;
		}
	}

	AssDialogue *new_active = &*parsed.begin();
	Selection new_selection;
	for (auto& line : parsed)
		new_selection.insert(&line);

	auto pos = c->ass->iterator_to(*c->selectionController->GetActiveLine());
	c->ass->Events.splice(pos, parsed, parsed.begin(), parsed.end());
	c->ass->Commit(_("paste"), AssFile::COMMIT_DIAG_ADDREM);
	c->selectionController->SetSelectionAndActive(std::move(new_selection), new_active);

	return true;
}

struct edit_line_paste final : public Command {
	CMD_NAME("edit/line/paste")
	CMD_ICON(paste_button)
	STR_MENU("&Paste Lines")
	STR_DISP("Paste Lines")
	STR_HELP("Paste subtitles")
	CMD_TYPE(COMMAND_VALIDATE)

	bool Validate(const agi::Context *) override {
		bool can_paste = false;
		if (wxTheClipboard->Open()) {
			can_paste = wxTheClipboard->IsSupported(wxDF_TEXT) || wxTheClipboard->IsSupported(wxDF_UNICODETEXT);
			wxTheClipboard->Close();
		}
		return can_paste;
	}

	void operator()(agi::Context *c) override {
		if (wxTextEntryBase *ctrl = dynamic_cast<wxTextEntryBase*>(c->parent->FindFocus())) {
			if (!try_paste_lines(c))
				ctrl->Paste();
		}
		else {
			auto pos = c->ass->iterator_to(*c->selectionController->GetActiveLine());
			paste_lines(c, false, [=](AssDialogue *new_line) -> AssDialogue * {
				c->ass->Events.insert(pos, *new_line);
				return new_line;
			});
		}
	}
};

struct edit_line_paste_over final : public Command {
	CMD_NAME("edit/line/paste/over")
	STR_MENU("Paste Lines &Over...")
	STR_DISP("Paste Lines Over")
	STR_HELP("Paste subtitles over others")
	CMD_TYPE(COMMAND_VALIDATE)

	bool Validate(const agi::Context *c) override {
		bool can_paste = !c->selectionController->GetSelectedSet().empty();
		if (can_paste && wxTheClipboard->Open()) {
			can_paste = wxTheClipboard->IsSupported(wxDF_TEXT) || wxTheClipboard->IsSupported(wxDF_UNICODETEXT);
			wxTheClipboard->Close();
		}
		return can_paste;
	}

	void operator()(agi::Context *c) override {
		auto const& sel = c->selectionController->GetSelectedSet();
		std::vector<bool> pasteOverOptions;

		// Only one line selected, so paste over downwards from the active line
		if (sel.size() < 2) {
			auto pos = c->ass->iterator_to(*c->selectionController->GetActiveLine());

			paste_lines(c, true, [&](AssDialogue *new_line) -> AssDialogue * {
				std::unique_ptr<AssDialogue> deleter(new_line);
				if (pos == c->ass->Events.end()) return nullptr;

				AssDialogue *ret = paste_over(c->parent, pasteOverOptions, new_line, &*pos);
				if (ret)
					++pos;
				return ret;
			});
		}
		else {
			// Multiple lines selected, so paste over the selection
			auto sorted_selection = c->selectionController->GetSortedSelection();
			auto pos = begin(sorted_selection);
			paste_lines(c, true, [&](AssDialogue *new_line) -> AssDialogue * {
				std::unique_ptr<AssDialogue> deleter(new_line);
				if (pos == end(sorted_selection)) return nullptr;

				AssDialogue *ret = paste_over(c->parent, pasteOverOptions, new_line, *pos);
				if (ret) ++pos;
				return ret;
			});
		}
	}
};

namespace {
std::string trim_text(std::string text) {
	boost::regex start(R"(^( |	|\\[nNh])+)");
	boost::regex end(R"(( |	|\\[nNh])+$)");

	text = regex_replace(text, start, "", boost::format_first_only);
	text = regex_replace(text, end, "", boost::format_first_only);
	return text;
}

void expand_times(AssDialogue *src, AssDialogue *dst) {
	dst->Start = std::min(dst->Start, src->Start);
	dst->End = std::max(dst->End, src->End);
}

bool check_start(AssDialogue *d1, AssDialogue *d2) {
	if (boost::starts_with(d1->Text.get(), d2->Text.get())) {
		d1->Text = trim_text(d1->Text.get().substr(d2->Text.get().size()));
		expand_times(d1, d2);
		return true;
	}
	return false;
}

bool check_end(AssDialogue *d1, AssDialogue *d2) {
	if (boost::ends_with(d1->Text.get(), d2->Text.get())) {
		d1->Text = trim_text(d1->Text.get().substr(0, d1->Text.get().size() - d2->Text.get().size()));
		expand_times(d1, d2);
		return true;
	}
	return false;
}

}

struct edit_line_recombine final : public validate_sel_multiple {
	CMD_NAME("edit/line/recombine")
	STR_MENU("Recom&bine Lines")
	STR_DISP("Recombine Lines")
	STR_HELP("Recombine subtitles which have been split and merged")

	void operator()(agi::Context *c) override {
		auto const& sel_set = c->selectionController->GetSelectedSet();
		if (sel_set.size() < 2) return;

		auto active_line = c->selectionController->GetActiveLine();

		std::vector<AssDialogue*> sel(sel_set.begin(), sel_set.end());
		boost::sort(sel, [](const AssDialogue *a, const AssDialogue *b) {
			return a->Start < b->Start;
		});

		for (auto &diag : sel)
			diag->Text = trim_text(diag->Text);

		auto end = sel.end() - 1;
		for (auto cur = sel.begin(); cur != end; ++cur) {
			auto d1 = *cur;
			auto d2 = cur + 1;

			// 1, 1+2 (or 2+1), 2 gets turned into 1, 2, 2 so kill the duplicate
			if (d1->Text == (*d2)->Text) {
				expand_times(d1, *d2);
				delete d1;
				continue;
			}

			// 1, 1+2, 1 turns into 1, 2, [empty]
			if (d1->Text.get().empty()) {
				delete d1;
				continue;
			}

			// If d2 is the last line in the selection it'll never hit the above test
			if (d2 == end && (*d2)->Text.get().empty()) {
				delete *d2;
				continue;
			}

			// 1, 1+2
			while (d2 <= end && check_start(*d2, d1))
				++d2;

			// 1, 2+1
			while (d2 <= end && check_end(*d2, d1))
				++d2;

			// 1+2, 2
			while (d2 <= end && check_end(d1, *d2))
				++d2;

			// 2+1, 2
			while (d2 <= end && check_start(d1, *d2))
				++d2;
		}

		// Remove now non-existent lines from the selection
		Selection lines, new_sel;
		boost::copy(c->ass->Events | agi::address_of, inserter(lines, lines.begin()));
		boost::set_intersection(lines, sel_set, inserter(new_sel, new_sel.begin()));

		if (new_sel.empty())
			new_sel.insert(*lines.begin());

		// Restore selection
		if (!new_sel.count(active_line))
			active_line = *new_sel.begin();
		c->selectionController->SetSelectionAndActive(std::move(new_sel), active_line);

		c->ass->Commit(_("combining"), AssFile::COMMIT_DIAG_ADDREM | AssFile::COMMIT_DIAG_FULL);
	}
};

struct edit_line_split_by_karaoke final : public validate_sel_nonempty {
	CMD_NAME("edit/line/split/by_karaoke")
	STR_MENU("Split Lines (by karaoke)")
	STR_DISP("Split Lines (by karaoke)")
	STR_HELP("Use karaoke timing to split line into multiple smaller lines")

	void operator()(agi::Context *c) override {
		auto sel = c->selectionController->GetSortedSelection();
		if (sel.empty()) return;

		Selection new_sel;
		AssKaraoke kara;

		std::vector<std::unique_ptr<AssDialogue>> to_delete;
		for (auto line : sel) {
			kara.SetLine(line);

			// If there aren't at least two tags there's nothing to split
			if (kara.size() < 2) continue;

			for (auto const& syl : kara) {
				auto new_line = new AssDialogue(*line);

				new_line->Start = syl.start_time;
				new_line->End = syl.start_time + syl.duration;
				new_line->Text = syl.GetText(false);

				c->ass->Events.insert(c->ass->iterator_to(*line), *new_line);

				new_sel.insert(new_line);
			}

			c->ass->Events.erase(c->ass->iterator_to(*line));
			to_delete.emplace_back(line);
		}

		if (to_delete.empty()) return;

		c->ass->Commit(_("splitting"), AssFile::COMMIT_DIAG_ADDREM | AssFile::COMMIT_DIAG_FULL);

		AssDialogue *new_active = c->selectionController->GetActiveLine();
		if (!new_sel.count(c->selectionController->GetActiveLine()))
			new_active = *new_sel.begin();
		c->selectionController->SetSelectionAndActive(std::move(new_sel), new_active);
	}
};

void split_lines(agi::Context *c, AssDialogue *&n1, AssDialogue *&n2) {
	int pos = c->textSelectionController->GetSelectionStart();

	n1 = c->selectionController->GetActiveLine();
	n2 = new AssDialogue(*n1);
	c->ass->Events.insert(++c->ass->iterator_to(*n1), *n2);

	std::string orig = n1->Text;
	n1->Text = boost::trim_right_copy(orig.substr(0, pos));
	n2->Text = boost::trim_left_copy(orig.substr(pos));
}

template<typename Func>
void split_lines(agi::Context *c, Func&& set_time) {
	AssDialogue *n1, *n2;
	split_lines(c, n1, n2);
	set_time(n1, n2);

	c->ass->Commit(_("split"), AssFile::COMMIT_DIAG_ADDREM | AssFile::COMMIT_DIAG_FULL);
}

struct edit_line_split_estimate final : public validate_video_and_sel_nonempty {
	CMD_NAME("edit/line/split/estimate")
	STR_MENU("Split at cursor (estimate times)")
	STR_DISP("Split at cursor (estimate times)")
	STR_HELP("Split the current line at the cursor, dividing the original line's duration between the new ones")

	void operator()(agi::Context *c) override {
		split_lines(c, [](AssDialogue *n1, AssDialogue *n2) {
			size_t len = n1->Text.get().size() + n2->Text.get().size();
			if (!len) return;
			double splitPos = double(n1->Text.get().size()) / len;
			n2->Start = n1->End = (int)((n1->End - n1->Start) * splitPos) + n1->Start;
		});
	}
};

struct edit_line_split_preserve final : public validate_sel_nonempty {
	CMD_NAME("edit/line/split/preserve")
	STR_MENU("Split at cursor (preserve times)")
	STR_DISP("Split at cursor (preserve times)")
	STR_HELP("Split the current line at the cursor, setting both lines to the original line's times")

	void operator()(agi::Context *c) override {
		split_lines(c, [](AssDialogue *, AssDialogue *) { });
	}
};

struct edit_line_split_video final : public validate_video_and_sel_nonempty {
	CMD_NAME("edit/line/split/video")
	STR_MENU("Split at cursor (at video frame)")
	STR_DISP("Split at cursor (at video frame)")
	STR_HELP("Split the current line at the cursor, dividing the line's duration at the current video frame")

	void operator()(agi::Context *c) override {
		split_lines(c, [&](AssDialogue *n1, AssDialogue *n2) {
			int cur_frame = mid(
				c->videoController->FrameAtTime(n1->Start, agi::vfr::START),
				c->videoController->GetFrameN(),
				c->videoController->FrameAtTime(n1->End, agi::vfr::END));
			n1->End = n2->Start = c->videoController->TimeAtFrame(cur_frame, agi::vfr::END);
		});
	}
};

struct edit_redo final : public Command {
	CMD_NAME("edit/redo")
	CMD_ICON(redo_button)
	STR_HELP("Redo last undone action")
	CMD_TYPE(COMMAND_VALIDATE | COMMAND_DYNAMIC_NAME)

	wxString StrMenu(const agi::Context *c) const override {
		return c->subsController->IsRedoStackEmpty() ?
			_("Nothing to &redo") :
			fmt_tl("&Redo %s", c->subsController->GetRedoDescription());
	}
	wxString StrDisplay(const agi::Context *c) const override {
		return c->subsController->IsRedoStackEmpty() ?
			_("Nothing to redo") :
			fmt_tl("Redo %s", c->subsController->GetRedoDescription());
	}

	bool Validate(const agi::Context *c) override {
		return !c->subsController->IsRedoStackEmpty();
	}

	void operator()(agi::Context *c) override {
		c->subsController->Redo();
	}
};

struct edit_undo final : public Command {
	CMD_NAME("edit/undo")
	CMD_ICON(undo_button)
	STR_HELP("Undo last action")
	CMD_TYPE(COMMAND_VALIDATE | COMMAND_DYNAMIC_NAME)

	wxString StrMenu(const agi::Context *c) const override {
		return c->subsController->IsUndoStackEmpty() ?
			_("Nothing to &undo") :
			fmt_tl("&Undo %s", c->subsController->GetUndoDescription());
	}
	wxString StrDisplay(const agi::Context *c) const override {
		return c->subsController->IsUndoStackEmpty() ?
			_("Nothing to undo") :
			fmt_tl("Undo %s", c->subsController->GetUndoDescription());
	}

	bool Validate(const agi::Context *c) override {
		return !c->subsController->IsUndoStackEmpty();
	}

	void operator()(agi::Context *c) override {
		c->subsController->Undo();
	}
};

struct edit_revert final : public Command {
	CMD_NAME("edit/revert")
	STR_DISP("Revert")
	STR_MENU("Revert")
	STR_HELP("Revert the active line to its initial state (shown in the upper editor)")

	void operator()(agi::Context *c) override {
		AssDialogue *line = c->selectionController->GetActiveLine();
		line->Text = c->initialLineState->GetInitialText();
		c->ass->Commit(_("revert line"), AssFile::COMMIT_DIAG_TEXT, -1, line);
	}
};

struct edit_clear final : public Command {
	CMD_NAME("edit/clear")
	STR_DISP("Clear")
	STR_MENU("Clear")
	STR_HELP("Clear the current line's text")

	void operator()(agi::Context *c) override {
		AssDialogue *line = c->selectionController->GetActiveLine();
		line->Text = "";
		c->ass->Commit(_("clear line"), AssFile::COMMIT_DIAG_TEXT, -1, line);
	}
};

std::string get_text(AssDialogueBlock &d) { return d.GetText(); }
struct edit_clear_text final : public Command {
	CMD_NAME("edit/clear/text")
	STR_DISP("Clear Text")
	STR_MENU("Clear Text")
	STR_HELP("Clear the current line's text, leaving override tags")

	void operator()(agi::Context *c) override {
		AssDialogue *line = c->selectionController->GetActiveLine();
		auto blocks = line->ParseTags();
		line->Text = join(blocks
			| indirected
			| filtered([](AssDialogueBlock const& b) { return b.GetType() != AssBlockType::PLAIN; })
			| transformed(get_text),
			"");
		c->ass->Commit(_("clear line"), AssFile::COMMIT_DIAG_TEXT, -1, line);
	}
};

struct edit_insert_original final : public Command {
	CMD_NAME("edit/insert_original")
	STR_DISP("Insert Original")
	STR_MENU("Insert Original")
	STR_HELP("Insert the original line text at the cursor")

	void operator()(agi::Context *c) override {
		AssDialogue *line = c->selectionController->GetActiveLine();
		int sel_start = c->textSelectionController->GetSelectionStart();
		int sel_end = c->textSelectionController->GetSelectionEnd();

		line->Text = line->Text.get().substr(0, sel_start) + c->initialLineState->GetInitialText() + line->Text.get().substr(sel_end);
		c->ass->Commit(_("insert original"), AssFile::COMMIT_DIAG_TEXT, -1, line);
	}
};

}

namespace cmd {
	void init_edit() {
		reg(agi::make_unique<edit_color_primary>());
		reg(agi::make_unique<edit_color_secondary>());
		reg(agi::make_unique<edit_color_outline>());
		reg(agi::make_unique<edit_color_shadow>());
		reg(agi::make_unique<edit_font>());
		reg(agi::make_unique<edit_find_replace>());
		reg(agi::make_unique<edit_line_copy>());
		reg(agi::make_unique<edit_line_cut>());
		reg(agi::make_unique<edit_line_delete>());
		reg(agi::make_unique<edit_line_comment>());
		reg(agi::make_unique<edit_line_comment_invert>());
		reg(agi::make_unique<edit_line_duplicate>());
		reg(agi::make_unique<edit_line_duplicate_shift>());
		reg(agi::make_unique<edit_line_duplicate_shift_back>());
		reg(agi::make_unique<edit_line_join_as_karaoke>());
		reg(agi::make_unique<edit_line_join_concatenate>());
		reg(agi::make_unique<edit_line_join_keep_first>());
		reg(agi::make_unique<edit_line_join_next>());
		reg(agi::make_unique<edit_line_join_next_translatormode>());
		reg(agi::make_unique<edit_line_join_last>());
		reg(agi::make_unique<edit_line_paste>());
		reg(agi::make_unique<edit_line_paste_over>());
		reg(agi::make_unique<edit_line_recombine>());
		reg(agi::make_unique<edit_line_split_by_karaoke>());
		reg(agi::make_unique<edit_line_split_estimate>());
		reg(agi::make_unique<edit_line_split_preserve>());
		reg(agi::make_unique<edit_line_split_video>());
		reg(agi::make_unique<edit_style_bold>());
		reg(agi::make_unique<edit_style_italic>());
		reg(agi::make_unique<edit_style_underline>());
		reg(agi::make_unique<edit_style_strikeout>());
		reg(agi::make_unique<edit_redo>());
		reg(agi::make_unique<edit_undo>());
		reg(agi::make_unique<edit_revert>());
		reg(agi::make_unique<edit_insert_original>());
		reg(agi::make_unique<edit_clear>());
		reg(agi::make_unique<edit_clear_text>());
	}
}
