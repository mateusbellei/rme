#include "main.h"

#include "world_catalog.h"
#include "json.h"
#include "procedural_json.h"

#include <fstream>
#include <filesystem>

namespace WorldGen {
	bool LoadBlueprintCatalog(const std::string& filename, BlueprintCatalog& catalog, std::string& error) {
		error.clear();
		json::mValue root;
		if (!ProceduralJSON::ReadFile(filename, 2 * 1024 * 1024, root, error)) {
			return false;
		}
		if (root.type() != json::obj_type) {
			error = "Construction catalog JSON must be an object.";
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
			if (!integer("weight", blueprint.weight) || !integer("maxLevels", blueprint.maxLevels) || !integer("maxPerSettlement", blueprint.maxPerSettlement)) {
				error = "Invalid construction template weight or levels.";
				return false;
			}
			const auto role = entry.find("role");
			if (role != entry.end()) {
				if (role->second.type() != json::str_type || role->second.get_str().empty()) {
					error = "Template role must be a nonempty identifier.";
					return false;
				}
				blueprint.role = role->second.get_str();
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
			const auto materials = entry.find("materials");
			if (materials != entry.end()) {
				if (materials->second.type() != json::obj_type) {
					error = "Template materials must be an object with exact floor/wall brush names.";
					return false;
				}
				for (const auto& binding : { std::pair { "floor", &blueprint.floorBrush }, std::pair { "wall", &blueprint.wallBrush } }) {
					const auto value = materials->second.get_obj().find(binding.first);
					if (value == materials->second.get_obj().end()) {
						continue;
					}
					if (value->second.type() != json::str_type || value->second.get_str().empty()) {
						error = "Template material bindings must be nonempty strings.";
						return false;
					}
					*binding.second = value->second.get_str();
				}
			}
			const auto objects = entry.find("objects");
			if (objects != entry.end()) {
				if (objects->second.type() != json::array_type || objects->second.get_array().size() > 128) {
					error = "Template objects must be an array of at most 128 entries.";
					return false;
				}
				for (const auto& object : objects->second.get_array()) {
					if (object.type() != json::obj_type) {
						error = "Furniture object must be an object.";
						return false;
					}
					const auto& fields = object.get_obj();
					BlueprintProp prop;
					for (const auto& coordinate : { std::pair { "x", &prop.x }, std::pair { "y", &prop.y }, std::pair { "floor", &prop.floor } }) {
						const auto value = fields.find(coordinate.first);
						if (value == fields.end() && std::string(coordinate.first) == "floor") {
							continue;
						}
						if (value == fields.end() || value->second.type() != json::int_type || value->second.get_int64() < 0 || value->second.get_int64() > 31) {
							error = "Furniture requires bounded integer x/y/floor coordinates.";
							return false;
						}
						*coordinate.second = int(value->second.get_int64());
					}
					const auto brush = fields.find("brush"), brushes = fields.find("brushes");
					if ((brush == fields.end()) == (brushes == fields.end())) {
						error = "Furniture requires either brush or four orientation brushes.";
						return false;
					}
					if (brush != fields.end()) {
						if (brush->second.type() != json::str_type) {
							error = "Furniture brush must be a string.";
							return false;
						}
						prop.brushes.push_back(brush->second.get_str());
					} else {
						if (brushes->second.type() != json::array_type || brushes->second.get_array().size() != 4) {
							error = "Furniture brushes must contain four orientation names.";
							return false;
						}
						for (const auto& name : brushes->second.get_array()) {
							if (name.type() != json::str_type) {
								error = "Furniture orientation brush must be a string.";
								return false;
							}
							prop.brushes.push_back(name.get_str());
						}
					}
					const auto required = fields.find("required");
					if (required != fields.end()) {
						if (required->second.type() != json::bool_type) {
							error = "Furniture required must be a boolean.";
							return false;
						}
						prop.required = required->second.get_bool();
					}
					blueprint.objects.push_back(std::move(prop));
				}
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
