#include "main.h"

#include "world_catalog.h"
#include "json.h"

#include <fstream>
#include <filesystem>

namespace WorldGen {
	bool LoadBlueprintCatalog(const std::string& filename, BlueprintCatalog& catalog, std::string& error) {
		error.clear();
		std::ifstream input(std::filesystem::u8path(filename));
		if (input) {
			input.seekg(0, std::ios::end);
			if (input.tellg() > 2 * 1024 * 1024) {
				error = "Construction catalog exceeds the 2 MiB input limit.";
				return false;
			}
			input.seekg(0, std::ios::beg);
		}
		json::mValue root;
		if (!input || !json::read(input, root) || root.type() != json::obj_type) {
			error = "Cannot read construction catalog JSON: " + filename;
			return false;
		}
		const auto& object = root.get_obj();
		const auto version = object.find("version"), templates = object.find("templates");
		if (version == object.end() || version->second.type() != json::int_type || version->second.get_int64() != 1 || templates == object.end() || templates->second.type() != json::array_type || templates->second.get_array().size() > 64) {
			error = "Construction catalog requires version 1 and a templates array (maximum 64).";
			return false;
		}
		BlueprintCatalog parsed;
		for (const auto& value : templates->second.get_array()) {
			if (value.type() != json::obj_type) {
				error = "Every construction template must be an object.";
				return false;
			}
			const auto& entry = value.get_obj();
			Blueprint blueprint;
			const auto id = entry.find("id");
			if (id == entry.end() || id->second.type() != json::str_type) {
				error = "Construction template needs an id.";
				return false;
			}
			blueprint.id = id->second.get_str();
			auto integer = [&](const char* key, int& target) {const auto it=entry.find(key);if(it==entry.end()){return true;
}if(it->second.type()!=json::int_type||it->second.get_int64()<0||it->second.get_int64()>1000){return false;
}target=int(it->second.get_int64());return true; };
			if (!integer("weight", blueprint.weight) || !integer("maxLevels", blueprint.maxLevels)) {
				error = "Invalid construction template weight or levels.";
				return false;
			}
			const auto rotate = entry.find("rotate");
			if (rotate != entry.end()) {
				if (rotate->second.type() != json::bool_type) {
					error = "Template rotate must be a boolean.";
					return false;
				}
				blueprint.allowRotation = rotate->second.get_bool();
			}
			auto strings = [&](const char* key, std::vector<std::string>& target, size_t maxEntries) {const auto it=entry.find(key);if(it==entry.end()){return true;
}if(it->second.type()!=json::array_type||it->second.get_array().size()>maxEntries){return false;
}for(const auto& v:it->second.get_array()){if(v.type()!=json::str_type||v.get_str().size()>64){return false;
}target.push_back(v.get_str());}return true; };
			if (!strings("rows", blueprint.rows, 32) || !strings("upperRows", blueprint.upperRows, 32) || !strings("biomes", blueprint.biomes, 5)) {
				error = "Template rows, upperRows and biomes must be bounded string arrays.";
				return false;
			}
			parsed.push_back(std::move(blueprint));
		}
		if (!ValidateCatalog(parsed, error)) {
			return false;
		}
		catalog = std::move(parsed);
		return true;
	}
}
