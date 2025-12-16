// Copyright (c) 2025
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
// [Toshi audio-meta]

#include "matroska_audio_metadata.h"

extern "C" {
#include "MatroskaParser.h"
}

#include <libaegisub/file_mapping.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <exception>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

class MatroskaInputStream final : public InputStream {
	agi::read_file_mapping file;
	std::string error;

	static int Read(InputStream *st, uint64_t pos, void *buffer, int count) {
		auto *self = static_cast<MatroskaInputStream*>(st);
		if (pos >= self->file.size())
			return 0;

		auto remaining = self->file.size() - pos;
		if (remaining < static_cast<uint64_t>(INT_MAX))
			count = std::min(static_cast<int>(remaining), count);

		try {
			memcpy(buffer, self->file.read(pos, count), count);
		}
		catch (std::exception const& e) {
			self->error = e.what();
			return -1;
		}

		return count;
	}

	static int64_t Scan(InputStream *st, uint64_t start, unsigned signature) {
		auto *self = static_cast<MatroskaInputStream*>(st);
		auto size = self->file.size();
		unsigned cmp = 0;

		try {
			for (uint64_t i = start; i < size; ++i) {
				int c = *self->file.read(i, 1);
				cmp = ((cmp << 8) | c) & 0xffffffff;
				if (cmp == signature)
					return static_cast<int64_t>(i) - 4;
			}
		}
		catch (std::exception const& e) {
			self->error = e.what();
			return -1;
		}

		return -1;
	}

	static int64_t Size(InputStream *st) {
		return static_cast<int64_t>(static_cast<MatroskaInputStream*>(st)->file.size());
	}

public:
	explicit MatroskaInputStream(agi::fs::path const& filename)
	: file(filename)
	{
		read = &MatroskaInputStream::Read;
		scan = &MatroskaInputStream::Scan;
		getcachesize = [](InputStream *) -> unsigned int { return 16 * 1024 * 1024; };
		geterror = [](InputStream *st) -> const char * { return static_cast<MatroskaInputStream*>(st)->error.c_str(); };
		memalloc = [](InputStream *, size_t size) { return malloc(size); };
		memrealloc = [](InputStream *, void *mem, size_t size) { return realloc(mem, size); };
		memfree = [](InputStream *, void *mem) { free(mem); };
		progress = [](InputStream *, uint64_t, uint64_t) { return 1; };
		getfilesize = &MatroskaInputStream::Size;
	}
};

bool IsMatroskaExtension(agi::fs::path const& filename) {
	std::string ext = filename.extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

	return ext == ".mkv" || ext == ".mka" || ext == ".mk3d" || ext == ".webm";
}

std::string NormalizeLanguage(std::string lang) {
	std::transform(lang.begin(), lang.end(), lang.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return lang;
}

// Prefer LanguageIETF when available on TrackInfo; fall back to Language.
// Overload resolution does the member detection without requiring the field to exist.
template <typename T>
auto ExtractLanguageIETF(const T *info, int) -> decltype((void)info->LanguageIETF, std::string()) {
	if (!info)
		return {};

	if constexpr (std::is_array_v<decltype(info->LanguageIETF)>) {
		if (info->LanguageIETF[0])
			return NormalizeLanguage(std::string(info->LanguageIETF));
	}
	else {
		if (info->LanguageIETF && info->LanguageIETF[0])
			return NormalizeLanguage(std::string(info->LanguageIETF));
	}

	return {};
}

template <typename T>
std::string ExtractLanguageIETF(const T *, ...) {
	return {};
}

std::string ExtractLanguage(const TrackInfo *info) {
	if (!info)
		return {};

	auto ietf = ExtractLanguageIETF(info, 0);
	if (!ietf.empty())
		return ietf;

	if (info->Language[0])
		return NormalizeLanguage(std::string(info->Language));

	return {};
}

} // namespace

bool DecorateAudioTrackListFromMatroska(const agi::fs::path& filename, std::map<int, std::string>& track_list) {
	if (track_list.size() <= 1)
		return false;

	if (!IsMatroskaExtension(filename))
		return false;

	std::vector<int> track_keys;
	track_keys.reserve(track_list.size());
	for (auto const& entry : track_list)
		track_keys.push_back(entry.first);

	std::vector<MatroskaAudioMeta> mkv_audio_tracks;
	try {
		MatroskaInputStream input(filename);
		char err[2048];
		std::unique_ptr<MatroskaFile, decltype(&mkv_Close)> file(mkv_Open(&input, err, sizeof(err)), mkv_Close);
		if (!file)
			return false;

		unsigned int num_tracks = mkv_GetNumTracks(file.get());
		for (unsigned int i = 0; i < num_tracks; ++i) {
			TrackInfo *info = mkv_GetTrackInfo(file.get(), i);
			if (!info || info->Type != TT_AUDIO)
				continue;

			MatroskaAudioMeta meta;
			meta.language = ExtractLanguage(info);
			if (info->Name && info->Name[0])
				meta.name = info->Name;

			mkv_audio_tracks.push_back(std::move(meta));
		}
	}
	catch (...) {
		return false;
	}

	if (track_keys.size() != mkv_audio_tracks.size())
		return false;

	bool decorated = false;
	for (size_t i = 0; i < track_keys.size(); ++i) {
		const auto& meta = mkv_audio_tracks[i];
		std::string suffix;
		if (!meta.language.empty())
			suffix += "[" + meta.language + "]";
		if (!meta.name.empty()) {
			if (!suffix.empty())
				suffix += " ";
			suffix += meta.name;
		}

		if (!suffix.empty()) {
			auto it = track_list.find(track_keys[i]);
			if (it != track_list.end()) {
				it->second += "  -  " + suffix;
				decorated = true;
			}
		}
	}

	// We rely on matching the provider's audio track ordering to Matroska's
	// parsed audio track order because FFMS2 does not expose Matroska metadata.
	return decorated;
}
