// Copyright (c) 2026

#include <main.h>

#include "ass_override.h"

namespace {

void ExpectParameter(std::string const& tag_text, size_t index, AssParameterClass classification, VariableDataType type) {
	AssOverrideTag tag(tag_text);
	ASSERT_TRUE(tag.IsValid()) << tag_text;
	ASSERT_LT(index, tag.Params.size()) << tag_text;
	EXPECT_FALSE(tag.Params[index].omitted) << tag_text;
	EXPECT_EQ(classification, tag.Params[index].classification) << tag_text;
	EXPECT_EQ(type, tag.Params[index].GetType()) << tag_text;
}

TEST(resolution_resampler_classification, distinguishes_horizontal_and_vertical_sizes) {
	ExpectParameter("\\bord2", 0, AssParameterClass::ABSOLUTE_SIZE_Y, VariableDataType::FLOAT);
	ExpectParameter("\\xbord2", 0, AssParameterClass::ABSOLUTE_SIZE_X, VariableDataType::FLOAT);
	ExpectParameter("\\ybord2", 0, AssParameterClass::ABSOLUTE_SIZE_Y, VariableDataType::FLOAT);
	ExpectParameter("\\shad2", 0, AssParameterClass::ABSOLUTE_SIZE_Y, VariableDataType::FLOAT);
	ExpectParameter("\\xshad2", 0, AssParameterClass::ABSOLUTE_SIZE_X, VariableDataType::FLOAT);
	ExpectParameter("\\yshad2", 0, AssParameterClass::ABSOLUTE_SIZE_Y, VariableDataType::FLOAT);
	ExpectParameter("\\pbo2", 0, AssParameterClass::ABSOLUTE_SIZE_Y, VariableDataType::INT);
	ExpectParameter("\\fsp2", 0, AssParameterClass::ABSOLUTE_SIZE_Y, VariableDataType::FLOAT);
	ExpectParameter("\\fs20", 0, AssParameterClass::ABSOLUTE_SIZE_Y, VariableDataType::FLOAT);
}

TEST(resolution_resampler_classification, leaves_layout_relative_blur_unscaled) {
	ExpectParameter("\\be2", 0, AssParameterClass::NORMAL, VariableDataType::INT);
	ExpectParameter("\\blur2", 0, AssParameterClass::NORMAL, VariableDataType::FLOAT);
}

TEST(resolution_resampler_classification, preserves_fractional_origins) {
	ExpectParameter("\\org(12.5,24.25)", 0, AssParameterClass::ABSOLUTE_POS_X, VariableDataType::FLOAT);
	ExpectParameter("\\org(12.5,24.25)", 1, AssParameterClass::ABSOLUTE_POS_Y, VariableDataType::FLOAT);
}

TEST(resolution_resampler_classification, identifies_positions_clips_and_drawings) {
	ExpectParameter("\\pos(12.5,24.25)", 0, AssParameterClass::ABSOLUTE_POS_X, VariableDataType::FLOAT);
	ExpectParameter("\\pos(12.5,24.25)", 1, AssParameterClass::ABSOLUTE_POS_Y, VariableDataType::FLOAT);
	ExpectParameter("\\move(1,2,3,4)", 2, AssParameterClass::ABSOLUTE_POS_X, VariableDataType::FLOAT);
	ExpectParameter("\\move(1,2,3,4)", 3, AssParameterClass::ABSOLUTE_POS_Y, VariableDataType::FLOAT);
	ExpectParameter("\\clip(1,2,3,4)", 0, AssParameterClass::ABSOLUTE_POS_X, VariableDataType::INT);
	ExpectParameter("\\clip(1,2,3,4)", 1, AssParameterClass::ABSOLUTE_POS_Y, VariableDataType::INT);
	ExpectParameter("\\iclip(1,m 0 0 l 10 10)", 1, AssParameterClass::DRAWING, VariableDataType::TEXT);
	ExpectParameter("\\fscx125", 0, AssParameterClass::RELATIVE_SIZE_X, VariableDataType::FLOAT);
	ExpectParameter("\\fscy125", 0, AssParameterClass::RELATIVE_SIZE_Y, VariableDataType::FLOAT);
}

} // namespace
