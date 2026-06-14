//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "procedural_recipe.h"
#include "json.h"

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

	static std::string SourceToString(GenerationSource source) {
		switch (source) {
			case GenerationSource::ImageMask: return "image";
			case GenerationSource::PromptWithImage: return "prompt_with_image";
			default: return "prompt";
		}
	}

	static GenerationSource SourceFromString(const std::string& value) {
		const std::string lower = as_lower_str(value);
		if (lower == "image" || lower == "image_mask") return GenerationSource::ImageMask;
		if (lower == "prompt_with_image" || lower == "combined") return GenerationSource::PromptWithImage;
		return GenerationSource::TextPrompt;
	}

	static json::mObject MakeObject() {
		return json::mObject();
	}
}

bool ProceduralRecipe::Save(const wxString& path, const GenerationSpec& spec, wxString& error) {
	json::mObject root = MakeObject();
	root["version"] = json::mValue(3);
	root["source"] = json::mValue(SourceToString(spec.source));
	root["prompt"] = json::mValue(nstr(spec.textPrompt.prompt));
	root["referenceImage"] = json::mValue(nstr(spec.imageMask.imagePath));
	root["referenceWeight"] = json::mValue(spec.reference.blendWeight);
	root["preset"] = json::mValue(PresetToString(spec.preset));
	root["seed"] = json::mValue(static_cast<int>(spec.seed));
	root["useSelection"] = json::mValue(spec.useSelection);
	root["useSidecar"] = json::mValue(spec.useSidecar);
	root["elevation"] = json::mValue(spec.elevation.maxLevels);
	root["legend"] = json::mValue(nstr(spec.imageMask.legendPath));

	json::mObject region = MakeObject();
	region["width"] = json::mValue(spec.region.width);
	region["height"] = json::mValue(spec.region.height);
	region["z"] = json::mValue(spec.region.z);
	root["region"] = region;

	json::mObject doodads = MakeObject();
	doodads["enabled"] = json::mValue(spec.doodads.enabled);
	doodads["density"] = json::mValue(spec.doodads.density);
	root["doodads"] = doodads;

	json::mObject pipeline = MakeObject();
	pipeline["borderize"] = json::mValue(spec.pipeline.borderizeAfter);
	pipeline["randomize"] = json::mValue(spec.pipeline.randomizeGround);
	pipeline["walls"] = json::mValue(spec.pipeline.placeWalls);
	root["pipeline"] = pipeline;

	std::ofstream output(nstr(path).c_str());
	if (!output.good()) {
		error = "Could not write recipe: " + path;
		return false;
	}
	json::write(json::mValue(root), output);
	return true;
}

bool ProceduralRecipe::Load(const wxString& path, GenerationSpec& spec, wxString& error) {
	std::ifstream input(nstr(path).c_str());
	if (!input.good()) {
		error = "Could not open recipe: " + path;
		return false;
	}

	json::mValue root;
	if (!json::read(input, root) || root.type() != json::obj_type) {
		error = "Invalid recipe JSON: " + path;
		return false;
	}

	const json::mObject& object = root.get_obj();
	spec = GenerationSpec();

	auto readString = [&](const char* key, wxString& out) {
		json::mObject::const_iterator it = object.find(key);
		if (it != object.end() && it->second.type() == json::str_type) {
			out = wxstr(it->second.get_str());
		}
	};

	auto readInt = [&](const char* key, int& out) {
		json::mObject::const_iterator it = object.find(key);
		if (it != object.end() && it->second.type() == json::int_type) {
			out = it->second.get_int();
		}
	};

	auto readBool = [&](const char* key, bool& out) {
		json::mObject::const_iterator it = object.find(key);
		if (it != object.end() && it->second.type() == json::bool_type) {
			out = it->second.get_bool();
		}
	};

	readString("prompt", spec.textPrompt.prompt);
	readString("referenceImage", spec.imageMask.imagePath);
	readString("legend", spec.imageMask.legendPath);
	readInt("referenceWeight", spec.reference.blendWeight);
	int seedValue = static_cast<int>(spec.seed);
	readInt("seed", seedValue);
	spec.seed = static_cast<uint32_t>(seedValue);
	readBool("useSelection", spec.useSelection);
	readBool("useSidecar", spec.useSidecar);
	readInt("elevation", spec.elevation.maxLevels);

	json::mObject::const_iterator sourceIt = object.find("source");
	if (sourceIt != object.end() && sourceIt->second.type() == json::str_type) {
		spec.source = SourceFromString(sourceIt->second.get_str());
	}

	json::mObject::const_iterator presetIt = object.find("preset");
	if (presetIt != object.end() && presetIt->second.type() == json::str_type) {
		spec.preset = PresetFromString(presetIt->second.get_str());
	}

	json::mObject::const_iterator regionIt = object.find("region");
	if (regionIt != object.end() && regionIt->second.type() == json::obj_type) {
		const json::mObject& region = regionIt->second.get_obj();
		json::mObject::const_iterator wIt = region.find("width");
		json::mObject::const_iterator hIt = region.find("height");
		json::mObject::const_iterator zIt = region.find("z");
		if (wIt != region.end() && wIt->second.type() == json::int_type) {
			spec.region.width = wIt->second.get_int();
		}
		if (hIt != region.end() && hIt->second.type() == json::int_type) {
			spec.region.height = hIt->second.get_int();
		}
		if (zIt != region.end() && zIt->second.type() == json::int_type) {
			spec.region.z = zIt->second.get_int();
		}
	}

	json::mObject::const_iterator doodadsIt = object.find("doodads");
	if (doodadsIt != object.end() && doodadsIt->second.type() == json::obj_type) {
		const json::mObject& doodads = doodadsIt->second.get_obj();
		json::mObject::const_iterator enIt = doodads.find("enabled");
		json::mObject::const_iterator denIt = doodads.find("density");
		if (enIt != doodads.end() && enIt->second.type() == json::bool_type) {
			spec.doodads.enabled = enIt->second.get_bool();
		}
		if (denIt != doodads.end() && denIt->second.type() == json::int_type) {
			spec.doodads.density = denIt->second.get_int();
		}
	}

	json::mObject::const_iterator pipelineIt = object.find("pipeline");
	if (pipelineIt != object.end() && pipelineIt->second.type() == json::obj_type) {
		const json::mObject& pipeline = pipelineIt->second.get_obj();
		json::mObject::const_iterator bIt = pipeline.find("borderize");
		json::mObject::const_iterator rIt = pipeline.find("randomize");
		json::mObject::const_iterator wIt = pipeline.find("walls");
		if (bIt != pipeline.end() && bIt->second.type() == json::bool_type) {
			spec.pipeline.borderizeAfter = bIt->second.get_bool();
		}
		if (rIt != pipeline.end() && rIt->second.type() == json::bool_type) {
			spec.pipeline.randomizeGround = rIt->second.get_bool();
		}
		if (wIt != pipeline.end() && wIt->second.type() == json::bool_type) {
			spec.pipeline.placeWalls = wIt->second.get_bool();
		}
	}

	return true;
}
