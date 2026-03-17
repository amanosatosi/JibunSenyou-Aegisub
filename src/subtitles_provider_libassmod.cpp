// Copyright (c) 2026, Aegisub Project
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS; WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
// Aegisub Project http://www.aegisub.org/

/// @file subtitles_provider_libassmod.cpp
/// @brief libassmod-based subtitle renderer
/// @ingroup subtitle_rendering
///

#include "subtitles_provider_libassmod.h"

#include "ass_attachment.h"
#include "ass_file.h"
#include "compat.h"
#include "include/aegisub/subtitles_provider.h"
#include "video_frame.h"

#include <libaegisub/ass/uuencode.h>
#include <libaegisub/background_runner.h>
#include <libaegisub/dispatch.h>
#include <libaegisub/exception.h>
#include <libaegisub/log.h>
#include <libaegisub/make_unique.h>
#include <libaegisub/util.h>

#include <atomic>
#include <algorithm>
#include <boost/gil.hpp>
#include <cctype>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <wx/dir.h>
#include <wx/image.h>
#include <wx/filename.h>
#include <wx/imagpng.h>
#include <wx/intl.h>
#include <wx/log.h>
#include <wx/mstream.h>
#include <wx/strconv.h>
#include <wx/thread.h>

#ifndef _WIN32
#include <dlfcn.h>
#endif
#ifdef _WIN32
#include <windows.h>
#endif

extern "C" {
#include <ass/ass.h>
}

#ifndef LIBASSMOD_FEATURE_TAG_IMAGE
#define LIBASSMOD_FEATURE_TAG_IMAGE 1
typedef enum {
	ASS_TAG_IMAGE_FORMAT_PNG = 1,
	ASS_TAG_IMAGE_FORMAT_JPEG = 2,
	ASS_TAG_IMAGE_FORMAT_WEBP = 3,
} ASS_TagImageFormat;
#endif

#ifndef LIBASSMOD_FEATURE_RGBA
typedef struct ass_image_rgba {
	int w, h;
	int stride;
	uint8_t *rgba;
	int dst_x, dst_y;
	int type;
	struct ass_image_rgba *next;
} ASS_ImageRGBA;

typedef struct ass_render_result {
	ASS_Image *imgs;
	ASS_ImageRGBA *imgs_rgba;
	int use_rgba;
} ASS_RenderResult;
#endif

namespace {
#ifndef _WIN32
#ifdef __APPLE__
#define DLOPEN_FLAGS (RTLD_LAZY | RTLD_LOCAL)
#else
#define DLOPEN_FLAGS (RTLD_LAZY | RTLD_LOCAL | RTLD_DEEPBIND)
#endif
#endif

#ifdef _WIN32
using LibassModHandle = HMODULE;
#else
using LibassModHandle = void *;
#endif

using AssLibraryInitFunc = ASS_Library *(*)(void);
using AssLibraryDoneFunc = void (*)(ASS_Library *);
using AssSetMessageCbFunc = void (*)(ASS_Library *, void (*)(int, const char *, va_list, void *), void *);
using AssRendererInitFunc = ASS_Renderer *(*)(ASS_Library *);
using AssRendererDoneFunc = void (*)(ASS_Renderer *);
using AssSetFontScaleFunc = void (*)(ASS_Renderer *, double);
using AssSetFontsFunc = void (*)(ASS_Renderer *, const char *, const char *, int, const char *, int);
using AssReadMemoryFunc = ASS_Track *(*)(ASS_Library *, char *, size_t, const char *);
using AssFreeTrackFunc = void (*)(ASS_Track *);
using AssSetFrameSizeFunc = void (*)(ASS_Renderer *, int, int);
using AssSetStorageSizeFunc = void (*)(ASS_Renderer *, int, int);
using AssRenderFrameAutoFunc = ASS_RenderResult (*)(ASS_Renderer *, ASS_Track *, long long, int *);
using AssFreeImagesRGBAFunc = void (*)(ASS_ImageRGBA *);
#ifdef LIBASSMOD_FEATURE_TAG_IMAGE
using AssClearTagImagesFunc = void (*)(ASS_Renderer *);
using AssSetTagImageRGBAFunc = int (*)(ASS_Renderer *, const char *, ASS_TagImageFormat, int, int, int, const unsigned char *);
#endif

struct LibassModApi {
	LibassModHandle handle = nullptr;
	AssLibraryInitFunc ass_library_init = nullptr;
	AssLibraryDoneFunc ass_library_done = nullptr;
	AssSetMessageCbFunc ass_set_message_cb = nullptr;
	AssRendererInitFunc ass_renderer_init = nullptr;
	AssRendererDoneFunc ass_renderer_done = nullptr;
	AssSetFontScaleFunc ass_set_font_scale = nullptr;
	AssSetFontsFunc ass_set_fonts = nullptr;
	AssReadMemoryFunc ass_read_memory = nullptr;
	AssFreeTrackFunc ass_free_track = nullptr;
	AssSetFrameSizeFunc ass_set_frame_size = nullptr;
	AssSetStorageSizeFunc ass_set_storage_size = nullptr;
	AssRenderFrameAutoFunc ass_render_frame_auto = nullptr;
	AssFreeImagesRGBAFunc ass_free_images_rgba = nullptr;
#ifdef LIBASSMOD_FEATURE_TAG_IMAGE
	AssClearTagImagesFunc ass_clear_tag_images = nullptr;
	AssSetTagImageRGBAFunc ass_set_tag_image_rgba = nullptr;
#endif
};

LibassModApi api;
bool api_loaded = false;
std::string api_error;
std::once_flag api_once;
ASS_Library *library = nullptr;
std::unique_ptr<agi::dispatch::Queue> cache_queue;
std::once_flag cache_queue_once;

void msg_callback(int level, const char *fmt, va_list args, void *) {
	if (level >= 7) return;
	char buf[1024];
#ifdef _WIN32
	vsprintf_s(buf, sizeof(buf), fmt, args);
#else
	vsnprintf(buf, sizeof(buf), fmt, args);
#endif

	if (level < 2) // warning/error
		LOG_I("subtitle/provider/libassmod") << buf;
	else // verbose
		LOG_D("subtitle/provider/libassmod") << buf;
}

void CloseLibassModHandle() {
#ifdef _WIN32
	if (api.handle) {
		FreeLibrary(api.handle);
		api.handle = nullptr;
	}
#else
	if (api.handle) {
		dlclose(api.handle);
		api.handle = nullptr;
	}
#endif
}

template <typename T>
bool LoadSymbol(LibassModHandle handle, const char *name, T &out, std::string &error) {
#ifdef _WIN32
	auto sym = reinterpret_cast<T>(GetProcAddress(handle, name));
#else
	auto sym = reinterpret_cast<T>(dlsym(handle, name));
#endif
	if (!sym) {
		error = std::string("Missing libassmod symbol: ") + name;
		return false;
	}
	out = sym;
	return true;
}

template <typename T>
void LoadOptionalSymbol(LibassModHandle handle, const char *name, T &out) {
#ifdef _WIN32
	auto sym = reinterpret_cast<T>(GetProcAddress(handle, name));
#else
	auto sym = reinterpret_cast<T>(dlsym(handle, name));
#endif
	out = sym;
}

bool LoadLibassModApi(std::string &error) {
#ifdef _WIN32
	static const wchar_t *kLibassmodNames[] = { L"libassmod.dll", L"assmod.dll", L"ass.dll", L"libass.dll" };
	for (auto name : kLibassmodNames) {
		api.handle = LoadLibraryW(name);
		if (api.handle)
			break;
	}
	if (!api.handle) {
		error = "Could not load libassmod (tried libassmod.dll, assmod.dll, ass.dll, libass.dll).";
		return false;
	}
#else
#ifdef __APPLE__
	static const char *kLibassmodNames[] = { "libassmod.dylib", "libassmod.so", "libass.dylib", "libass.so" };
#else
	static const char *kLibassmodNames[] = { "libassmod.so", "libass.so" };
#endif
	for (auto name : kLibassmodNames) {
		api.handle = dlopen(name, DLOPEN_FLAGS);
		if (api.handle)
			break;
	}
	if (!api.handle) {
		error = "Could not load libassmod (tried libassmod and libass shared library names).";
		return false;
	}
#endif

	if (!LoadSymbol(api.handle, "ass_library_init", api.ass_library_init, error) ||
		!LoadSymbol(api.handle, "ass_library_done", api.ass_library_done, error) ||
		!LoadSymbol(api.handle, "ass_set_message_cb", api.ass_set_message_cb, error) ||
		!LoadSymbol(api.handle, "ass_renderer_init", api.ass_renderer_init, error) ||
		!LoadSymbol(api.handle, "ass_renderer_done", api.ass_renderer_done, error) ||
		!LoadSymbol(api.handle, "ass_set_font_scale", api.ass_set_font_scale, error) ||
		!LoadSymbol(api.handle, "ass_set_fonts", api.ass_set_fonts, error) ||
		!LoadSymbol(api.handle, "ass_read_memory", api.ass_read_memory, error) ||
		!LoadSymbol(api.handle, "ass_free_track", api.ass_free_track, error) ||
		!LoadSymbol(api.handle, "ass_set_frame_size", api.ass_set_frame_size, error) ||
		!LoadSymbol(api.handle, "ass_set_storage_size", api.ass_set_storage_size, error) ||
		!LoadSymbol(api.handle, "ass_render_frame_auto", api.ass_render_frame_auto, error) ||
		!LoadSymbol(api.handle, "ass_free_images_rgba", api.ass_free_images_rgba, error)) {
		CloseLibassModHandle();
		return false;
	}
#ifdef LIBASSMOD_FEATURE_TAG_IMAGE
	LoadOptionalSymbol(api.handle, "ass_clear_tag_images", api.ass_clear_tag_images);
	LoadOptionalSymbol(api.handle, "ass_set_tag_image_rgba", api.ass_set_tag_image_rgba);
#endif

	library = api.ass_library_init();
	if (!library) {
		error = "libassmod initialization failed.";
		CloseLibassModHandle();
		return false;
	}

	api.ass_set_message_cb(library, msg_callback, nullptr);
	return true;
}

bool EnsureLibassMod(std::string *error) {
	std::call_once(api_once, [] {
		api_loaded = LoadLibassModApi(api_error);
	});
	if (!api_loaded && error)
		*error = api_error;
	return api_loaded;
}

void EnsureCacheQueue() {
	std::call_once(cache_queue_once, [] {
		cache_queue = agi::dispatch::Create();
	});
}

#ifdef LIBASSMOD_FEATURE_TAG_IMAGE
struct TagImage {
	std::string key;
	std::string basename_lower;
	ASS_TagImageFormat format = ASS_TAG_IMAGE_FORMAT_PNG;
	int width = 0;
	int height = 0;
	int stride = 0;
	std::vector<unsigned char> rgba;
};

std::string trim_copy(std::string str) {
	auto not_space = [](unsigned char c) { return !std::isspace(c); };
	auto begin = std::find_if(str.begin(), str.end(), not_space);
	if (begin == str.end())
		return {};
	auto end = std::find_if(str.rbegin(), str.rend(), not_space).base();
	return std::string(begin, end);
}

std::string to_lower_copy(std::string str) {
	std::transform(str.begin(), str.end(), str.begin(), [](char c) {
		if (c >= 'A' && c <= 'Z')
			return static_cast<char>(c - 'A' + 'a');
		return c;
	});
	return str;
}

std::string path_basename(std::string const& path) {
	size_t cut = path.find_last_of("/\\");
	if (cut == std::string::npos)
		return path;
	return path.substr(cut + 1);
}

std::string strip_matching_quotes(std::string path) {
	path = trim_copy(std::move(path));
	if (path.size() >= 2) {
		bool quoted = (path.front() == '"' && path.back() == '"')
			|| (path.front() == '\'' && path.back() == '\'');
		if (quoted)
			path = path.substr(1, path.size() - 2);
	}
	return trim_copy(std::move(path));
}

std::string add_double_quotes(std::string const& path) {
	std::string quoted;
	quoted.reserve(path.size() + 2);
	quoted.push_back('"');
	quoted += path;
	quoted.push_back('"');
	return quoted;
}

void append_unique_candidate(std::vector<wxString> *candidates, wxString const& path) {
	if (path.empty())
		return;
	if (std::find(candidates->begin(), candidates->end(), path) == candidates->end())
		candidates->push_back(path);
}

std::string wx_to_utf8_copy(wxString const& value) {
	return std::string(value.utf8_str());
}

static inline char img_ascii_tolower(char c) {
	if (c >= 'A' && c <= 'Z')
		return static_cast<char>(c - 'A' + 'a');
	return c;
}

bool img_starts_with_icase(const char *text, size_t len, const char *prefix) {
	for (size_t i = 0; prefix[i]; ++i) {
		if (i >= len)
			return false;
		if (img_ascii_tolower(text[i]) != prefix[i])
			return false;
	}
	return true;
}

bool path_is_absolute(std::string const& path) {
	std::vector<wxString> bases;
	append_unique_candidate(&bases, wxString::FromUTF8(path.c_str()));
	if (!path.empty())
		append_unique_candidate(&bases, wxString::FromUTF8Unchecked(path.c_str()));
	if (!path.empty())
		append_unique_candidate(&bases, wxString(path.c_str(), wxConvLocal));

	for (auto const& base : bases) {
		wxFileName fname(base);
		if (fname.IsAbsolute())
			return true;
	}
	return false;
}

std::vector<wxString> file_image_candidates(std::string const& path, wxString const& script_dir) {
	std::vector<wxString> bases;
	append_unique_candidate(&bases, wxString::FromUTF8(path.c_str()));
	if (!path.empty())
		append_unique_candidate(&bases, wxString::FromUTF8Unchecked(path.c_str()));
	if (!path.empty())
		append_unique_candidate(&bases, wxString(path.c_str(), wxConvLocal));

	std::vector<wxString> candidates;
	for (auto const& base : bases) {
		wxFileName fname(base);
		// Prefer subtitle-relative absolute path first to avoid noisy failed opens on raw relative names.
		if (!fname.IsAbsolute() && !script_dir.empty()) {
			wxFileName resolved(fname);
			wxLogNull suppress;
			resolved.MakeAbsolute(script_dir);
			append_unique_candidate(&candidates, resolved.GetFullPath());
		}
		append_unique_candidate(&candidates, base);
	}
	return candidates;
}

bool parse_tag_image_format(std::string const& path, ASS_TagImageFormat *format) {
	std::string lower = to_lower_copy(path);
	if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".png") == 0) {
		*format = ASS_TAG_IMAGE_FORMAT_PNG;
		return true;
	}
	if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".jpg") == 0) {
		*format = ASS_TAG_IMAGE_FORMAT_JPEG;
		return true;
	}
	if (lower.size() >= 5 && lower.compare(lower.size() - 5, 5, ".jpeg") == 0) {
		*format = ASS_TAG_IMAGE_FORMAT_JPEG;
		return true;
	}
	if (lower.size() >= 5 && lower.compare(lower.size() - 5, 5, ".webp") == 0) {
		*format = ASS_TAG_IMAGE_FORMAT_WEBP;
		return true;
	}
	return false;
}

void ensure_image_handlers() {
	static std::once_flag handlers_once;
	std::call_once(handlers_once, [] {
		if (!wxImage::FindHandler(wxBITMAP_TYPE_PNG))
			wxImage::AddHandler(new wxPNGHandler);
	});
}

bool decode_image_to_rgba(wxImage &image, TagImage *out) {
	if (!image.IsOk())
		return false;

	int width = image.GetWidth();
	int height = image.GetHeight();
	if (width <= 0 || height <= 0)
		return false;

	unsigned char *rgb = image.GetData();
	unsigned char *alpha = image.HasAlpha() ? image.GetAlpha() : nullptr;
	if (!rgb)
		return false;

	out->width = width;
	out->height = height;
	out->stride = width * 4;
	out->rgba.resize(static_cast<size_t>(out->stride) * static_cast<size_t>(height));

	for (int i = 0; i < width * height; ++i) {
		out->rgba[i * 4 + 0] = rgb[i * 3 + 0];
		out->rgba[i * 4 + 1] = rgb[i * 3 + 1];
		out->rgba[i * 4 + 2] = rgb[i * 3 + 2];
		out->rgba[i * 4 + 3] = alpha ? alpha[i] : 255;
	}

	return true;
}

bool decode_attachment_image(AssAttachment const& attachment, TagImage *out) {
	std::string const& entry = attachment.GetEntryData();
	size_t header_end = entry.find('\n');
	if (header_end == std::string::npos)
		return false;

	std::string header = trim_copy(entry.substr(0, header_end));
	if (header.size() < 9 || to_lower_copy(header.substr(0, 9)) != "filename:")
		return false;

	std::string filename = trim_copy(header.substr(9));
	if (filename.empty())
		return false;
	if (!parse_tag_image_format(filename, &out->format))
		return false;

	auto decoded = agi::ass::UUDecode(entry.c_str() + header_end + 1,
		entry.c_str() + entry.size());
	if (decoded.empty())
		return false;

	ensure_image_handlers();
	wxMemoryInputStream stream(decoded.data(), decoded.size());
	wxImage image;
	{
		wxLogNull suppress;
		if (!image.LoadFile(stream, wxBITMAP_TYPE_ANY))
			return false;
	}
	if (!decode_image_to_rgba(image, out))
		return false;

	out->key = filename;
	out->basename_lower = to_lower_copy(path_basename(filename));
	return true;
}

bool decode_file_image(std::string const& path, wxString const& script_dir,
	TagImage *out, std::string *resolved_path) {
	if (!parse_tag_image_format(path, &out->format))
		return false;
	if (resolved_path)
		resolved_path->clear();

	ensure_image_handlers();
	for (auto const& candidate : file_image_candidates(path, script_dir)) {
		if (!wxFileName::FileExists(candidate))
			continue;

		wxImage image;
		{
			wxLogNull suppress;
			if (!image.LoadFile(candidate, wxBITMAP_TYPE_ANY))
				continue;
		}
		if (!decode_image_to_rgba(image, out))
			return false;

		out->key = path;
		out->basename_lower = to_lower_copy(path_basename(path));
		if (resolved_path)
			*resolved_path = wx_to_utf8_copy(candidate);
		return true;
	}

	std::string basename_lower = to_lower_copy(path_basename(path));
	if (!script_dir.empty() && !basename_lower.empty()) {
		wxDir dir(script_dir);
		if (dir.IsOpened()) {
			wxString entry;
			bool more = dir.GetFirst(&entry, wxEmptyString, wxDIR_FILES);
			while (more) {
				if (to_lower_copy(wx_to_utf8_copy(entry)) == basename_lower) {
					wxString candidate = wxFileName(script_dir, entry).GetFullPath();
					wxImage image;
					{
						wxLogNull suppress;
						if (!image.LoadFile(candidate, wxBITMAP_TYPE_ANY)) {
							more = dir.GetNext(&entry);
							continue;
						}
					}
					if (!decode_image_to_rgba(image, out))
						return false;

					out->key = path;
					out->basename_lower = basename_lower;
					if (resolved_path)
						*resolved_path = wx_to_utf8_copy(candidate);
					return true;
				}
				more = dir.GetNext(&entry);
			}
		}
	}

	return false;
}

void collect_img_paths_from_span(const char *data, size_t len,
	std::vector<std::string> *paths, std::unordered_set<std::string> *seen) {
	for (size_t i = 0; i + 4 < len; ++i) {
		if (data[i] != '\\')
			continue;

		size_t j = i + 1;
		if (j < len && data[j] >= '1' && data[j] <= '4')
			++j;
		if (j + 2 >= len)
			continue;
		if (data[j] != 'i' || data[j + 1] != 'm' || data[j + 2] != 'g')
			continue;

		j += 3;
		while (j < len && (data[j] == ' ' || data[j] == '\t'))
			++j;
		if (j >= len || data[j] != '(')
			continue;

		++j;
		while (j < len && (data[j] == ' ' || data[j] == '\t'))
			++j;

		if (j >= len)
			continue;

		size_t start = j;
		size_t end = j;
		if (data[j] == '"' || data[j] == '\'') {
			char quote = data[j++];
			start = j;
			while (j < len && data[j] != quote)
				++j;
			end = j;
		} else {
			while (j < len && data[j] != ',' && data[j] != ')')
				++j;
			end = j;
		}
		if (end <= start)
			continue;

		std::string path(data + start, data + end);
		path = strip_matching_quotes(path);
		if (!path.empty() && seen->insert(path).second)
			paths->push_back(path);
	}
}

std::vector<std::string> collect_img_paths(const char *data, size_t len) {
	std::vector<std::string> paths;
	std::unordered_set<std::string> seen;
	bool saw_section = false;
	bool in_events = true;

	size_t line_start = 0;
	for (size_t i = 0; i <= len; ++i) {
		if (i < len && data[i] != '\n')
			continue;

		size_t start = line_start;
		size_t end = i;
		line_start = i + 1;

		if (end > start && data[end - 1] == '\r')
			--end;
		while (start < end && (data[start] == ' ' || data[start] == '\t'))
			++start;
		while (end > start && (data[end - 1] == ' ' || data[end - 1] == '\t'))
			--end;
		if (end <= start)
			continue;

		const char *line = data + start;
		size_t line_len = end - start;
		if (line[0] == '[' && line[line_len - 1] == ']') {
			saw_section = true;
			size_t sec_start = 1;
			size_t sec_end = line_len - 1;
			while (sec_start < sec_end && (line[sec_start] == ' ' || line[sec_start] == '\t'))
				++sec_start;
			while (sec_end > sec_start && (line[sec_end - 1] == ' ' || line[sec_end - 1] == '\t'))
				--sec_end;
			in_events = (sec_end - sec_start == 6)
				&& img_starts_with_icase(line + sec_start, sec_end - sec_start, "events");
			continue;
		}

		if (saw_section && !in_events)
			continue;
		if (!img_starts_with_icase(line, line_len, "dialogue:"))
			continue;

		collect_img_paths_from_span(line, line_len, &paths, &seen);
	}

	return paths;
}
#endif

// Stuff used on the cache thread, owned by a shared_ptr in case the provider
// gets deleted before the cache finishing updating
struct cache_thread_shared {
	ASS_Renderer *renderer = nullptr;
	std::atomic<bool> ready{false};
	~cache_thread_shared() {
		if (renderer && api.ass_renderer_done)
			api.ass_renderer_done(renderer);
	}
};

class LibassModSubtitlesProvider final : public SubtitlesProvider {
	agi::BackgroundRunner *br;
	std::shared_ptr<cache_thread_shared> shared;
	ASS_Track *ass_track = nullptr;

#ifdef LIBASSMOD_FEATURE_TAG_IMAGE
	std::vector<TagImage> attachment_tag_images;
	std::unordered_map<std::string, TagImage> file_tag_image_cache;
	std::vector<std::string> tag_image_paths;
	wxString tag_image_script_dir;
	bool tag_images_dirty = false;

	void PrepareSubtitles(AssFile *subs, int) override {
		wxString script_dir;
		if (!subs->Filename.empty())
			script_dir = wxString(subs->Filename.parent_path().wstring().c_str());
		if (script_dir != tag_image_script_dir)
			file_tag_image_cache.clear();
		tag_image_script_dir = script_dir;

		attachment_tag_images.clear();
		for (auto const& attachment : subs->Attachments) {
			if (attachment.Group() != AssEntryGroup::GRAPHIC)
				continue;

			TagImage image;
			if (decode_attachment_image(attachment, &image))
				attachment_tag_images.push_back(std::move(image));
		}
	}

	void RegisterTagImages() {
		if (!tag_images_dirty)
			return;

		ASS_Renderer *ass_renderer = renderer();
		if (!ass_renderer)
			return;
		if (!api.ass_clear_tag_images || !api.ass_set_tag_image_rgba) {
			static std::once_flag missing_tag_api_once;
			std::call_once(missing_tag_api_once, [] {
				LOG_W("subtitle/provider/libassmod")
					<< "libassmod tag-image API not available (missing ass_clear_tag_images/ass_set_tag_image_rgba)";
			});
			tag_images_dirty = false;
			return;
		}

		api.ass_clear_tag_images(ass_renderer);

		std::unordered_set<std::string> registered_paths;
		auto register_key = [&](std::string const& key, ASS_TagImageFormat format,
			int width, int height, int stride, const unsigned char *rgba) {
			if (key.empty())
				return;
			if (registered_paths.find(key) != registered_paths.end())
				return;
			if (api.ass_set_tag_image_rgba(ass_renderer, key.c_str(), format,
				width, height, stride, rgba) >= 0) {
				registered_paths.insert(key);
			}
		};
		auto register_path_variants = [&](std::string const& key, ASS_TagImageFormat format,
			int width, int height, int stride, const unsigned char *rgba) {
			std::string clean = strip_matching_quotes(key);
			if (clean.empty())
				return;
			register_key(clean, format, width, height, stride, rgba);
			register_key(add_double_quotes(clean), format, width, height, stride, rgba);
		};

		std::unordered_map<std::string, const TagImage *> attachment_by_name;
		for (auto const& image : attachment_tag_images) {
			attachment_by_name.emplace(image.basename_lower, &image);
			register_path_variants(image.key, image.format,
				image.width, image.height, image.stride, image.rgba.data());
		}

		for (auto const& raw_path : tag_image_paths) {
			std::string path = strip_matching_quotes(raw_path);
			if (path.empty())
				continue;

			ASS_TagImageFormat format;
			if (!parse_tag_image_format(path, &format))
				continue;

			bool absolute_path = path_is_absolute(path);

			auto file_it = file_tag_image_cache.find(path);
			if (file_it == file_tag_image_cache.end()) {
				TagImage file_image;
				std::string resolved_file_path;
				if (decode_file_image(path, tag_image_script_dir, &file_image, &resolved_file_path)) {
					file_image.key = resolved_file_path.empty() ? path : resolved_file_path;
					file_it = file_tag_image_cache.emplace(path, std::move(file_image)).first;
				}
			}
			if (file_it != file_tag_image_cache.end()) {
				auto const& image = file_it->second;
				if (image.format == format) {
					register_path_variants(path, image.format,
						image.width, image.height, image.stride, image.rgba.data());
					register_path_variants(image.key, image.format,
						image.width, image.height, image.stride, image.rgba.data());
					continue;
				}
			}

			if (absolute_path)
				continue;

			std::string base = to_lower_copy(path_basename(path));
			auto attachment_it = attachment_by_name.find(base);
			if (attachment_it == attachment_by_name.end())
				continue;

			auto const& image = *attachment_it->second;
			if (image.format != format)
				continue;
			register_path_variants(path, image.format,
				image.width, image.height, image.stride, image.rgba.data());
			register_path_variants(image.key, image.format,
				image.width, image.height, image.stride, image.rgba.data());
		}

		tag_images_dirty = false;
	}
#endif

	ASS_Renderer *renderer() {
		if (shared->ready)
			return shared->renderer;

		auto block = [&] {
			if (shared->ready)
				return;
			agi::util::sleep_for(250);
			if (shared->ready)
				return;
			br->Run([=](agi::ProgressSink *ps) {
				ps->SetTitle(from_wx(_("Updating font index")));
				ps->SetMessage(from_wx(_("This may take several minutes")));
				ps->SetIndeterminate();
				while (!shared->ready && !ps->IsCancelled())
					agi::util::sleep_for(250);
			});
		};

		if (wxThread::IsMain())
			block();
		else
			agi::dispatch::Main().Sync(block);
		return shared->renderer;
	}

public:
	LibassModSubtitlesProvider(agi::BackgroundRunner *br);
	~LibassModSubtitlesProvider();

	void LoadSubtitles(const char *data, size_t len) override {
		if (ass_track) api.ass_free_track(ass_track);
		ass_track = api.ass_read_memory(library, const_cast<char *>(data), len, nullptr);
		if (!ass_track) throw agi::InternalError("libassmod failed to load subtitles.");
#ifdef LIBASSMOD_FEATURE_TAG_IMAGE
		tag_image_paths = collect_img_paths(data, len);
		tag_images_dirty = true;
#endif
	}

	void DrawSubtitles(VideoFrame &dst, double time) override;

	void Reinitialize() override {
		// No need to reinit if we're not even done with the initial init
		if (!shared->ready)
			return;

		api.ass_renderer_done(shared->renderer);
		shared->renderer = api.ass_renderer_init(library);
		api.ass_set_font_scale(shared->renderer, 1.);
		api.ass_set_fonts(shared->renderer, nullptr, "Sans", 1, nullptr, true);
#ifdef LIBASSMOD_FEATURE_TAG_IMAGE
		tag_images_dirty = true;
#endif
	}
};

LibassModSubtitlesProvider::LibassModSubtitlesProvider(agi::BackgroundRunner *br)
: br(br)
, shared(std::make_shared<cache_thread_shared>())
{
	std::string error;
	if (!EnsureLibassMod(&error))
		throw agi::InternalError("libassmod unavailable: " + error);

	EnsureCacheQueue();
	auto state = shared;
	cache_queue->Async([state] {
		auto ass_renderer = api.ass_renderer_init(library);
		if (ass_renderer) {
			api.ass_set_font_scale(ass_renderer, 1.);
			api.ass_set_fonts(ass_renderer, nullptr, "Sans", 1, nullptr, true);
		}
		state->renderer = ass_renderer;
		state->ready = true;
	});
}

LibassModSubtitlesProvider::~LibassModSubtitlesProvider() {
	if (ass_track) api.ass_free_track(ass_track);
}

#define _r(c) ((c)>>24)
#define _g(c) (((c)>>16)&0xFF)
#define _b(c) (((c)>>8)&0xFF)
#define _a(c) ((c)&0xFF)

void LibassModSubtitlesProvider::DrawSubtitles(VideoFrame &frame, double time) {
	ASS_Renderer *ass_renderer = renderer();
	if (!ass_renderer || !ass_track)
		return;
#ifdef LIBASSMOD_FEATURE_TAG_IMAGE
	RegisterTagImages();
#endif

	api.ass_set_frame_size(ass_renderer, frame.width, frame.height);
	// Note: this relies on Aegisub always rendering at video storage res
	api.ass_set_storage_size(ass_renderer, frame.width, frame.height);

	int detect_change = 0;
	ASS_RenderResult render_result = api.ass_render_frame_auto(ass_renderer, ass_track, int(time * 1000), &detect_change);

	// libassmod returns either premultiplied RGBA images or the legacy alpha-masked monochrome list.
	// Blend whichever list is preferred by the renderer into the frame.

	using namespace boost::gil;
	auto dst = interleaved_view(frame.width, frame.height, (bgra8_pixel_t*)frame.data.data(), frame.width * 4);
	if (frame.flipped)
		dst = flipped_up_down_view(dst);

	if (render_result.use_rgba && render_result.imgs_rgba) {
		for (ASS_ImageRGBA *img = render_result.imgs_rgba; img; img = img->next) {
			auto srcview = interleaved_view(img->w, img->h, (rgba8_pixel_t*)img->rgba, img->stride);
			auto dstview = subimage_view(dst, img->dst_x, img->dst_y, img->w, img->h);

			transform_pixels(dstview, srcview, dstview, [](const bgra8_pixel_t frame_px, const rgba8_pixel_t src_px) -> bgra8_pixel_t {
				unsigned int alpha = src_px[3];
				unsigned int inv_alpha = 255 - alpha;

				bgra8_pixel_t ret;
				ret[0] = static_cast<unsigned char>(src_px[2] + (frame_px[0] * inv_alpha) / 255);
				ret[1] = static_cast<unsigned char>(src_px[1] + (frame_px[1] * inv_alpha) / 255);
				ret[2] = static_cast<unsigned char>(src_px[0] + (frame_px[2] * inv_alpha) / 255);
				ret[3] = 0;
				return ret;
			});
		}
	}
	else {
		for (ASS_Image *img = render_result.imgs; img; img = img->next) {
			unsigned int opacity = 255 - ((unsigned int)_a(img->color));
			unsigned int r = (unsigned int)_r(img->color);
			unsigned int g = (unsigned int)_g(img->color);
			unsigned int b = (unsigned int)_b(img->color);

			auto srcview = interleaved_view(img->w, img->h, (gray8_pixel_t*)img->bitmap, img->stride);
			auto dstview = subimage_view(dst, img->dst_x, img->dst_y, img->w, img->h);

			transform_pixels(dstview, srcview, dstview, [=](const bgra8_pixel_t frame_px, const gray8_pixel_t src_px) -> bgra8_pixel_t {
				unsigned int k = ((unsigned)src_px) * opacity / 255;
				unsigned int ck = 255 - k;

				bgra8_pixel_t ret;
				ret[0] = (k * b + ck * frame_px[0]) / 255;
				ret[1] = (k * g + ck * frame_px[1]) / 255;
				ret[2] = (k * r + ck * frame_px[2]) / 255;
				ret[3] = 0;
				return ret;
			});
		}
	}

	if (render_result.imgs_rgba)
		api.ass_free_images_rgba(render_result.imgs_rgba);
}
}

namespace libassmod {
std::unique_ptr<SubtitlesProvider> Create(std::string const&, agi::BackgroundRunner *br) {
	return agi::make_unique<LibassModSubtitlesProvider>(br);
}

bool IsAvailable(std::string *error) {
	return EnsureLibassMod(error);
}

std::string PrimaryLibraryName() {
#ifdef _WIN32
	return "libassmod.dll";
#elif defined(__APPLE__)
	return "libassmod.dylib";
#else
	return "libassmod.so";
#endif
}

void CacheFonts() {
	std::string error;
	if (!EnsureLibassMod(&error)) {
		LOG_I("subtitle/provider/libassmod") << "libassmod unavailable: " << error;
		return;
	}

	EnsureCacheQueue();

	cache_queue->Async([] {
		auto ass_renderer = api.ass_renderer_init(library);
		if (!ass_renderer)
			return;
		api.ass_set_fonts(ass_renderer, nullptr, "Sans", 1, nullptr, true);
		api.ass_renderer_done(ass_renderer);
	});
}
}
