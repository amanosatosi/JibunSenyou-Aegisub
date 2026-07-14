#include <main.h>

#include "ass_dialogue.h"
#include "ass_karaoke.h"

TEST(AssKaraoke, ToshikiTiming_InsertAndDragPreservesTotalDuration) {
	AssDialogue dia;
	dia.Start = 0;
	dia.End = 500;
	dia.Text = "ab";

	AssKaraoke kara(&dia, false, false);
	kara.AddSplitKTiming(0, 1);
	ASSERT_EQ(2u, kara.size());
	EXPECT_EQ(0, kara.begin()->duration);

	kara.SetTimingBoundaries(0, 500, {40}, false);
	EXPECT_EQ(40, kara.begin()->duration);
	EXPECT_EQ(460, (kara.begin() + 1)->duration);
	EXPECT_EQ(500, kara.begin()->duration + (kara.begin() + 1)->duration);

	kara.SetTimingBoundaries(0, 500, {120}, false);
	EXPECT_EQ(120, kara.begin()->duration);
	EXPECT_EQ(380, (kara.begin() + 1)->duration);
}

TEST(AssKaraoke, ToshikiTiming_ClampsCrossingBoundaries) {
	AssDialogue dia;
	dia.Start = 0;
	dia.End = 500;
	dia.Text = "abc";

	AssKaraoke kara(&dia, false, false);
	kara.AddSplitKTiming(0, 1);
	kara.AddSplitKTiming(1, 1);
	kara.SetTimingBoundaries(0, 500, {200, 300}, false);
	kara.SetTimingBoundaries(0, 500, {400, 300}, false);

	EXPECT_EQ(400, kara.begin()->duration);
	auto it = kara.begin() + 1;
	EXPECT_EQ(0, it->duration);
	++it;
	EXPECT_EQ(100, it->duration);
}

TEST(AssKaraoke, ToshikiTiming_PreservesPartialAndZeroLengthSlots) {
	AssDialogue dia;
	dia.Start = 0;
	dia.End = 400;
	dia.Text = "{\\k22}a{\\k0}r{\\k18}u";

	AssKaraoke kara(&dia, false, false);
	ASSERT_EQ(3u, kara.size());
	EXPECT_EQ(220, (kara.begin() + 1)->start_time);
	EXPECT_EQ(0, (kara.begin() + 1)->duration);

	kara.SetTimingBoundaries(0, 400, {220, 260}, false);
	EXPECT_EQ("{\\k22}a{\\k4}r{\\k14}u", kara.GetText());
}

TEST(AssKaraoke, ToshikiTiming_TagTypesAndSingleSegmentChanges) {
	AssDialogue dia;
	dia.Start = 0;
	dia.End = 200;
	dia.Text = "ab";

	AssKaraoke kara(&dia, false, false);
	kara.AddSplitKTiming(0, 1);
	kara.SetTimingBoundaries(0, 200, {100}, false);

	kara.SetTagType("\\k", false);
	EXPECT_EQ("{\\k10}a{\\k10}b", kara.GetText());
	kara.SetTagType("\\K", false);
	EXPECT_EQ("{\\K10}a{\\K10}b", kara.GetText());
	kara.SetTagType("\\kf", false);
	EXPECT_EQ("{\\kf10}a{\\kf10}b", kara.GetText());
	kara.SetSyllableTagType(1, "\\ko", false);
	EXPECT_EQ("{\\kf10}a{\\ko10}b", kara.GetText());
}

TEST(AssKaraoke, ToshikiTiming_ReopenCommittedLineWithoutDrift) {
	AssDialogue dia;
	dia.Start = 1000;
	dia.End = 1500;
	dia.Text = "{\\ko20}a{\\ko15}b{\\ko15}c";

	AssKaraoke first(&dia, false, false);
	first.SetTimingBoundaries(1000, 1500, {1200, 1350}, false);
	dia.Text = first.GetText();

	AssKaraoke reopened(&dia, false, true);
	EXPECT_EQ(dia.Text, reopened.GetText());
	EXPECT_EQ(200, reopened.begin()->duration);
	EXPECT_EQ(150, (reopened.begin() + 1)->duration);
	EXPECT_EQ(150, (reopened.begin() + 2)->duration);
}
