// Copyright (c) 2026

#include <main.h>

#include "ass_dialogue.h"
#include "ass_file.h"
#include "ass_style.h"
#include "project_properties.h"
#include "resolution_resampler.h"
#include "style_import.h"

#include <libaegisub/path.h>

#include <boost/filesystem/operations.hpp>
#include <gmock/gmock.h>

namespace {

using ::testing::HasSubstr;

ResampleSettings Settings(int source_x, int source_y, int dest_x, int dest_y) {
	return {{0, 0, 0, 0}, source_x, source_y, dest_x, dest_y,
		ResampleARMode::Stretch, YCbCrMatrix::rgb, YCbCrMatrix::rgb};
}

AssStyle& AddStyle(AssFile& file, std::string const& name = "Default") {
	auto style = new AssStyle;
	style->name = name;
	file.Styles.push_back(*style);
	return *style;
}

AssDialogue& AddLine(AssFile& file, std::string const& text) {
	auto line = new AssDialogue;
	line->Text = text;
	file.Events.push_back(*line);
	return *line;
}

void SetResolution(AssFile& file, int x, int y) {
	file.SetScriptInfo("PlayResX", x ? std::to_string(x) : "");
	file.SetScriptInfo("PlayResY", y ? std::to_string(y) : "");
}

std::string ResampleText(std::string text, ResampleSettings settings) {
	AssFile file;
	auto& line = AddLine(file, text);
	ApplyResolutionResample(&file, settings);
	return line.Text;
}

TEST(resolution_resampler, downscales_standard_styles_tags_clips_and_drawings) {
	AssFile file;
	SetResolution(file, 1920, 1080);
	auto& style = AddStyle(file);
	style.fontsize = 75;
	style.outline_w = 6;
	style.shadow_w = 3;
	style.spacing = 3;
	style.scalex = 100;
	style.Margin = {{30, 60, 90}};
	auto& line = AddLine(file,
		"{\\pos(960,540)\\move(0,0,1920,1080,100,900)\\org(120.5,240.25)"
		"\\bord6\\xbord9\\ybord12\\shad3\\xshad6\\yshad9\\fs75\\fsp3"
		"\\clip(30,60,300,600)\\iclip(1,m 30 60 l 300 600)}Text"
		"{\\p1}m 0 0 l 300 150{\\p0}");
	line.Margin = {{30, 60, 90}};

	ApplyResolutionResample(&file, Settings(1920, 1080, 1280, 720));

	EXPECT_DOUBLE_EQ(50, style.fontsize);
	EXPECT_DOUBLE_EQ(4, style.outline_w);
	EXPECT_DOUBLE_EQ(2, style.shadow_w);
	EXPECT_DOUBLE_EQ(2, style.spacing);
	EXPECT_DOUBLE_EQ(100, style.scalex);
	EXPECT_EQ((std::array<int, 3>{{20, 40, 60}}), style.Margin);
	EXPECT_EQ((std::array<int, 3>{{20, 40, 60}}), line.Margin);
	EXPECT_EQ(
		"{\\pos(640,360)\\move(0,0,1280,720,100,900)\\org(80.333,160.167)"
		"\\bord4\\xbord6\\ybord8\\shad2\\xshad4\\yshad6\\fs50\\fsp2"
		"\\clip(20,40,200,400)\\iclip(1,m 20 40 l 200 400)}Text"
		"{\\p1}m 0 0 l 200 100{\\p0}", line.Text.get());
	EXPECT_EQ("1280", file.GetScriptInfo("PlayResX"));
	EXPECT_EQ("720", file.GetScriptInfo("PlayResY"));
}

TEST(resolution_resampler, upscales_standard_content) {
	AssFile file;
	SetResolution(file, 1280, 720);
	auto& style = AddStyle(file);
	style.fontsize = 50;
	auto& line = AddLine(file, "{\\pos(640,360)\\bord4\\fs50}Text");

	ApplyResolutionResample(&file, Settings(1280, 720, 1920, 1080));

	EXPECT_DOUBLE_EQ(75, style.fontsize);
	EXPECT_EQ("{\\pos(960,540)\\bord6\\fs75}Text", line.Text.get());
}

TEST(resolution_resampler, stretch_uses_upstream_anamorphic_rules) {
	AssFile file;
	SetResolution(file, 1440, 1080);
	auto& style = AddStyle(file);
	style.scalex = 100;
	auto& line = AddLine(file,
		"{\\pos(720,540)\\fscx100\\fscy100\\bord10\\clip(m 0 0 l 300 300)}Text"
		"{\\p1}m 0 0 l 300 300{\\p0}");

	ApplyResolutionResample(&file, Settings(1440, 1080, 1920, 1080));

	EXPECT_NEAR(133.333333, style.scalex, 0.00001);
	EXPECT_EQ(
		"{\\pos(960,540)\\fscx133.333\\fscy100\\bord10\\clip(m 0 0 l 400 300)}Text"
		"{\\p1}m 0 0 l 300 300{\\p0}", line.Text.get());
}

TEST(resolution_resampler, preserves_layoutres_relationship_when_present) {
	AssFile file;
	SetResolution(file, 1920, 1080);
	file.SetScriptInfo("LayoutResX", "960");
	file.SetScriptInfo("LayoutResY", "540");

	ApplyResolutionResample(&file, Settings(1920, 1080, 1280, 720));

	EXPECT_EQ("960", file.GetScriptInfo("LayoutResX"));
	EXPECT_EQ("540", file.GetScriptInfo("LayoutResY"));
}

TEST(resolution_resampler, leaves_layoutres_absent_when_absent) {
	AssFile file;
	SetResolution(file, 1920, 1080);

	ApplyResolutionResample(&file, Settings(1920, 1080, 1280, 720));

	EXPECT_TRUE(file.GetScriptInfo("LayoutResX").empty());
	EXPECT_TRUE(file.GetScriptInfo("LayoutResY").empty());
}

TEST(resolution_resampler, effective_resolution_infers_missing_playres_x) {
	AssFile file;
	SetResolution(file, 0, 720);
	int x, y;
	file.GetResolution(x, y);
	EXPECT_EQ(960, x);
	EXPECT_EQ(720, y);
	auto& line = AddLine(file, "{\\pos(960,720)}Text");
	ApplyResolutionResample(&file, Settings(x, y, 1280, 720));
	EXPECT_EQ("{\\pos(1280,720)}Text", line.Text.get());
}

TEST(resolution_resampler, effective_resolution_infers_missing_playres_y) {
	AssFile file;
	SetResolution(file, 1920, 0);
	int x, y;
	file.GetResolution(x, y);
	EXPECT_EQ(1920, x);
	EXPECT_EQ(1440, y);
	auto& line = AddLine(file, "{\\pos(1920,1440)}Text");
	ApplyResolutionResample(&file, Settings(x, y, 1280, 720));
	EXPECT_EQ("{\\pos(1280,720)}Text", line.Text.get());
}

TEST(resolution_resampler, effective_resolution_uses_aegisub_default_when_both_axes_missing) {
	AssFile file;
	int x, y;
	file.GetResolution(x, y);
	EXPECT_EQ(384, x);
	EXPECT_EQ(288, y);
	auto& line = AddLine(file, "{\\pos(384,288)}Text");
	ApplyResolutionResample(&file, Settings(x, y, 1280, 720));
	EXPECT_EQ("{\\pos(1280,720)}Text", line.Text.get());
}

TEST(resolution_resampler, rescales_mangetsu_pixel_and_coordinate_parameters) {
	auto result = ResampleText(
		"{\\1bs9\\2bsx12\\3bsy15\\bbs18\\boxp21\\boxpx24\\boxpy27"
		"\\colsp30\\fsvp33\\fshp36\\furifsp39\\furipos(42,45)"
		"\\rnd48\\rndx51\\rndy54\\rndz57\\z60\\jitter(12,15,18,21,250,42)"
		"\\movevc(30,60,300,600,100,900)"
		"\\mover(90,120,180,240,45,90,30,60,100,900)"
		"\\moves3(30,60,60,120,90,180,100,900)"
		"\\moves4(30,60,60,120,90,180,120,240,100,900)}Text",
		Settings(1920, 1080, 1280, 720));

	EXPECT_THAT(result, HasSubstr("\\1bs6\\2bsx8\\3bsy10\\bbs12"));
	EXPECT_THAT(result, HasSubstr("\\boxp14\\boxpx16\\boxpy18"));
	EXPECT_THAT(result, HasSubstr("\\colsp20\\fsvp22\\fshp24\\furifsp26\\furipos(28,30)"));
	EXPECT_THAT(result, HasSubstr("\\rnd32\\rndx34\\rndy36\\rndz38\\z40"));
	EXPECT_THAT(result, HasSubstr("\\jitter(8,10,12,14,250,42)"));
	EXPECT_THAT(result, HasSubstr("\\movevc(20,40,200,400,100,900)"));
	EXPECT_THAT(result, HasSubstr("\\mover(60,80,120,160,45,90,20,40,100,900)"));
	EXPECT_THAT(result, HasSubstr("\\moves3(20,40,40,80,60,120,100,900)"));
	EXPECT_THAT(result, HasSubstr("\\moves4(20,40,40,80,60,120,80,160,100,900)"));
}

TEST(resolution_resampler, rescales_positioned_gradient_geometry_only) {
	auto result = ResampleText(
		"{\\pgrd(150,180,600,540,45,&H0000FF&,50%,&H00FF00&,&HFF0000&)}Text",
		Settings(1920, 1080, 1280, 720));
	EXPECT_EQ(
		"{\\pgrd(100,120,400,360,45,&H0000FF&,50%,&H00FF00&,&HFF0000&)}Text",
		result);
}

TEST(resolution_resampler, preserves_positioned_gradient_reset_form) {
	EXPECT_EQ("{\\pgrd()}Text",
		ResampleText("{\\pgrd()}Text", Settings(1920, 1080, 1280, 720)));
}

TEST(resolution_resampler, preserves_mangetsu_non_spatial_parameters) {
	std::string text =
		"{\\scale125\\fsc110\\frs30\\xblur2.5\\yblur3.5"
		"\\distort(1,0,1.3,1,-0.15,1)\\ortho1\\tan2\\col3\\colan7"
		"\\box1\\bs4\\bbc&H102030&\\bba&H80&"
		"\\furistyle2\\furisx80\\furisy90\\furis45\\furiap1\\furi1"
		"\\jitter0\\kt10"
		"\\img(texture.png,10,20)\\fad(100,200,&H000000&,&HFFFFFF&)}Text";
	EXPECT_EQ(text, ResampleText(text, Settings(1920, 1080, 1280, 720)));
}

TEST(resolution_resampler, style_import_flow_resamples_before_replacement) {
	AssFile current;
	SetResolution(current, 1920, 1080);
	auto& current_sign = AddStyle(current, "Sign");
	current_sign.fontsize = 75;
	auto& line = AddLine(current, "{\\pos(960,540)}Text");

	AssFile source;
	SetResolution(source, 1280, 720);
	auto& source_sign = AddStyle(source, "Sign");
	source_sign.fontsize = 50;
	auto& source_new = AddStyle(source, "New Style");
	source_new.fontsize = 32;

	ApplyResolutionResample(&current, Settings(1920, 1080, 1280, 720));
	EXPECT_TRUE(ImportStyle(current, source_sign, true));
	EXPECT_TRUE(ImportStyle(current, source_new, false));

	EXPECT_DOUBLE_EQ(50, current.GetStyle("Sign")->fontsize);
	ASSERT_NE(nullptr, current.GetStyle("New Style"));
	EXPECT_DOUBLE_EQ(32, current.GetStyle("New Style")->fontsize);
	EXPECT_EQ("{\\pos(640,360)}Text", line.Text.get());
	EXPECT_EQ("1280", current.GetScriptInfo("PlayResX"));
	EXPECT_EQ("720", current.GetScriptInfo("PlayResY"));
	EXPECT_DOUBLE_EQ(50, source_sign.fontsize);
	EXPECT_DOUBLE_EQ(32, source_new.fontsize);
	EXPECT_EQ("1280", source.GetScriptInfo("PlayResX"));
	EXPECT_EQ("720", source.GetScriptInfo("PlayResY"));
}

TEST(project_properties, video_update_does_not_rewrite_other_project_paths) {
	ProjectProperties properties;
	properties.video_file = "old-video.mkv";
	properties.audio_file = "special-audio.wav";
	properties.timecodes_file = "timecodes.txt";
	properties.keyframes_file = "keyframes.txt";

	agi::Path path_helper;
	auto script = boost::filesystem::system_complete("project/current.ass");
	path_helper.SetToken("?script", script.parent_path());
	project::UpdateVideoRelativePath(properties, path_helper, script.parent_path() / "new-video.mkv");

	EXPECT_EQ("new-video.mkv", properties.video_file);
	EXPECT_EQ("special-audio.wav", properties.audio_file);
	EXPECT_EQ("timecodes.txt", properties.timecodes_file);
	EXPECT_EQ("keyframes.txt", properties.keyframes_file);
}

} // namespace
