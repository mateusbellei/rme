//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "procedural_recipe.h"
#include "json.h"

#include <fstream>
#include <filesystem>

namespace {
	static std::string PresetToString(GenerationPreset preset) {
		switch (preset) {
			case GenerationPreset::Forest:
				return "forest";
			case GenerationPreset::Desert:
				return "desert";
			case GenerationPreset::Cave:
				return "cave";
			case GenerationPreset::City:
				return "city";
			case GenerationPreset::Coast:
				return "coast";
			case GenerationPreset::Mountain:
				return "mountain";
			case GenerationPreset::Ice:
				return "ice";
			default:
				return "auto";
		}
	}

	static GenerationPreset PresetFromString(const std::string& value) {
		const std::string lower = as_lower_str(value);
		if (lower == "forest") {
			return GenerationPreset::Forest;
		}
		if (lower == "desert") {
			return GenerationPreset::Desert;
		}
		if (lower == "cave") {
			return GenerationPreset::Cave;
		}
		if (lower == "city") {
			return GenerationPreset::City;
		}
		if (lower == "coast") {
			return GenerationPreset::Coast;
		}
		if (lower == "mountain") {
			return GenerationPreset::Mountain;
		}
		if (lower == "ice") {
			return GenerationPreset::Ice;
		}
		return GenerationPreset::Auto;
	}

	static std::string SourceToString(GenerationSource source) {
		switch (source) {
			case GenerationSource::WorldPlan:
				return "world";
			case GenerationSource::ImageMask:
				return "image";
			case GenerationSource::PromptWithImage:
				return "prompt_with_image";
			default:
				return "prompt";
		}
	}

	static GenerationSource SourceFromString(const std::string& value) {
		const std::string lower = as_lower_str(value);
		if (lower == "world") {
			return GenerationSource::WorldPlan;
		}
		if (lower == "image" || lower == "image_mask") {
			return GenerationSource::ImageMask;
		}
		if (lower == "prompt_with_image" || lower == "combined") {
			return GenerationSource::PromptWithImage;
		}
		return GenerationSource::TextPrompt;
	}

	static json::mObject MakeObject() {
		return json::mObject();
	}
}

bool ProceduralRecipe::Save(const wxString& path, const GenerationSpec& spec, wxString& error) {
	json::mObject root = MakeObject();
	root["version"] = json::mValue(4);
	root["source"] = json::mValue(SourceToString(spec.source));
	root["prompt"] = json::mValue(nstr(spec.textPrompt.prompt));
	root["referenceImage"] = json::mValue(nstr(spec.imageMask.imagePath));
	root["referenceWeight"] = json::mValue(spec.reference.blendWeight);
	root["preset"] = json::mValue(PresetToString(spec.preset));
	root["seed"] = json::mValue(static_cast<int64_t>(spec.seed));
	root["useSelection"] = json::mValue(spec.useSelection);
	root["useSidecar"] = json::mValue(spec.useSidecar);
	root["elevation"] = json::mValue(spec.elevation.maxLevels);
	root["legend"] = json::mValue(nstr(spec.imageMask.legendPath));

	json::mObject region = MakeObject();
	region["originX"] = json::mValue(spec.region.originX);
	region["originY"] = json::mValue(spec.region.originY);
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

	json::mObject world;
	world["shape"] = json::mValue(spec.world.settings.shape == WorldGen::LandShape::Archipelago ? "archipelago" : "continent");
	world["seaLevel"] = json::mValue(spec.world.settings.seaLevel);
	world["rivers"] = json::mValue(spec.world.settings.riverCount);
	world["settlements"] = json::mValue(spec.world.settings.settlementCount);
	world["buildings"] = json::mValue(spec.world.settings.buildingsPerSettlement);
	world["buildingLevels"] = json::mValue(spec.world.settings.buildingLevels);
	world["caveDepth"] = json::mValue(spec.world.settings.caveDepth);
	world["caveFill"] = json::mValue(spec.world.settings.caveFill);
	world["roadWidth"] = json::mValue(spec.world.settings.roadWidth);
	world["protectExisting"] = json::mValue(spec.world.protectExisting);
	world["connectionItemId"] = json::mValue(spec.world.connectionItemId);
	world["wallBrush"] = json::mValue(nstr(spec.world.wallBrushName));
	world["catalog"] = json::mValue(nstr(spec.world.catalogPath));
	root["world"] = world;

	std::ofstream output(std::filesystem::u8path(nstr(path)));
	if (!output.good()) {
		error = "Could not write recipe: " + path;
		return false;
	}
	json::write(json::mValue(root), output);
	output.flush();
	if (!output.good()) {
		error = "Failed to finish writing recipe: " + path;
		return false;
	}
	return true;
}

bool ProceduralRecipe::Load(const wxString& path, GenerationSpec& result, wxString& error) {
	std::ifstream input(std::filesystem::u8path(nstr(path)));
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
	GenerationSpec spec;
	error.clear();
	bool validNumbers = true;

	auto readString = [&](const char* key, wxString& out) {
		json::mObject::const_iterator it = object.find(key);
		if (it != object.end() && it->second.type() == json::str_type) {
			out = wxstr(it->second.get_str());
		}
	};

	auto readInt = [&](const char* key, int& out) {
		json::mObject::const_iterator it = object.find(key);
		if (it != object.end() && it->second.type() == json::int_type) {
			const auto value = it->second.get_int64();
			if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()) {
				validNumbers = false;
			} else {
				out = static_cast<int>(value);
			}
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

	int recipeVersion = 3;
	const auto version = object.find("version");
	if (version != object.end()) {
		if (version->second.type() != json::int_type || version->second.get_int64() < 1 || version->second.get_int64() > 4) {
			error = "Unsupported recipe version.";
			return false;
		}
		recipeVersion = static_cast<int>(version->second.get_int64());
	}
	const auto seedIt = object.find("seed");
	if (seedIt != object.end()) {
		if (seedIt->second.type() != json::int_type || seedIt->second.get_int64() < (recipeVersion <= 3 ? -2147483648LL : 0LL) || seedIt->second.get_int64() > 0xFFFFFFFFLL) {
			error = "Recipe seed must be an unsigned 32-bit integer.";
			return false;
		}
		spec.seed = static_cast<uint32_t>(seedIt->second.get_int64());
	}

	readBool("useSelection", spec.useSelection);
	readBool("useSidecar", spec.useSidecar);
	readInt("elevation", spec.elevation.maxLevels);

	json::mObject::const_iterator sourceIt = object.find("source");
	if (sourceIt != object.end() && sourceIt->second.type() == json::str_type) {
		const auto& source = sourceIt->second.get_str();
		if (source != "world" && source != "image" && source != "image_mask" && source != "prompt" && source != "text_prompt" && source != "prompt_with_image" && source != "combined") {
			error = "Unknown generation recipe source.";
			return false;
		}
		spec.source = SourceFromString(source);
	}

	json::mObject::const_iterator presetIt = object.find("preset");
	if (presetIt != object.end() && presetIt->second.type() == json::str_type) {
		spec.preset = PresetFromString(presetIt->second.get_str());
	}

	json::mObject::const_iterator regionIt = object.find("region");
	if (regionIt != object.end() && regionIt->second.type() == json::obj_type) {
		const json::mObject& region = regionIt->second.get_obj();

		auto regionInt = [&](const char* key, int& out) {
			const auto it = region.find(key);
			if (it == region.end()) {
				return;
			}
			if (it->second.type() != json::int_type || it->second.get_int64() < std::numeric_limits<int>::min() || it->second.get_int64() > std::numeric_limits<int>::max()) {
				validNumbers = false;
				return;
			}
			out = static_cast<int>(it->second.get_int64());
		};
		regionInt("width", spec.region.width);
		regionInt("height", spec.region.height);
		regionInt("z", spec.region.z);
		regionInt("originX", spec.region.originX);
		regionInt("originY", spec.region.originY);
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

	const auto versionIt = object.find("version");
	if (versionIt != object.end() && (versionIt->second.type() != json::int_type || versionIt->second.get_int64() < 1 || versionIt->second.get_int64() > 4)) {
		error = "Unsupported recipe version.";
		return false;
	}
	const auto worldIt = object.find("world");
	if (worldIt != object.end()) {
		if (worldIt->second.type() != json::obj_type) {
			error = "Recipe world must be an object.";
			return false;
		}
		const auto& world = worldIt->second.get_obj();
		auto integer = [&](const char* key, int& target) {
			const auto it = world.find(key);
			if (it == world.end()) {
				return;
			}
			if (it->second.type() != json::int_type || it->second.get_int64() < 0 || it->second.get_int64() > 65535) {
				validNumbers = false;
				return;
			}
			target = static_cast<int>(it->second.get_int64());
		};
		integer("seaLevel", spec.world.settings.seaLevel);
		integer("rivers", spec.world.settings.riverCount);
		integer("settlements", spec.world.settings.settlementCount);
		integer("buildings", spec.world.settings.buildingsPerSettlement);
		integer("buildingLevels", spec.world.settings.buildingLevels);
		integer("caveDepth", spec.world.settings.caveDepth);
		integer("caveFill", spec.world.settings.caveFill);
		integer("roadWidth", spec.world.settings.roadWidth);
		integer("connectionItemId", spec.world.connectionItemId);
		const auto shape = world.find("shape");
		if (shape != world.end()) {
			if (shape->second.type() != json::str_type || (shape->second.get_str() != "continent" && shape->second.get_str() != "archipelago")) {
				error = "World shape must be continent or archipelago.";
				return false;
			}
			spec.world.settings.shape = shape->second.get_str() == "archipelago" ? WorldGen::LandShape::Archipelago : WorldGen::LandShape::Continent;
		}
		const auto protect = world.find("protectExisting");
		if (protect != world.end()) {
			if (protect->second.type() != json::bool_type) {
				error = "World protectExisting must be a boolean.";
				return false;
			}
			spec.world.protectExisting = protect->second.get_bool();
		}
		const auto catalog = world.find("catalog");
		if (catalog != world.end()) {
			if (catalog->second.type() != json::str_type) {
				error = "World catalog must be a path string.";
				return false;
			}
			spec.world.catalogPath = wxstr(catalog->second.get_str());
		}
		const auto wall = world.find("wallBrush");
		if (wall != world.end()) {
			if (wall->second.type() != json::str_type) {
				error = "World wallBrush must be a string.";
				return false;
			}
			spec.world.wallBrushName = wxstr(wall->second.get_str());
		}
	}
	if (!validNumbers || spec.region.width <= 0 || spec.region.height <= 0 || spec.region.z < 0 || spec.region.z > MAP_MAX_LAYER || spec.region.originX < 0 || spec.region.originY < 0) {
		error = "Recipe contains invalid region or numeric settings.";
		return false;
	}
	if (spec.source == GenerationSource::WorldPlan) {
		auto settings = spec.world.settings;
		settings.width = spec.region.width;
		settings.height = spec.region.height;
		std::string problem;
		if (!WorldGen::ValidateSettings(settings, problem)) {
			error = wxstr(problem);
			return false;
		}
	}
	result = spec;

	return true;
}
