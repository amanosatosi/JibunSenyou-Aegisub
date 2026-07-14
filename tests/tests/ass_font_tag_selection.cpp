// Copyright (c) 2026

#include <main.h>

#include "ass_font_tag_selection.h"

namespace {

void ExpectFontRange(std::string const& text, std::string const& value) {
	auto const value_start = static_cast<int>(text.find(value));
	ASSERT_NE(-1, value_start);
	auto range = FindAssFontNameValueAt(text, value_start);
	ASSERT_TRUE(range);
	EXPECT_EQ(value_start, range.start);
	EXPECT_EQ(value_start + static_cast<int>(value.size()), range.end);
	EXPECT_TRUE(range.Contains(value_start));
	EXPECT_TRUE(range.Contains(range.end - 1));
	EXPECT_FALSE(range.Contains(range.start - 1));
	EXPECT_FALSE(range.Contains(range.end));
}

TEST(ass_font_tag_selection, finds_single_and_multi_word_values) {
	ExpectFontRange("{\\fnArial}", "Arial");
	ExpectFontRange("{\\fnArial Narrow}", "Arial Narrow");
	ExpectFontRange("{\\bord3\\fnNoto Sans CJK JP\\fs50}", "Noto Sans CJK JP");
	ExpectFontRange("{\\fnＤＦＰ平成ゴシック体W7}", "ＤＦＰ平成ゴシック体W7");
	ExpectFontRange("{\\fnFont-Name 2.0}", "Font-Name 2.0");
}

TEST(ass_font_tag_selection, handles_empty_and_incomplete_values) {
	EXPECT_FALSE(FindAssFontNameValueAt("{\\fn}", 3));
	ExpectFontRange("{\\fnIncomplete Font", "Incomplete Font");
}

TEST(ass_font_tag_selection, selects_only_the_font_tag_containing_the_position) {
	std::string const text = "{\\fnArial}A{\\fnTimes New Roman}B";
	auto first = FindAssFontNameValueAt(text, static_cast<int>(text.find("Arial")));
	auto second = FindAssFontNameValueAt(text, static_cast<int>(text.find("Roman")));
	ASSERT_TRUE(first);
	ASSERT_TRUE(second);
	EXPECT_EQ("Arial", text.substr(first.start, first.end - first.start));
	EXPECT_EQ("Times New Roman", text.substr(second.start, second.end - second.start));
	EXPECT_FALSE(first.Contains(second.start));

	// Prefixes, braces, and ordinary dialogue text are not font values.
	EXPECT_FALSE(FindAssFontNameValueAt(text, static_cast<int>(text.find("\\fn"))));
	EXPECT_FALSE(FindAssFontNameValueAt(text, static_cast<int>(text.find("\\fn")) + 1));
	EXPECT_FALSE(FindAssFontNameValueAt(text, static_cast<int>(text.find("}A")) + 1));
}

} // namespace
