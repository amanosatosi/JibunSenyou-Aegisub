// Theme preset loader: copies the "Colour" block from a theme JSON into options.

#include "theme_preset.h"

#include "options.h"

#include <algorithm>
#include <cctype>
#include <libaegisub/exception.h>
#include <libaegisub/color.h>
#include <libaegisub/fs.h>
#include <libaegisub/io.h>
#include <libaegisub/json.h>
#include <libaegisub/log.h>
#include <libaegisub/path.h>
#include <unordered_map>
#include <vector>

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

void EnsureThemesInUserDir() {
	auto user_dir = config::path->Decode("?user/themes");
	auto data_dir = config::path->Decode("?data/themes");

	if (!agi::fs::DirectoryExists(data_dir))
		return;

	try {
		agi::fs::CreateDirectory(user_dir);
	}
	catch (agi::fs::FileSystemError const&) {
		return;
	}

	static const std::vector<std::string> builtin_ids = {
		"aegisub_default",
		"dark_mode_unofficial",
		"ayu_light",
		"ayu_dark"
	};

	for (auto const& id : builtin_ids) {
		auto src = data_dir / (id + ".json");
		auto dst = user_dir / (id + ".json");
		if (!agi::fs::FileExists(src) || agi::fs::FileExists(dst))
			continue;
		try {
			agi::fs::Copy(src, dst);
			LOG_I("theme_preset") << "Copied theme preset '" << id << "' to user dir.";
		}
		catch (agi::fs::FileSystemError const&) {
			LOG_W("theme_preset") << "Failed copying theme preset '" << id << "'.";
		}
	}
}

std::vector<ThemeInfo> ListAvailableThemes() {
	std::vector<ThemeInfo> result;
	auto user_dir = config::path->Decode("?user/themes");
	if (!agi::fs::DirectoryExists(user_dir))
		return result;

	std::unordered_map<std::string, ThemeInfo> found;

	for (agi::fs::DirectoryIterator it(user_dir, "*.json"); it != agi::fs::DirectoryIterator(); ++it) {
		auto const& entry = *it;
		std::string id = entry.stem().string();
		std::string name = id;

		try {
			auto stream = agi::io::Open(entry);
			auto root = agi::json_util::parse(*stream);
			auto const& obj = static_cast<json::Object const&>(root);
			auto it_name = obj.find("Name");
			if (it_name != obj.end())
				name = static_cast<json::String const&>(it_name->second);
		}
		catch (...) {
			// Ignore malformed themes; keep fallback name.
		}

		// Basic prettifying: replace underscores with spaces and capitalise words
		std::replace(name.begin(), name.end(), '_', ' ');
		bool cap = true;
		for (char &c : name) {
			if (std::isspace(static_cast<unsigned char>(c))) {
				cap = true;
				continue;
			}
			if (cap) {
				c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
				cap = false;
			} else {
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
		}

		found[id] = ThemeInfo{id, name};
	}

	static const std::vector<std::string> builtin_order = {
		"aegisub_default",
		"dark_mode_unofficial",
		"ayu_light",
		"ayu_dark"
	};
	for (auto const& id : builtin_order) {
		auto it = found.find(id);
		if (it != found.end()) {
			result.push_back(it->second);
			found.erase(it);
		}
	}

	std::vector<std::string> remaining;
	remaining.reserve(found.size());
	for (auto const& kv : found) remaining.push_back(kv.first);
	std::sort(remaining.begin(), remaining.end());
	for (auto const& id : remaining)
		result.push_back(found[id]);

	return result;
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

static bool ApplyArrayOption(agi::OptionValue *opt, json::UnknownElement const& val, std::string *error) {
	using json::Array;
	using json::Object;
	using json::String;

	Array const& arr = static_cast<Array const&>(val);

	switch (opt->GetType()) {
	case agi::OptionType::ListString: {
		std::vector<std::string> out;
		out.reserve(arr.size());
		for (auto const& elem : arr) {
			try {
				Object const& obj = static_cast<Object const&>(elem);
				if (obj.size() != 1 || obj.begin()->first != "string")
					continue;
				out.push_back(static_cast<String const&>(obj.begin()->second));
			}
			catch (json::Exception const&) { }
		}
		opt->SetListString(out);
		return true;
	}
	case agi::OptionType::ListInt: {
		std::vector<int64_t> out;
		out.reserve(arr.size());
		for (auto const& elem : arr) {
			try {
				Object const& obj = static_cast<Object const&>(elem);
				if (obj.size() != 1 || obj.begin()->first != "int")
					continue;
				out.push_back(static_cast<json::Integer const&>(obj.begin()->second));
			}
			catch (json::Exception const&) { }
		}
		opt->SetListInt(out);
		return true;
	}
	case agi::OptionType::ListDouble: {
		std::vector<double> out;
		out.reserve(arr.size());
		for (auto const& elem : arr) {
			try {
				Object const& obj = static_cast<Object const&>(elem);
				if (obj.size() != 1 || obj.begin()->first != "double")
					continue;
				out.push_back(static_cast<json::Double const&>(obj.begin()->second));
			}
			catch (json::Exception const&) { }
		}
		opt->SetListDouble(out);
		return true;
	}
	case agi::OptionType::ListColor: {
		std::vector<agi::Color> out;
		out.reserve(arr.size());
		for (auto const& elem : arr) {
			try {
				Object const& obj = static_cast<Object const&>(elem);
				if (obj.size() != 1 || obj.begin()->first != "color")
					continue;
				out.emplace_back(static_cast<String const&>(obj.begin()->second));
			}
			catch (json::Exception const&) { }
		}
		opt->SetListColor(out);
		return true;
	}
	case agi::OptionType::ListBool: {
		std::vector<bool> out;
		out.reserve(arr.size());
		for (auto const& elem : arr) {
			try {
				Object const& obj = static_cast<Object const&>(elem);
				if (obj.size() != 1 || obj.begin()->first != "bool")
					continue;
				out.push_back(static_cast<json::Boolean const&>(obj.begin()->second));
			}
			catch (json::Exception const&) { }
		}
		opt->SetListBool(out);
		return true;
	}
	default:
		break;
	}
	if (error) *error = "Unsupported list option type";
	return false;
}

static bool ApplyOptionValue(const std::string& path, json::UnknownElement const& val, std::string *error) {
	agi::OptionValue *opt = nullptr;
	try {
		opt = OPT_SET(path);
	}
	catch (agi::InternalError const&) {
		LOG_W("theme_preset") << "Theme key '" << path << "' not found in options; skipping.";
		return true; // benign skip
	}

	switch (opt->GetType()) {
	case agi::OptionType::String:
		try { opt->SetString(static_cast<json::String const&>(val)); return true; }
		catch (json::Exception const&) { if (error) *error = "Expected string"; return false; }
	case agi::OptionType::Int:
		try { opt->SetInt(static_cast<json::Integer const&>(val)); return true; }
		catch (json::Exception const&) { if (error) *error = "Expected integer"; return false; }
	case agi::OptionType::Double:
		try { opt->SetDouble(static_cast<json::Double const&>(val)); return true; }
		catch (json::Exception const&) { if (error) *error = "Expected double"; return false; }
	case agi::OptionType::Bool:
		try { opt->SetBool(static_cast<json::Boolean const&>(val)); return true; }
		catch (json::Exception const&) { if (error) *error = "Expected bool"; return false; }
	case agi::OptionType::Color:
		try {
			agi::Color c(static_cast<json::String const&>(val));
			opt->SetColor(c);
			return true;
		}
		catch (json::Exception const&) { if (error) *error = "Expected colour string"; return false; }
	case agi::OptionType::ListString:
	case agi::OptionType::ListInt:
	case agi::OptionType::ListDouble:
	case agi::OptionType::ListColor:
	case agi::OptionType::ListBool:
		try { return ApplyArrayOption(opt, val, error); }
		catch (json::Exception const&) { if (error) *error = "Expected array"; return false; }
	}

	if (error) *error = "Unknown option type";
	return false;
}

static bool ApplyThemeObject(const json::Object& obj, const std::string& prefix, std::string *failed_key, std::string *error) {
	for (auto const& kv : obj) {
		const std::string path = prefix.empty() ? kv.first : prefix + "/" + kv.first;

		try {
			auto const& child_obj = static_cast<json::Object const&>(kv.second);
			if (!ApplyThemeObject(child_obj, path, failed_key, error))
				return false;
			continue;
		}
		catch (json::Exception const&) {
			// Not an object; fall through to value handling.
		}

		std::string local_error;
		if (!ApplyOptionValue(path, kv.second, error ? &local_error : nullptr)) {
			if (failed_key) *failed_key = path;
			if (error && error->empty()) *error = local_error;
			return false;
		}
	}
	return true;
}

bool ApplyTheme(const std::string& id, std::string *error) {
	if (id.empty())
		return false;

	agi::fs::path theme_path = ResolveThemePath(id);
	if (theme_path.empty())
		return false;

	try {
		auto stream = agi::io::Open(theme_path);
		auto root = agi::json_util::parse(*stream);
		json::Object const& obj = static_cast<json::Object const&>(root);
		auto it = obj.find("Colour");
		if (it == obj.end()) {
			LOG_W("theme_preset") << "Theme '" << id << "' missing Colour section.";
			if (error) *error = "Theme missing Colour section";
			return false;
		}

		std::string failed_key;
		if (!ApplyThemeObject(static_cast<json::Object const&>(it->second), "Colour", &failed_key, error)) {
			if (error && !failed_key.empty())
				*error = "Failed to apply key: " + failed_key + (error->empty() ? "" : " (" + *error + ")");
			return false;
		}
		return true;
	}
	catch (agi::Exception const& e) {
		LOG_E("theme_preset/apply") << "Failed to apply theme '" << id << "': " << e.GetMessage();
		if (error) *error = e.GetMessage();
	}
	catch (...) {
		LOG_E("theme_preset/apply") << "Failed to apply theme '" << id << "'.";
		if (error) *error = "Unknown error";
	}
	return false;
}

} // namespace theme_preset
