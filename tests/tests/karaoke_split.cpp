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

#include <gtest/gtest.h>

#include <libaegisub/karaoke_split.h>

TEST(KaraokeSplit, ProportionalExample) {
	auto split = agi::SplitKaraokeDurationCs(20, 1, 3);
	EXPECT_EQ(split.first, 7);
	EXPECT_EQ(split.second, 13);
}

TEST(KaraokeSplit, BoundaryCases) {
	auto at_start = agi::SplitKaraokeDurationCs(20, 0, 3);
	EXPECT_EQ(at_start.first, 0);
	EXPECT_EQ(at_start.second, 20);

	auto at_end = agi::SplitKaraokeDurationCs(20, 3, 3);
	EXPECT_EQ(at_end.first, 20);
	EXPECT_EQ(at_end.second, 0);
}

TEST(KaraokeSplit, TotalPreserved) {
	for (int d = 0; d <= 200; ++d) {
		for (size_t total = 1; total <= 10; ++total) {
			for (size_t left = 0; left <= total; ++left) {
				auto split = agi::SplitKaraokeDurationCs(d, left, total);
				EXPECT_EQ(split.first + split.second, d);
			}
		}
	}
}

