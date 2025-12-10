// Simple theme preset loader for colour settings.

#pragma once

#include <string>

namespace theme_preset {

// Returns the directory that should contain theme JSON files (<config dir>/themes).
std::string GetThemeDir();

// Apply a theme preset by id (e.g. "aegisub_default", "dark_mode_unofficial").
// Loads <GetThemeDir()>/<id>.json (or a data-path fallback) and copies its
// "Colour" block into the current options tree.
// Returns true on success, false on any error.
bool ApplyTheme(const std::string& id);

} // namespace theme_preset
