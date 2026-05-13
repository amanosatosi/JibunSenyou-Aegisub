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
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

#include "ocr_engine.h"

#include "../compat.h"
#include "../format.h"
#include "../options.h"

#include <libaegisub/cajun/elements.h>
#include <libaegisub/cajun/reader.h>
#include <libaegisub/format_path.h>
#include <libaegisub/fs.h>
#include <libaegisub/path.h>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <wx/arrstr.h>
#include <wx/utils.h>

namespace {

wxString PathString(agi::fs::path const& path) {
	return wxString(path.wstring());
}

wxString QuoteArg(wxString arg) {
	arg.Replace("\"", "\\\"");
	return "\"" + arg + "\"";
}

std::string JoinOutput(wxArrayString const& output) {
	std::string joined;
	for (auto const& line : output) {
		if (!joined.empty())
			joined += "\n";
		joined += from_wx(line);
	}
	return joined;
}

std::string ExtractJsonObject(std::string const& output) {
	auto begin = output.find('{');
	auto end = output.rfind('}');
	if (begin == std::string::npos || end == std::string::npos || begin > end)
		return output;
	return output.substr(begin, end - begin + 1);
}

std::string TrimAndCollapse(std::string text) {
	auto is_space = [](unsigned char c) { return std::isspace(c); };
	text.erase(text.begin(), std::find_if(text.begin(), text.end(), [&](unsigned char c) { return !is_space(c); }));
	text.erase(std::find_if(text.rbegin(), text.rend(), [&](unsigned char c) { return !is_space(c); }).base(), text.end());

	std::string out;
	bool last_space = false;
	for (unsigned char c : text) {
		if (is_space(c)) {
			if (!last_space)
				out += ' ';
			last_space = true;
		}
		else {
			out += static_cast<char>(c);
			last_space = false;
		}
	}
	return out;
}

std::string GetString(json::Object const& object, char const *key) {
	auto it = object.find(key);
	if (it == object.end())
		return {};
	return static_cast<json::String const&>(it->second);
}

int GetInteger(json::Object const& object, char const *key) {
	auto it = object.find(key);
	if (it == object.end())
		return 0;
	return static_cast<int>(static_cast<json::Integer const&>(it->second));
}

double GetNumber(json::Object const& object, char const *key) {
	auto it = object.find(key);
	if (it == object.end())
		return 0.0;

	try {
		return static_cast<json::Double const&>(it->second);
	}
	catch (json::Exception const&) {
		return static_cast<double>(static_cast<json::Integer const&>(it->second));
	}
}

std::string GetDataAsString(json::UnknownElement const& data) {
	try {
		return static_cast<json::String const&>(data);
	}
	catch (json::Exception const&) {
		return {};
	}
}

} // namespace

namespace ocr {

std::string NormalizeText(std::vector<OCRLine> const& lines, bool keep_line_breaks) {
	std::string out;
	for (auto const& line : lines) {
		auto text = TrimAndCollapse(line.text);
		if (text.empty())
			continue;

		if (!out.empty())
			out += keep_line_breaks ? "\n" : " ";
		out += text;
	}
	return out;
}

OCRResult ParsePaddleOCRJson(std::string const& json_text, OCROptions const& options) {
	OCRResult result;

	try {
		std::istringstream stream(ExtractJsonObject(json_text));
		json::UnknownElement root_element;
		json::Reader::Read(root_element, stream);
		auto const& root = static_cast<json::Object const&>(root_element);

		result.code = GetInteger(root, "code");
		auto data_it = root.find("data");
		if (data_it == root.end()) {
			result.diagnostic = "OCR runtime returned JSON without a data field.";
			return result;
		}

		if (result.code == 100) {
			auto const& lines = static_cast<json::Array const&>(data_it->second);
			for (auto const& item : lines) {
				auto const& item_object = static_cast<json::Object const&>(item);

				OCRLine line;
				line.text = GetString(item_object, "text");
				line.confidence = GetNumber(item_object, "score");

				auto box_it = item_object.find("box");
				if (box_it != item_object.end()) {
					auto const& box = static_cast<json::Array const&>(box_it->second);
					for (auto const& point_element : box) {
						auto const& point = static_cast<json::Array const&>(point_element);
						if (point.size() >= 2) {
							int x = static_cast<int>(static_cast<json::Integer const&>(point[0]));
							int y = static_cast<int>(static_cast<json::Integer const&>(point[1]));
							line.box.emplace_back(x, y);
						}
					}
				}

				result.lines.push_back(std::move(line));
			}

			result.text = NormalizeText(result.lines, options.keep_line_breaks);
			result.ok = true;
			return result;
		}

		if (result.code == 101) {
			result.ok = true;
			result.text.clear();
			result.diagnostic = GetDataAsString(data_it->second);
			return result;
		}

		result.diagnostic = GetDataAsString(data_it->second);
		if (result.diagnostic.empty())
			result.diagnostic = "OCR runtime returned an error without a readable message.";
	}
	catch (json::Exception const& e) {
		result.diagnostic = "Failed to parse OCR runtime JSON: " + std::string(e.what()) + "\n\nOutput:\n" + json_text;
	}
	catch (std::exception const& e) {
		result.diagnostic = "Failed to parse OCR runtime output: " + std::string(e.what()) + "\n\nOutput:\n" + json_text;
	}

	return result;
}

OCREngine::OCREngine()
: runtime_dir(config::path->Decode("?data/ocr"))
, executable(runtime_dir / "bin" / "PaddleOCR-json.exe")
, models_dir(runtime_dir / "models")
{
}

agi::fs::path OCREngine::ConfigPath(std::string const& language) const {
	if (language == "english")
		return models_dir / "config_en.txt";
	if (language == "chinese_simplified")
		return models_dir / "config_chinese.txt";
	if (language == "chinese_traditional")
		return models_dir / "config_chinese_cht.txt";
	if (language == "korean")
		return models_dir / "config_korean.txt";
	return models_dir / "config_japan.txt";
}

bool OCREngine::IsAvailable(OCROptions const& options) const {
	return GetDiagnostic(options).empty();
}

wxString OCREngine::GetDiagnostic(OCROptions const& options) const {
#ifndef _WIN32
	return _("Bundled OCR is currently available only in Windows release artifacts.");
#endif

#ifndef WITH_PADDLEOCR
	return _("This build was configured without bundled PaddleOCR support. Build Windows artifacts with -Dpaddleocr=enabled.");
#endif

	if (!agi::fs::FileExists(executable))
		return fmt_tl("OCR runtime is missing:\n%s\n\nReinstall Aegisub or restore the bundled ocr folder next to aegisub.exe.", executable);
	if (!agi::fs::DirectoryExists(models_dir))
		return fmt_tl("OCR model folder is missing:\n%s\n\nReinstall Aegisub or restore the bundled ocr folder next to aegisub.exe.", models_dir);

	auto config_path = ConfigPath(options.language);
	if (!agi::fs::FileExists(config_path))
		return fmt_tl("OCR model configuration is missing:\n%s\n\nThe bundled ocr\\models folder is incomplete.", config_path);

	return "";
}

OCRResult OCREngine::RecognizeImage(agi::fs::path const& image_path, OCROptions const& options) const {
	OCRResult result;
	auto diagnostic = GetDiagnostic(options);
	if (!diagnostic.empty()) {
		result.diagnostic = from_wx(diagnostic);
		return result;
	}

	if (!agi::fs::FileExists(image_path)) {
		result.diagnostic = "Image file does not exist: " + image_path.string();
		return result;
	}

	wxString command = QuoteArg(PathString(executable));
	command += " -image_path=" + QuoteArg(PathString(image_path));
	command += " -models_path=" + QuoteArg(PathString(models_dir));
	command += " -config_path=" + QuoteArg(PathString(ConfigPath(options.language)));
	command += " -ensure_ascii=false";

	wxArrayString output;
	wxArrayString errors;
	long code = wxExecute(command, output, errors, wxEXEC_SYNC | wxEXEC_NODISABLE);

	std::string stdout_text = JoinOutput(output);
	std::string stderr_text = JoinOutput(errors);
	if (stdout_text.empty() && !stderr_text.empty()) {
		result.diagnostic = "OCR runtime failed:\n" + stderr_text;
		return result;
	}

	result = ParsePaddleOCRJson(stdout_text, options);
	if (!result.ok && !stderr_text.empty())
		result.diagnostic += "\n\nRuntime stderr:\n" + stderr_text;
	if (!result.ok && code != 0 && result.diagnostic.empty())
		result.diagnostic = "OCR runtime exited with code " + std::to_string(code) + ".";
	return result;
}

} // namespace ocr
