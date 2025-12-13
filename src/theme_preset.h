#pragma once

#include <string>
#include <vector>

namespace theme_preset {

struct ThemeInfo {
	std::string id;
	std::string name;
};

// Returns the directory that should contain theme JSON files (<config dir>/themes).
std::string GetThemeDir();

// Enumerate available themes in the user theme directory.
std::vector<ThemeInfo> ListAvailableThemes();

// Ensure built-in themes exist in the user theme directory by copying missing
// files from the installed data themes directory.
void EnsureThemesInUserDir();

// Apply a theme preset by id (e.g. "aegisub_default", "dark_mode_unofficial").
// Loads <GetThemeDir()>/<id>.json (or a data-path fallback) and copies its
// "Colour" block into the current options tree.
// Returns true on success, false on any error.
bool ApplyTheme(const std::string& id, std::string *error = nullptr);

} // namespace theme_preset
