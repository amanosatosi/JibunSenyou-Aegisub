// Copyright (c) 2026

#include <main.h>

#include "mangetsu_gradient_placement.h"

namespace {

TEST(mangetsu_gradient_placement, ordinary_gradient_tokens_are_unchanged) {
	auto tokens = mangetsu::TokenizeGradientValue("(45,&H000000&,50%,&H00FF00&,&HFFFFFF&)");
	ASSERT_EQ(5u, tokens.size());
	EXPECT_EQ("45", tokens[0]);
	EXPECT_EQ("50%", tokens[2]);
	EXPECT_EQ("&HFFFFFF&", tokens[4]);
}

TEST(mangetsu_gradient_placement, parse_pgrd_and_primary_alias_values_equally) {
	EXPECT_TRUE(mangetsu::IsPlacementGradientTagName("\\pgrd"));
	EXPECT_TRUE(mangetsu::IsPlacementGradientTagName("\\1pgrd"));
	EXPECT_FALSE(mangetsu::IsPlacementGradientTagName("\\2pgrd"));
	mangetsu::PlacementRect compact;
	mangetsu::PlacementRect explicit_primary;
	std::string attached_compact;
	std::string attached_explicit;
	ASSERT_TRUE(mangetsu::ParsePlacementGradientValue(
		"(700,500,100,200,450,&H000000&,&HFFFFFF&)", compact, attached_compact));
	ASSERT_TRUE(mangetsu::ParsePlacementGradientValue(
		"(700,500,100,200,450,&H000000&,&HFFFFFF&)", explicit_primary, attached_explicit));
	EXPECT_DOUBLE_EQ(100, compact.left);
	EXPECT_DOUBLE_EQ(200, compact.top);
	EXPECT_DOUBLE_EQ(700, compact.right);
	EXPECT_DOUBLE_EQ(500, compact.bottom);
	EXPECT_EQ(attached_compact, attached_explicit);
	EXPECT_EQ("(450,&H000000&,&HFFFFFF&)", attached_compact);
}

TEST(mangetsu_gradient_placement, preserves_negative_offscreen_coordinates_and_stop_grammar) {
	mangetsu::PlacementRect rect;
	std::string attached;
	ASSERT_TRUE(mangetsu::ParsePlacementGradientValue(
		"(-10.5,20.25,800.75,340.5,-45.5,&H000000&,50%,&H0000FF&,&HFFFFFF&)", rect, attached));
	EXPECT_DOUBLE_EQ(-10.5, rect.left);
	EXPECT_DOUBLE_EQ(800.75, rect.right);
	EXPECT_EQ("(-45.5,&H000000&,50%,&H0000FF&,&HFFFFFF&)", attached);
	EXPECT_EQ("(-10.5,20.25,800.75,340.5,-45.5,&H000000&,50%,&H0000FF&,&HFFFFFF&)",
		mangetsu::FormatPlacementGradientValue(rect, attached));
}

TEST(mangetsu_gradient_placement, reject_malformed_positioned_values) {
	mangetsu::PlacementRect rect;
	std::string attached;
	EXPECT_FALSE(mangetsu::ParsePlacementGradientValue("(100,200,700,500,&H000000&,&HFFFFFF&)", rect, attached));
	EXPECT_FALSE(mangetsu::ParsePlacementGradientValue("(100x,200,700,500,0,&H000000&,&HFFFFFF&)", rect, attached));
	EXPECT_FALSE(mangetsu::ParsePlacementGradientValue("(100,200,700,500,nan,&H000000&,&HFFFFFF&)", rect, attached));
	EXPECT_FALSE(mangetsu::ParsePlacementGradientValue("(100,200,700,500,0,&H000000&)", rect, attached));
}

TEST(mangetsu_gradient_placement, drag_direction_normalizes_and_serializes_clean_integers) {
	auto rect = mangetsu::NormalizePlacementRect(321, 180, 120, 40);
	ASSERT_TRUE(rect.valid);
	EXPECT_EQ("(120,40,321,180,0,&H000000&,&HFFFFFF&)",
		mangetsu::FormatPlacementGradientValue(rect, "(0,&H000000&,&HFFFFFF&)"));
}

} // namespace
