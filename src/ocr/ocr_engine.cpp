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
#include "../options.h"

#include <libaegisub/cajun/elements.h>
#include <libaegisub/cajun/reader.h>
#include <libaegisub/format_path.h>
#include <libaegisub/fs.h>
#include <libaegisub/io.h>
#include <libaegisub/path.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <wx/arrstr.h>
#include <wx/log.h>
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

std::string Trim(std::string text) {
	auto is_space = [](unsigned char c) { return std::isspace(c); };
	text.erase(text.begin(), std::find_if(text.begin(), text.end(), [&](unsigned char c) { return !is_space(c); }));
	text.erase(std::find_if(text.rbegin(), text.rend(), [&](unsigned char c) { return !is_space(c); }).base(), text.end());
	return text;
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

bool StartsWithJson(std::string const& text) {
	auto it = std::find_if(text.begin(), text.end(), [](unsigned char c) { return !std::isspace(c); });
	return it != text.end() && (*it == '{' || *it == '[');
}

std::string ExtractJsonFromRuntimeOutput(std::string const& text) {
	for (size_t start = 0; start < text.size(); ++start) {
		if (text[start] != '{' && text[start] != '[')
			continue;

		std::vector<char> expected_closers;
		bool in_string = false;
		bool escaped = false;

		for (size_t pos = start; pos < text.size(); ++pos) {
			char c = text[pos];
			if (in_string) {
				if (escaped)
					escaped = false;
				else if (c == '\\')
					escaped = true;
				else if (c == '"')
					in_string = false;
				continue;
			}

			if (c == '"') {
				in_string = true;
				continue;
			}

			if (c == '{')
				expected_closers.push_back('}');
			else if (c == '[')
				expected_closers.push_back(']');
			else if (c == '}' || c == ']') {
				if (expected_closers.empty() || expected_closers.back() != c)
					break;

				expected_closers.pop_back();
				if (expected_closers.empty())
					return text.substr(start, pos - start + 1);
			}
		}
	}

	return {};
}

std::vector<std::string> SplitLines(std::string const& text) {
	std::vector<std::string> lines;
	std::istringstream stream(text);
	std::string line;
	while (std::getline(stream, line))
		lines.push_back(Trim(line));
	return lines;
}

std::string ParentFolderName(std::string path) {
	std::replace(path.begin(), path.end(), '\\', '/');
	if (!path.empty() && path.back() == '/')
		path.pop_back();

	auto file_separator = path.find_last_of('/');
	if (file_separator == std::string::npos)
		return path;
	if (file_separator == 0)
		return path.substr(0, 1);

	auto parent_end = file_separator;
	auto parent_begin = path.find_last_of('/', parent_end - 1);
	if (parent_begin == std::string::npos)
		return path.substr(0, parent_end);
	return path.substr(parent_begin + 1, parent_end - parent_begin - 1);
}

std::string MissingModelFileMessage(std::string const& line) {
	auto marker = std::string("Cannot open file ");
	auto begin = line.find(marker);
	if (begin == std::string::npos)
		return {};

	begin += marker.size();
	auto end = line.find(',', begin);
	auto path = Trim(line.substr(begin, end == std::string::npos ? std::string::npos : end - begin));
	if (path.find("inference.pdmodel") == std::string::npos)
		return {};

	auto folder = ParentFolderName(path);
	if (folder.empty())
		return "PaddleOCR-json failed to load the OCR model. Missing inference.pdmodel.";
	return "PaddleOCR-json failed to load the OCR model. Missing inference.pdmodel in " + folder + ".";
}

std::string FirstUsefulRuntimeError(std::string const& stdout_text, std::string const& stderr_text) {
	std::vector<std::string> lines = SplitLines(stdout_text);
	auto stderr_lines = SplitLines(stderr_text);
	lines.insert(lines.end(), stderr_lines.begin(), stderr_lines.end());

	for (auto const& line : lines) {
		auto message = MissingModelFileMessage(line);
		if (!message.empty())
			return message;
	}

	for (auto const& line : lines) {
		if (line.empty() ||
			line.find("PaddleOCR-json") == 0 ||
			line == "Error Message Summary:" ||
			line.find("Load config from") == 0 ||
			line.find("_model_dir set to") != std::string::npos ||
			line.find("rec_char_dict_path set to") != std::string::npos)
			continue;

		if (line.find("Error") != std::string::npos ||
			line.find("Exception") != std::string::npos ||
			line.find("Cannot open file") != std::string::npos)
			return "PaddleOCR-json failed: " + line;
	}

	return "PaddleOCR-json failed before returning OCR JSON.";
}

std::string RuntimeDebugDetails(std::string const& stdout_text, std::string const& stderr_text, long code) {
	std::string details = "\n\nDebug details:\nExit code: " + std::to_string(code);
	details += "\nRuntime stdout:\n" + (stdout_text.empty() ? std::string("<empty>") : stdout_text);
	details += "\nRuntime stderr:\n" + (stderr_text.empty() ? std::string("<empty>") : stderr_text);
	return details;
}

std::string RuntimeFailureDiagnostic(std::string const& stdout_text, std::string const& stderr_text, long code) {
	return FirstUsefulRuntimeError(stdout_text, stderr_text) + RuntimeDebugDetails(stdout_text, stderr_text, code);
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

double GetElementNumber(json::UnknownElement const& element) {
	try {
		return static_cast<json::Double const&>(element);
	}
	catch (json::Exception const&) {
		return static_cast<double>(static_cast<json::Integer const&>(element));
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

struct ModelConfig {
	agi::fs::path config_path;
	agi::fs::path det_model_dir;
	agi::fs::path cls_model_dir;
	agi::fs::path rec_model_dir;
	agi::fs::path rec_char_dict_path;
	std::string missing_setting;
};

agi::fs::path ResolveModelPath(std::string configured_path, agi::fs::path const& models_dir) {
	std::replace(configured_path.begin(), configured_path.end(), '\\', '/');

	agi::fs::path path(configured_path);
	if (path.is_absolute())
		return path;

	if (configured_path == "models")
		return models_dir;

	if (configured_path.find("models/") == 0)
		return models_dir / configured_path.substr(7);

	return models_dir / configured_path;
}

std::map<std::string, std::string> ReadConfigSettings(agi::fs::path const& config_path) {
	std::map<std::string, std::string> settings;
	auto stream = agi::io::Open(config_path);

	std::string line;
	while (std::getline(*stream, line)) {
		line = Trim(line);
		if (line.empty() || line[0] == '#')
			continue;

		std::istringstream line_stream(line);
		std::string key;
		line_stream >> key;

		std::string value;
		std::getline(line_stream, value);
		value = Trim(value);
		if (!key.empty() && !value.empty())
			settings[key] = value;
	}

	return settings;
}

ModelConfig ReadModelConfig(agi::fs::path const& config_path, agi::fs::path const& models_dir, bool detect_only) {
	ModelConfig config;
	config.config_path = config_path;

	auto settings = ReadConfigSettings(config_path);
	std::vector<std::string> required_settings = {"det_model_dir"};
	if (!detect_only) {
		required_settings.push_back("rec_model_dir");
		required_settings.push_back("rec_char_dict_path");
	}
	for (auto const& key : required_settings) {
		if (!settings.count(key)) {
			config.missing_setting = key;
			return config;
		}
	}

	config.det_model_dir = ResolveModelPath(settings["det_model_dir"], models_dir);
	if (settings.count("cls_model_dir"))
		config.cls_model_dir = ResolveModelPath(settings["cls_model_dir"], models_dir);
	if (settings.count("rec_model_dir"))
		config.rec_model_dir = ResolveModelPath(settings["rec_model_dir"], models_dir);
	if (settings.count("rec_char_dict_path"))
		config.rec_char_dict_path = ResolveModelPath(settings["rec_char_dict_path"], models_dir);

	return config;
}

std::string FilesPresent(agi::fs::path const& folder) {
	if (!agi::fs::DirectoryExists(folder))
		return "<folder does not exist>";

	std::vector<std::string> files;
	for (auto const& file : agi::fs::DirectoryIterator(folder, "*"))
		files.push_back(file);

	if (files.empty())
		return "<empty>";

	std::sort(files.begin(), files.end());
	std::string joined;
	for (auto const& file : files) {
		if (!joined.empty())
			joined += ", ";
		joined += file;
	}
	return joined;
}

std::string ModelValidationDiagnostic(std::string const& message, agi::fs::path const& folder, ModelConfig const& config) {
	std::string diagnostic = message;
	diagnostic += "\n\nFolder path:\n" + folder.string();
	diagnostic += "\nFiles present:\n" + FilesPresent(folder);
	diagnostic += "\n\nActive config file path:\n" + config.config_path.string();
	diagnostic += "\nSelected det_model_dir:\n" + config.det_model_dir.string();
	diagnostic += "\nSelected cls_model_dir:\n" + config.cls_model_dir.string();
	diagnostic += "\nSelected rec_model_dir:\n" + config.rec_model_dir.string();
	diagnostic += "\nSelected dictionary path:\n" + config.rec_char_dict_path.string();
	return diagnostic;
}

std::string ValidateModelDirectory(std::string const& role, agi::fs::path const& folder, ModelConfig const& config) {
	for (auto const& file_name : {"inference.pdmodel", "inference.pdiparams"}) {
		auto file_path = folder / file_name;
		if (!agi::fs::FileExists(file_path)) {
			std::string message = "OCR model file is missing:\n" + file_path.string() +
				"\n\nPaddleOCR-json requires " + role + " model folders to contain inference.pdmodel and inference.pdiparams.";
			if (agi::fs::FileExists(folder / "inference.json"))
				message += "\n\nThis folder contains the official PP-OCRv5 Paddle 3/PIR files. The bundled PaddleOCR-json runtime cannot load that model format directly; the Windows packaging step must export the PP-OCRv5 model to the legacy Paddle Inference format first.";

			return ModelValidationDiagnostic(message, folder, config);
		}
	}

	return {};
}

std::string ValidateModelConfig(ModelConfig const& config, bool detect_only) {
	if (!config.missing_setting.empty())
		return "OCR model configuration is incomplete:\n" + config.config_path.string() + "\n\nMissing required setting: " + config.missing_setting;

	auto diagnostic = ValidateModelDirectory("detection", config.det_model_dir, config);
	if (!diagnostic.empty())
		return diagnostic;

	if (detect_only)
		return {};

	if (!config.cls_model_dir.empty()) {
		diagnostic = ValidateModelDirectory("classification", config.cls_model_dir, config);
		if (!diagnostic.empty())
			return diagnostic;
	}

	diagnostic = ValidateModelDirectory("recognition", config.rec_model_dir, config);
	if (!diagnostic.empty())
		return diagnostic;

	if (!agi::fs::FileExists(config.rec_char_dict_path))
		return ModelValidationDiagnostic(
			"OCR recognition dictionary is missing:\n" + config.rec_char_dict_path.string(),
			config.rec_char_dict_path.parent_path(),
			config);

	return {};
}

agi::fs::path RuntimeDir() {
	return config::path->Decode("?data/ocr");
}

agi::fs::path ExecutablePath(agi::fs::path const& runtime_dir) {
	return runtime_dir / "bin" / "PaddleOCR-json.exe";
}

agi::fs::path ModelsDir(agi::fs::path const& runtime_dir) {
	return runtime_dir / "models";
}

agi::fs::path ConfigPath(agi::fs::path const& models_dir) {
	return models_dir / "config_ppocrv5.txt";
}

wxString MissingOCRDiagnostic() {
	return _("OCR files are not installed. Reinstall Aegisub and select the OCR option to enable this feature.");
}

wxString IncompleteOCRDiagnostic() {
	return _("OCR files are missing or incomplete. Reinstall Aegisub and select the OCR option to repair this feature.");
}

wxString CheckRuntimeDiagnostic() {
#ifndef _WIN32
	return _("Bundled OCR is currently available only in Windows release artifacts.");
#endif

#ifndef WITH_PADDLEOCR
	return _("This build was configured without bundled PaddleOCR support. Build Windows artifacts with -Dpaddleocr=enabled.");
#endif

	auto runtime_dir = RuntimeDir();
	if (!agi::fs::DirectoryExists(runtime_dir))
		return MissingOCRDiagnostic();

	auto executable = ExecutablePath(runtime_dir);
	auto models_dir = ModelsDir(runtime_dir);
	auto config_path = ConfigPath(models_dir);
	if (!agi::fs::FileExists(executable) || !agi::fs::DirectoryExists(models_dir) || !agi::fs::FileExists(config_path))
		return IncompleteOCRDiagnostic();

	try {
		auto model_config = ReadModelConfig(config_path, models_dir, false);
		if (!ValidateModelConfig(model_config, false).empty())
			return IncompleteOCRDiagnostic();
	}
	catch (agi::Exception const&) {
		return IncompleteOCRDiagnostic();
	}
	catch (std::exception const&) {
		return IncompleteOCRDiagnostic();
	}

	return "";
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

void ParsePolygon(json::UnknownElement const& polygon_element, OCRLine& line) {
	auto const& polygon = static_cast<json::Array const&>(polygon_element);
	for (auto const& point_element : polygon) {
		auto const& point = static_cast<json::Array const&>(point_element);
		if (point.size() >= 2) {
			int x = static_cast<int>(std::round(GetElementNumber(point[0])));
			int y = static_cast<int>(std::round(GetElementNumber(point[1])));
			line.box.emplace_back(x, y);
		}
	}
}

OCRResult ParseImage2TextContract(json::Object const& root, OCROptions const& options) {
	OCRResult result;
	result.code = 100;

	auto backend = GetString(root, "backend");
	if (!backend.empty() && backend != "paddleocr-json") {
		result.diagnostic = "OCR runtime returned an unexpected backend: " + backend;
		return result;
	}

	auto error = GetString(root, "error");
	if (!error.empty()) {
		result.diagnostic = error;
		return result;
	}

	auto regions_it = root.find("regions");
	if (regions_it == root.end()) {
		result.diagnostic = "OCR runtime returned Image2Text JSON without a regions field.";
		return result;
	}

	auto const& regions = static_cast<json::Array const&>(regions_it->second);
	for (auto const& region : regions) {
		auto const& region_object = static_cast<json::Object const&>(region);

		OCRLine line;
		line.text = GetString(region_object, "text");
		line.confidence = GetNumber(region_object, "confidence");

		auto polygon_it = region_object.find("polygon");
		if (polygon_it != region_object.end())
			ParsePolygon(polygon_it->second, line);

		result.lines.push_back(std::move(line));
	}

	result.text = NormalizeText(result.lines, options.keep_line_breaks);
	result.ok = true;
	return result;
}

OCRResult ParsePaddleOCRJson(std::string const& json_text, OCROptions const& options) {
	OCRResult result;

	if (!StartsWithJson(json_text)) {
		result.diagnostic = "OCR runtime did not return JSON.\n\nOutput:\n" + json_text;
		return result;
	}

	try {
		std::istringstream stream(json_text);
		json::UnknownElement root_element;
		json::Reader::Read(root_element, stream);
		stream >> std::ws;
		if (!stream.eof()) {
			result.diagnostic = "OCR runtime returned extra non-JSON text around the OCR result.\n\nOutput:\n" + json_text;
			return result;
		}

		auto const& root = static_cast<json::Object const&>(root_element);
		if (root.find("regions") != root.end())
			return ParseImage2TextContract(root, options);

		result.code = GetInteger(root, "code");
		auto data_it = root.find("data");
		if (data_it == root.end()) {
			result.diagnostic = "OCR runtime returned JSON without a data field.";
			return result;
		}

		if (result.code == 100) {
			auto const& lines = static_cast<json::Array const&>(data_it->second);
			for (auto const& item : lines) {
				OCRLine line;
				try {
					auto const& item_object = static_cast<json::Object const&>(item);
					line.text = GetString(item_object, "text");
					line.confidence = GetNumber(item_object, "score");

					auto box_it = item_object.find("box");
					if (box_it != item_object.end())
						ParsePolygon(box_it->second, line);
				}
				catch (json::Exception const&) {
					ParsePolygon(item, line);
				}

				if (!line.box.empty())
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
	catch (json::Exception const&) {
		result.diagnostic = "OCR runtime returned malformed JSON instead of a clean Image2Text result.\n\nOutput:\n" + json_text;
	}
	catch (std::exception const&) {
		result.diagnostic = "OCR runtime returned an invalid Image2Text result.\n\nOutput:\n" + json_text;
	}

	return result;
}

OCREngine::OCREngine()
: runtime_dir(RuntimeDir())
, executable(ExecutablePath(runtime_dir))
, models_dir(ModelsDir(runtime_dir))
{
}

agi::fs::path OCREngine::ConfigPath(std::string const&) const {
	return ::ConfigPath(models_dir);
}

bool OCREngine::IsRuntimeAvailable() {
	return GetRuntimeDiagnostic().empty();
}

wxString OCREngine::GetRuntimeDiagnostic() {
	static wxString const diagnostic = CheckRuntimeDiagnostic();
	return diagnostic;
}

bool OCREngine::IsAvailable(OCROptions const& options) const {
	return GetDiagnostic(options).empty();
}

wxString OCREngine::GetDiagnostic(OCROptions const& options) const {
	return GetDiagnostic(options, false);
}

wxString OCREngine::GetDetectionDiagnostic(OCROptions const& options) const {
	return GetDiagnostic(options, true);
}

wxString OCREngine::GetDiagnostic(OCROptions const& options, bool detect_only) const {
	auto runtime_diagnostic = GetRuntimeDiagnostic();
	if (!runtime_diagnostic.empty())
		return runtime_diagnostic;

	try {
		auto config_path = ConfigPath(options.language);
		auto model_config = ReadModelConfig(config_path, models_dir, detect_only);
		auto model_diagnostic = ValidateModelConfig(model_config, detect_only);
		if (!model_diagnostic.empty())
			return IncompleteOCRDiagnostic();
	}
	catch (agi::Exception const&) {
		return IncompleteOCRDiagnostic();
	}
	catch (std::exception const&) {
		return IncompleteOCRDiagnostic();
	}

	return "";
}

OCRResult OCREngine::RunImage(agi::fs::path const& image_path, OCROptions const& options, bool detect_only) const {
	OCRResult result;
	auto diagnostic = GetDiagnostic(options, detect_only);
	if (!diagnostic.empty()) {
		result.diagnostic = from_wx(diagnostic);
		return result;
	}

	if (!agi::fs::FileExists(image_path)) {
		result.diagnostic = "Image file does not exist: " + image_path.string();
		return result;
	}

	try {
		auto model_config = ReadModelConfig(ConfigPath(options.language), models_dir, detect_only);
		std::string log_message =
			"Image2Text OCR starting: mode=" + std::string(detect_only ? "detect-only" : "recognize") +
			" config=" + model_config.config_path.string() +
			" det_model_dir=" + model_config.det_model_dir.string() +
			" cls_model_dir=" + model_config.cls_model_dir.string() +
			" rec_model_dir=" + model_config.rec_model_dir.string() +
			" rec_char_dict_path=" + model_config.rec_char_dict_path.string();
		wxLogMessage("%s", to_wx(log_message).c_str());
	}
	catch (agi::Exception const&) {
	}
	catch (std::exception const&) {
	}

	wxString command = QuoteArg(PathString(executable));
	command += " -image_path=" + QuoteArg(PathString(image_path));
	command += " -models_path=" + QuoteArg(PathString(models_dir));
	command += " -config_path=" + QuoteArg(PathString(ConfigPath(options.language)));
	command += " -ensure_ascii=false";
	if (detect_only) {
		command += " -det=true";
		command += " -rec=false";
		command += " -cls=false";
		command += " -use_angle_cls=false";
	}

	wxArrayString output;
	wxArrayString errors;
	long code = wxExecute(command, output, errors, wxEXEC_SYNC | wxEXEC_NODISABLE);

	std::string stdout_text = JoinOutput(output);
	std::string stderr_text = JoinOutput(errors);

	if (code != 0) {
		result.diagnostic = RuntimeFailureDiagnostic(stdout_text, stderr_text, code);
		return result;
	}

	auto json_text = ExtractJsonFromRuntimeOutput(stdout_text);
	if (json_text.empty()) {
		result.diagnostic = RuntimeFailureDiagnostic(stdout_text, stderr_text, code);
		return result;
	}

	result = ParsePaddleOCRJson(json_text, options);
	if (result.ok && detect_only) {
		for (auto& line : result.lines)
			line.text.clear();
		result.text.clear();
	}
	if (!result.ok)
		result.diagnostic += RuntimeDebugDetails(stdout_text, stderr_text, code);
	return result;
}

OCRResult OCREngine::RecognizeImage(agi::fs::path const& image_path, OCROptions const& options) const {
	return RunImage(image_path, options, false);
}

OCRResult OCREngine::DetectTextRegions(agi::fs::path const& image_path, OCROptions const& options) const {
	return RunImage(image_path, options, true);
}

} // namespace ocr
