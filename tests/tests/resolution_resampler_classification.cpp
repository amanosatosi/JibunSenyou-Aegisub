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

TEST(resolution_resampler_classification, identifies_mangetsu_spatial_parameters) {
	for (int layer = 1; layer <= 10; ++layer) {
		auto prefix = "\\" + std::to_string(layer);
		ExpectParameter(prefix + "bs2", 0, AssParameterClass::ABSOLUTE_SIZE_Y, VariableDataType::FLOAT);
		ExpectParameter(prefix + "bsx2", 0, AssParameterClass::ABSOLUTE_SIZE_X, VariableDataType::FLOAT);
		ExpectParameter(prefix + "bsy2", 0, AssParameterClass::ABSOLUTE_SIZE_Y, VariableDataType::FLOAT);
		ExpectParameter(prefix + "bbs2", 0, AssParameterClass::ABSOLUTE_SIZE_Y, VariableDataType::FLOAT);
	}
	ExpectParameter("\\bbs2", 0, AssParameterClass::ABSOLUTE_SIZE_Y, VariableDataType::FLOAT);
	ExpectParameter("\\boxp2", 0, AssParameterClass::ABSOLUTE_SIZE_Y, VariableDataType::FLOAT);
	ExpectParameter("\\boxpx2", 0, AssParameterClass::ABSOLUTE_SIZE_X, VariableDataType::FLOAT);
	ExpectParameter("\\boxpy2", 0, AssParameterClass::ABSOLUTE_SIZE_Y, VariableDataType::FLOAT);
	ExpectParameter("\\colsp2", 0, AssParameterClass::ABSOLUTE_SIZE_X, VariableDataType::FLOAT);
	ExpectParameter("\\fsvp2", 0, AssParameterClass::ABSOLUTE_SIZE_Y, VariableDataType::FLOAT);
	ExpectParameter("\\fshp2", 0, AssParameterClass::ABSOLUTE_SIZE_Y, VariableDataType::FLOAT);
	ExpectParameter("\\furifsp2", 0, AssParameterClass::ABSOLUTE_SIZE_Y, VariableDataType::FLOAT);
	ExpectParameter("\\furipos(2,3)", 0, AssParameterClass::ABSOLUTE_SIZE_X, VariableDataType::FLOAT);
	ExpectParameter("\\furipos(2,3)", 1, AssParameterClass::ABSOLUTE_SIZE_Y, VariableDataType::FLOAT);
	ExpectParameter("\\rnd2", 0, AssParameterClass::ABSOLUTE_SIZE_XY, VariableDataType::FLOAT);
	ExpectParameter("\\rndx2", 0, AssParameterClass::ABSOLUTE_SIZE_X, VariableDataType::FLOAT);
	ExpectParameter("\\rndy2", 0, AssParameterClass::ABSOLUTE_SIZE_Y, VariableDataType::FLOAT);
	ExpectParameter("\\rndz2", 0, AssParameterClass::ABSOLUTE_SIZE_Y, VariableDataType::FLOAT);
	ExpectParameter("\\z2", 0, AssParameterClass::ABSOLUTE_SIZE_Y, VariableDataType::FLOAT);
	ExpectParameter("\\jitter(1,2,3,4,250,42)", 0, AssParameterClass::ABSOLUTE_SIZE_X, VariableDataType::FLOAT);
	ExpectParameter("\\jitter(1,2,3,4,250,42)", 1, AssParameterClass::ABSOLUTE_SIZE_X, VariableDataType::FLOAT);
	ExpectParameter("\\jitter(1,2,3,4,250,42)", 2, AssParameterClass::ABSOLUTE_SIZE_Y, VariableDataType::FLOAT);
	ExpectParameter("\\jitter(1,2,3,4,250,42)", 3, AssParameterClass::ABSOLUTE_SIZE_Y, VariableDataType::FLOAT);
	ExpectParameter("\\movevc(1,2)", 0, AssParameterClass::ABSOLUTE_SIZE_X, VariableDataType::FLOAT);
	ExpectParameter("\\movevc(1,2)", 1, AssParameterClass::ABSOLUTE_SIZE_Y, VariableDataType::FLOAT);
	ExpectParameter("\\movevc(1,2,3,4,5,6)", 2, AssParameterClass::ABSOLUTE_SIZE_X, VariableDataType::FLOAT);
	ExpectParameter("\\movevc(1,2,3,4,5,6)", 3, AssParameterClass::ABSOLUTE_SIZE_Y, VariableDataType::FLOAT);
	ExpectParameter("\\mover(1,2,3,4,0,90,5,6)", 0, AssParameterClass::ABSOLUTE_POS_X, VariableDataType::FLOAT);
	ExpectParameter("\\mover(1,2,3,4,0,90,5,6)", 1, AssParameterClass::ABSOLUTE_POS_Y, VariableDataType::FLOAT);
	ExpectParameter("\\mover(1,2,3,4,0,90,5,6)", 2, AssParameterClass::ABSOLUTE_POS_X, VariableDataType::FLOAT);
	ExpectParameter("\\mover(1,2,3,4,0,90,5,6)", 3, AssParameterClass::ABSOLUTE_POS_Y, VariableDataType::FLOAT);
	ExpectParameter("\\mover(1,2,3,4,0,90,5,6)", 6, AssParameterClass::ABSOLUTE_SIZE_XY, VariableDataType::FLOAT);
	ExpectParameter("\\mover(1,2,3,4,0,90,5,6)", 7, AssParameterClass::ABSOLUTE_SIZE_XY, VariableDataType::FLOAT);
	for (size_t parameter = 0; parameter < 6; ++parameter)
		ExpectParameter("\\moves3(1,2,3,4,5,6)", parameter,
			parameter % 2 ? AssParameterClass::ABSOLUTE_POS_Y : AssParameterClass::ABSOLUTE_POS_X,
			VariableDataType::FLOAT);
	for (size_t parameter = 0; parameter < 8; ++parameter)
		ExpectParameter("\\moves4(1,2,3,4,5,6,7,8)", parameter,
			parameter % 2 ? AssParameterClass::ABSOLUTE_POS_Y : AssParameterClass::ABSOLUTE_POS_X,
			VariableDataType::FLOAT);
	ExpectParameter("\\pgrd(1,2,3,4,45,&H000000&,&HFFFFFF&)", 0, AssParameterClass::ABSOLUTE_POS_X, VariableDataType::FLOAT);
	ExpectParameter("\\pgrd(1,2,3,4,45,&H000000&,&HFFFFFF&)", 1, AssParameterClass::ABSOLUTE_POS_Y, VariableDataType::FLOAT);
	ExpectParameter("\\pgrd(1,2,3,4,45,&H000000&,&HFFFFFF&)", 2, AssParameterClass::ABSOLUTE_POS_X, VariableDataType::FLOAT);
	ExpectParameter("\\pgrd(1,2,3,4,45,&H000000&,&HFFFFFF&)", 3, AssParameterClass::ABSOLUTE_POS_Y, VariableDataType::FLOAT);
	ExpectParameter("\\1pgrd(1,2,3,4,45,&H000000&,&HFFFFFF&)", 0, AssParameterClass::ABSOLUTE_POS_X, VariableDataType::FLOAT);
}

TEST(resolution_resampler_classification, keeps_mangetsu_non_spatial_values_normal) {
	ExpectParameter("\\pgrd(1,2,3,4,45,&H000000&,50%,&HFFFFFF&)", 4, AssParameterClass::NORMAL, VariableDataType::FLOAT);
	ExpectParameter("\\pgrd(1,2,3,4,45,&H000000&,50%,&HFFFFFF&)", 6, AssParameterClass::NORMAL, VariableDataType::TEXT);
	ExpectParameter("\\scale125", 0, AssParameterClass::NORMAL, VariableDataType::FLOAT);
	ExpectParameter("\\fsc125", 0, AssParameterClass::NORMAL, VariableDataType::FLOAT);
	ExpectParameter("\\frs45", 0, AssParameterClass::NORMAL, VariableDataType::FLOAT);
	ExpectParameter("\\xblur2", 0, AssParameterClass::NORMAL, VariableDataType::FLOAT);
	ExpectParameter("\\yblur2", 0, AssParameterClass::NORMAL, VariableDataType::FLOAT);
	ExpectParameter("\\img(texture.png,10,20)", 1, AssParameterClass::NORMAL, VariableDataType::INT);
}

} // namespace
