//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "procedural_sidecar.h"
#include "json.h"
#include "gui.h"

#include <algorithm>
#include <fstream>

namespace {
	static std::string PresetToString(GenerationPreset preset) {
		switch (preset) {
			case GenerationPreset::Forest: return "forest";
			case GenerationPreset::Desert: return "desert";
			case GenerationPreset::Cave: return "cave";
			case GenerationPreset::City: return "city";
			case GenerationPreset::Coast: return "coast";
			case GenerationPreset::Mountain: return "mountain";
			case GenerationPreset::Ice: return "ice";
			default: return "auto";
		}
	}

	static GenerationPreset PresetFromString(const std::string& value) {
		const std::string lower = as_lower_str(value);
		if (lower == "forest") return GenerationPreset::Forest;
		if (lower == "desert") return GenerationPreset::Desert;
		if (lower == "cave") return GenerationPreset::Cave;
		if (lower == "city") return GenerationPreset::City;
		if (lower == "coast") return GenerationPreset::Coast;
		if (lower == "mountain") return GenerationPreset::Mountain;
		if (lower == "ice") return GenerationPreset::Ice;
		return GenerationPreset::Auto;
	}

	static wxString ResolveSidecarScript(const json::mObject& config) {
		json::mObject::const_iterator it = config.find("script");
		if (it == config.end() || it->second.type() != json::str_type) {
			return wxEmptyString;
		}
		wxString script = wxstr(it->second.get_str());
		if (script.empty()) {
			return wxEmptyString;
		}
		if (wxFileName(script).IsAbsolute() && wxFileName::FileExists(script)) {
			return script;
		}
		const wxString execPath = wxstr(g_gui.GetExecDirectory()) + script;
		if (wxFileName::FileExists(execPath)) {
			return execPath;
		}
		const wxString dataPath = wxstr(g_gui.GetDataDirectory()) + "../" + script;
		if (wxFileName::FileExists(dataPath)) {
			return dataPath;
		}
		return execPath;
	}
}

bool ProceduralSidecar::TryEnhance(GenerationSpec& spec, wxString& error) {
	if (!spec.useSidecar) {
		return true;
	}

	const wxString configPath = wxstr(g_gui.GetDataDirectory()) + "procedural/sidecar.json";
	std::ifstream configInput(nstr(configPath).c_str());
	if (!configInput.good()) {
		error = "Sidecar config not found: " + configPath;
		return false;
	}

	json::mValue configRoot;
	if (!json::read(configInput, configRoot) || configRoot.type() != json::obj_type) {
		error = "Invalid sidecar config JSON.";
		return false;
	}

	const json::mObject& config = configRoot.get_obj();
	json::mObject::const_iterator enabledIt = config.find("enabled");
	if (enabledIt == config.end() || enabledIt->second.type() != json::bool_type || !enabledIt->second.get_bool()) {
		return true;
	}

	const wxString scriptPath = ResolveSidecarScript(config);
	if (scriptPath.empty() || !wxFileName::FileExists(scriptPath)) {
		error = "Sidecar script not found: " + scriptPath;
		return false;
	}

	const wxString tempDir = wxFileName::GetTempDir();
	const wxString requestPath = tempDir + wxFileName::GetPathSeparator() + "rme_pg_request.json";
	const wxString responsePath = tempDir + wxFileName::GetPathSeparator() + "rme_pg_response.json";

	json::mObject request;
	request["prompt"] = json::mValue(nstr(spec.textPrompt.prompt));
	request["preset"] = json::mValue(PresetToString(spec.preset));
	request["seed"] = json::mValue(static_cast<int>(spec.seed));
	request["elevation"] = json::mValue(spec.elevation.maxLevels);
	request["referenceImage"] = json::mValue(nstr(spec.imageMask.imagePath));

	std::ofstream requestOut(nstr(requestPath).c_str());
	if (!requestOut.good()) {
		error = "Could not write sidecar request file.";
		return false;
	}
	json::write(json::mValue(request), requestOut);
	requestOut.close();

	wxString command;
	command << wxT("\"") << scriptPath << wxT("\" \"") << requestPath << wxT("\" \"") << responsePath << wxT("\"");
	const int exitCode = wxExecute(command, wxEXEC_SYNC | wxEXEC_HIDE_CONSOLE);
	if (exitCode != 0) {
		error = wxString::Format("Sidecar script failed (exit %d).", exitCode);
		return false;
	}

	std::ifstream responseIn(nstr(responsePath).c_str());
	if (!responseIn.good()) {
		error = "Sidecar did not produce response.json.";
		return false;
	}

	json::mValue responseRoot;
	if (!json::read(responseIn, responseRoot) || responseRoot.type() != json::obj_type) {
		error = "Invalid sidecar response JSON.";
		return false;
	}

	const json::mObject& response = responseRoot.get_obj();
	json::mObject::const_iterator okIt = response.find("ok");
	if (okIt != response.end() && okIt->second.type() == json::bool_type && !okIt->second.get_bool()) {
		error = "Sidecar returned ok=false.";
		return false;
	}

	json::mObject::const_iterator presetIt = response.find("preset");
	if (presetIt != response.end() && presetIt->second.type() == json::str_type) {
		spec.preset = PresetFromString(presetIt->second.get_str());
	}

	json::mObject::const_iterator elevIt = response.find("elevation");
	if (elevIt != response.end() && elevIt->second.type() == json::int_type) {
		spec.elevation.maxLevels = elevIt->second.get_int();
	}

	json::mObject::const_iterator depthIt = response.find("depthLevels");
	if (depthIt != response.end() && depthIt->second.type() == json::int_type) {
		spec.elevation.maxLevels = std::max(spec.elevation.maxLevels, depthIt->second.get_int());
	}

	json::mObject::const_iterator maskIt = response.find("maskPath");
	if (maskIt != response.end() && maskIt->second.type() == json::str_type) {
		spec.imageMask.imagePath = wxstr(maskIt->second.get_str());
	}

	return true;
}
