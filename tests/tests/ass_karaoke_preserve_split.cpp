// Copyright (c) 2025
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

#include <main.h>

#include <libaegisub/character_count.h>

#include "ass_dialogue.h"
#include "ass_karaoke.h"

TEST(AssKaraoke, AddSplitPreserveTimes_SplitsTextAndDuration) {
	AssDialogue dia;
	dia.Start = 0;
	dia.End = 200;
	dia.Text = "{\\k20}abc";

	AssKaraoke kara(&dia, false, false);
	ASSERT_EQ(1u, kara.size());

	kara.AddSplitPreserveTimes(0, 1); // split before 'b'
	ASSERT_EQ(2u, kara.size());

	auto it = kara.begin();
	EXPECT_EQ("a", it->text);
	EXPECT_EQ(70, it->duration);
	++it;
	EXPECT_EQ("bc", it->text);
	EXPECT_EQ(130, it->duration);

	EXPECT_EQ("{\\k7}a{\\k13}bc", kara.GetText());
}

TEST(AssKaraoke, AddSplitPreserveTimes_LeavesOtherSyllablesUnchanged) {
	AssDialogue dia;
	dia.Start = 0;
	dia.End = 200;
	dia.Text = "{\\k10}ab{\\k10}cd";

	AssKaraoke kara(&dia, false, false);
	ASSERT_EQ(2u, kara.size());

	kara.AddSplitPreserveTimes(0, 1); // split "ab" into "a" + "b"
	ASSERT_EQ(3u, kara.size());

	auto it = kara.begin();
	EXPECT_EQ("a", it->text);
	EXPECT_EQ(50, it->duration);
	++it;
	EXPECT_EQ("b", it->text);
	EXPECT_EQ(50, it->duration);
	++it;
	EXPECT_EQ("cd", it->text);
	EXPECT_EQ(100, it->duration);
	EXPECT_EQ(100, it->start_time); // original second syllable start time

	EXPECT_EQ("{\\k5}a{\\k5}b{\\k10}cd", kara.GetText());
}

TEST(AssKaraoke, AddSplitPreserveTimes_PreservesTagType) {
	AssDialogue dia;
	dia.Start = 0;
	dia.End = 200;
	dia.Text = "{\\kf20}abc";

	AssKaraoke kara(&dia, false, false);
	ASSERT_EQ(1u, kara.size());

	kara.AddSplitPreserveTimes(0, 1);
	ASSERT_EQ(2u, kara.size());

	EXPECT_EQ("{\\kf7}a{\\kf13}bc", kara.GetText());
}

TEST(AssKaraoke, AddSplitPreserveTimes_UTF8Boundary) {
	AssDialogue dia;
	dia.Start = 0;
	dia.End = 200;
	dia.Text = std::string(u8"{\\k20}あい");

	AssKaraoke kara(&dia, false, false);
	ASSERT_EQ(1u, kara.size());

	const std::string text = kara.begin()->text;
	const size_t split_at = agi::IndexOfCharacter(text, 1); // boundary after first character

	kara.AddSplitPreserveTimes(0, split_at);
	ASSERT_EQ(2u, kara.size());

	auto it = kara.begin();
	EXPECT_EQ(std::string(u8"あ"), it->text);
	++it;
	EXPECT_EQ(std::string(u8"い"), it->text);

	EXPECT_EQ(std::string(u8"{\\k10}あ{\\k10}い"), kara.GetText());
}

TEST(AssKaraoke, AddSplitPreserveTimes_NoOpOnBoundaries) {
	AssDialogue dia;
	dia.Start = 0;
	dia.End = 200;
	dia.Text = "{\\k20}abc";

	AssKaraoke kara(&dia, false, false);
	ASSERT_EQ(1u, kara.size());

	kara.AddSplitPreserveTimes(0, 0);
	EXPECT_EQ(1u, kara.size());

	kara.AddSplitPreserveTimes(0, kara.begin()->text.size());
	EXPECT_EQ(1u, kara.size());
}

