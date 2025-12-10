// Theme preset loader: copies the "Colour" block from a theme JSON into options.

#include "theme_preset.h"

#include "options.h"

#include <libaegisub/exception.h>
#include <libaegisub/fs.h>
#include <libaegisub/io.h>
#include <libaegisub/json.h>
#include <libaegisub/log.h>
#include <libaegisub/path.h>

namespace theme_preset {

std::string GetThemeDir() {
	auto dir = config::path->Decode("?user/themes");
	try {
		agi::fs::CreateDirectory(dir);
	}
	catch (agi::fs::FileSystemError const&) {
		// Ignore; failing to create the directory just means user themes won't be found.
	}
	return dir.string();
}

static agi::fs::path ResolveThemePath(const std::string& id) {
	agi::fs::path user_path = agi::fs::path(GetThemeDir()) / (id + ".json");
	if (agi::fs::FileExists(user_path))
		return user_path;

	agi::fs::path data_path = config::path->Decode("?data/themes/" + id + ".json");
	if (agi::fs::FileExists(data_path))
		return data_path;

	return agi::fs::path();
}

bool ApplyTheme(const std::string& id) {
	if (id.empty())
		return false;

	agi::fs::path theme_path = ResolveThemePath(id);
	if (theme_path.empty())
		return false;

	try {
		auto stream = agi::io::Open(theme_path);
		config::opt->ConfigNext(*stream);
		return true;
	}
	catch (agi::Exception const& e) {
		LOG_E("theme_preset/apply") << "Failed to apply theme '" << id << "': " << e.GetMessage();
	}
	catch (...) {
		LOG_E("theme_preset/apply") << "Failed to apply theme '" << id << "'.";
	}
	return false;
}

} // namespace theme_preset
