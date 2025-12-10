// Copyright (c) 2009, Amar Takhar <verm@aegisub.org>
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

#include "libresrc.h"

#include <string_view>
#include <unordered_map>

#include <wx/bitmap.h>
#include <wx/icon.h>
#include <wx/image.h>
#include <wx/intl.h>
#include <wx/mstream.h>

namespace {
bool g_use_dark_icons = false;

struct BitmapResourceView {
	const unsigned char *data = nullptr;
	size_t size = 0;
};

#if defined(__WXMSW__) && wxVERSION_NUMBER >= 3300
const std::unordered_map<std::string_view, BitmapResourceView>& GetDarkBitmapMap() {
	static const auto map = [] {
		std::unordered_map<std::string_view, BitmapResourceView> result;
		result.reserve(libresrc_bitmaps_count);

		for (size_t i = 0; i < libresrc_bitmaps_count; ++i) {
			const auto &entry = libresrc_bitmaps[i];
			if (std::string_view(entry.path).rfind("button_dark/", 0) == 0) {
				// Cache toolbar button dark-mode assets from src/bitmaps/button_dark/.
				result.emplace(entry.name, BitmapResourceView{entry.data, entry.size});
			}
		}

		return result;
	}();
	return map;
}

BitmapResourceView GetDarkBitmap(std::string_view name) {
	const auto &map = GetDarkBitmapMap();
	auto it = map.find(name);
	if (it != map.end()) return it->second;
	return {};
}
#endif
} // namespace

void libresrc_set_dark_icons_enabled(bool enabled) {
	g_use_dark_icons = enabled;
}

wxBitmap libresrc_getimage([[maybe_unused]] const char *name, const unsigned char *buff, size_t size, double scale, int dir) {
	const unsigned char *selected = buff;
	size_t selected_size = size;

#if defined(__WXMSW__) && wxVERSION_NUMBER >= 3300
	// In the wxWidgets master build, prefer dark toolbar icons when experimental dark mode is enabled.
	if (g_use_dark_icons) {
		if (auto dark = GetDarkBitmap(name); dark.data) {
			selected = dark.data;
			selected_size = dark.size;
		}
	}
#else
	(void)name;
#endif

	wxMemoryInputStream mem(selected, selected_size);
	if (dir != wxLayout_RightToLeft)
#if wxCHECK_VERSION(3, 1, 0)
	// Since wxWidgets 3.1.0, there is an undocumented third parameter in the ctor of wxBitmap from wxImage
	// This "scale" parameter sets the logical scale factor of the created wxBitmap
		return wxBitmap(wxImage(mem), wxBITMAP_SCREEN_DEPTH, scale);
	return wxBitmap(wxImage(mem).Mirror(), wxBITMAP_SCREEN_DEPTH, scale);
#else
		return wxBitmap(wxImage(mem));
	return wxBitmap(wxImage(mem).Mirror());
#endif
}

wxIcon libresrc_geticon(const unsigned char *buff, size_t size) {
	wxMemoryInputStream mem(buff, size);
	wxIcon icon;
	icon.CopyFromBitmap(wxBitmap(wxImage(mem)));
	return icon;
}
