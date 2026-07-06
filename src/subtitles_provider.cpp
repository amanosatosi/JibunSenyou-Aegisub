// Copyright (c) 2014, Thomas Goyne <plorkyeran@aegisub.org>
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

#include "include/aegisub/subtitles_provider.h"

#include "ass_dialogue.h"
#include "ass_attachment.h"
#include "ass_file.h"
#include "ass_info.h"
#include "ass_style.h"
#include "compat.h"
#include "factory_manager.h"
#include "options.h"
#include "subtitles_provider_csri.h"
#include "subtitles_provider_libass.h"
#include "subtitles_provider_libassmod.h"

#include <libaegisub/log.h>

#include <boost/algorithm/string/trim.hpp>

#include <mutex>

#include <wx/log.h>

namespace {
	struct factory {
		std::string name;
		std::string subtype;
		std::unique_ptr<SubtitlesProvider> (*create)(std::string const& subtype, agi::BackgroundRunner *br);
		bool hidden;
	};

	void WarnFallbackOnce(std::string const& provider, std::string const& primary_library, std::string const& error) {
		static std::once_flag libassmod_warned_once;
		static std::once_flag mangetsu_warned_once;
		auto &warned_once = provider == "Mangetsu" ? mangetsu_warned_once : libassmod_warned_once;
		std::call_once(warned_once, [&] {
			std::string message;
			if (error.find("Could not load") != std::string::npos) {
				message = primary_library + " not found. Falling back to libass.";
			}
			else {
				message = provider + " unavailable. Falling back to libass.";
			}
			if (!error.empty())
				message += " (" + error + ")";

			LOG_W("subtitle/provider") << message;
			wxLogWarning("%s", to_wx(message));
		});
	}

	static bool is_mangetsu_actor_colorcoding_metadata_comment(AssDialogue const& line) {
		std::string effect = line.Effect.get();
		boost::trim(effect);

		return line.Comment
			&& !line.Actor.get().empty()
			&& effect == "mangetsu-colorcoding";
	}

	std::vector<factory> const& factories() {
		static std::vector<factory> factories;
		if (factories.size()) return factories;
#ifdef WITH_CSRI
		for (auto const& subtype : csri::List())
			factories.push_back(factory{"CSRI/" + subtype, subtype, csri::Create, false});
#endif
		factories.push_back(factory{"libass", "", libass::Create, false});
		LOG_I("subtitle/provider") << "Subtitle renderer libass: available (built-in)";
		std::string libassmod_error;
		if (libassmod::IsAvailable(&libassmod_error))
			factories.push_back(factory{"libassmod", "", libassmod::Create, false});
		else
			LOG_D("subtitle/provider") << "libassmod provider hidden: " << libassmod_error;
		std::string mangetsu_error;
		if (mangetsu::IsAvailable(&mangetsu_error))
			factories.push_back(factory{"Mangetsu", "", mangetsu::Create, false});
		else
			LOG_D("subtitle/provider") << "Mangetsu provider hidden: " << mangetsu_error;
		return factories;
	}
}

std::vector<std::string> SubtitlesProviderFactory::GetClasses() {
	return ::GetClasses(factories());
}

std::vector<std::string> SubtitlesProviderFactory::GetUnavailableClasses() {
	std::vector<std::string> unavailable;

	std::string libassmod_error;
	if (!libassmod::IsAvailable(&libassmod_error))
		unavailable.push_back("libassmod unavailable: missing " + libassmod::PrimaryLibraryName());

	std::string mangetsu_error;
	if (!mangetsu::IsAvailable(&mangetsu_error))
		unavailable.push_back("Mangetsu unavailable: missing " + mangetsu::PrimaryLibraryName());

	return unavailable;
}

std::unique_ptr<SubtitlesProvider> SubtitlesProviderFactory::GetProvider(agi::BackgroundRunner *br) {
	auto preferred = OPT_GET("Subtitle/Provider")->GetString();
	if (preferred == "libassmod") {
		std::string libassmod_error;
		if (!libassmod::IsAvailable(&libassmod_error)) {
			WarnFallbackOnce("libassmod", libassmod::PrimaryLibraryName(), libassmod_error);
			preferred = "libass";
		}
	}
	else if (preferred == "Mangetsu") {
		std::string mangetsu_error;
		if (!mangetsu::IsAvailable(&mangetsu_error)) {
			WarnFallbackOnce("Mangetsu", mangetsu::PrimaryLibraryName(), mangetsu_error);
			preferred = "libass";
		}
	}
	auto sorted = GetSorted(factories(), preferred);

	std::string error;
	for (auto factory : sorted) {
		try {
			auto provider = factory->create(factory->subtype, br);
			if (provider) return provider;
		}
		catch (agi::UserCancelException const&) { throw; }
		catch (agi::Exception const& err) { error += factory->name + ": " + err.GetMessage() + "\n"; }
		catch (...) { error += factory->name + ": Unknown error\n"; }
	}

	throw error;
}

void SubtitlesProvider::LoadSubtitles(AssFile *subs, int time) {
	PrepareSubtitles(subs, time);
	buffer.clear();

	auto push_header = [&](const char *str) {
		buffer.insert(buffer.end(), str, str + strlen(str));
	};
	auto push_line = [&](std::string const& str) {
		buffer.insert(buffer.end(), &str[0], &str[0] + str.size());
		buffer.push_back('\n');
	};

	push_header("\xEF\xBB\xBF[Script Info]\n");
	for (auto const& line : subs->Info)
		push_line(line.GetEntryData());

	push_header("[V4+ Styles]\n");
	for (auto const& line : subs->Styles)
		push_line(line.GetEntryData());

	if (!subs->Attachments.empty()) {
		// TODO: some scripts may have a lot of attachments,
		// so ideally we'd want to write only those actually used on the requested video frame,
		// but this would require some pre-parsing of the attached font files with FreeType,
		// which isn't probably trivial.
		push_header("[Fonts]\n");
		for (auto const& attachment : subs->Attachments)
			if (attachment.Group() == AssEntryGroup::FONT)
				push_line(attachment.GetEntryData());
	}

	push_header("[Events]\n");
	push_header("Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n");
	for (auto const& line : subs->Events) {
		if (is_mangetsu_actor_colorcoding_metadata_comment(line))
			push_line(line.GetEntryData());
		else
			break;
	}

	for (auto const& line : subs->Events) {
		if (!line.Comment && (time < 0 || !(line.Start > time || line.End <= time)))
			push_line(line.GetEntryData());
	}

	LoadSubtitles(&buffer[0], buffer.size());
}
