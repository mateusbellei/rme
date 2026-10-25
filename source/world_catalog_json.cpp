#include "main.h"

#include "world_catalog.h"
#include "json.h"
#include "procedural_json.h"

#include <fstream>
#include <filesystem>

namespace WorldGen {
	bool ReadPrefabFile(const std::string& filename, std::string& bytes, uint64_t& fingerprint, std::string& error) {
		error.clear();
		if (filename.empty() || filename.size() > 32768 || filename.find('\0') != std::string::npos) {
			error = "Invalid OTBM prefab filename."; return false;
		}
		std::ifstream input(std::filesystem::u8path(filename), std::ios::binary | std::ios::ate);
		const auto length = input ? input.tellg() : std::streampos(-1);
		if (length < std::streamoff(24) || length > std::streamoff(MaximumPrefabBytes)) {
			error = "OTBM prefabs must be readable files of 24 bytes to 2 MiB: " + filename; return false;
		}
		std::string read(size_t(length), '\0'); input.seekg(0);
		if (!input.read(read.data(), std::streamsize(read.size())) || (read.compare(0, 4, "OTBM") != 0 && read.compare(0, 4, std::string(4, '\0')) != 0)) {
			error = "Invalid OTBM prefab signature: " + filename; return false;
		}
		uint64_t hash = 1469598103934665603ull;
		for (unsigned char value : read) { hash ^= value; hash *= 1099511628211ull; }
		fingerprint = hash; bytes = std::move(read); return true;
	}

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
			if (!integer("weight", blueprint.weight) || !integer("maxLevels", blueprint.maxLevels) || !integer("maxPerSettlement", blueprint.maxPerSettlement) || !integer("minPerSettlement", blueprint.minPerSettlement) || !integer("minimumPriority", blueprint.minimumPriority)) {
				error = "Invalid construction template weight, levels or per-settlement limits.";
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
			const auto upperFloors = entry.find("upperFloors");
			if (upperFloors != entry.end()) {
				if (upperFloors->second.type() != json::array_type || upperFloors->second.get_array().empty() || upperFloors->second.get_array().size() > 3) {
					error = "upperFloors must be an array of 1..3 bounded row arrays.";
					return false;
				}
				for (const auto& floor : upperFloors->second.get_array()) {
					if (floor.type() != json::array_type || floor.get_array().size() < 5 || floor.get_array().size() > 32) {
						error = "Each upperFloors entry must contain 5..32 rows.";
						return false;
					}
					std::vector<std::string> rows;
					for (const auto& row : floor.get_array()) {
						if (row.type() != json::str_type || row.get_str().size() > 32) {
							error = "Upper floor rows must be strings of at most 32 cells.";
							return false;
						}
						rows.push_back(row.get_str());
					}
					blueprint.upperFloors.push_back(std::move(rows));
				}
			}
			const auto materials = entry.find("materials");
			auto readMaterial = [&](const json::mValue& value, std::string& ground, std::string& wall) {
				if (value.type() != json::obj_type) {
					error = "Template materials must be objects with exact floor/wall brush names.";
					return false;
				}
				for (const auto& binding : { std::pair { "floor", &ground }, std::pair { "wall", &wall } }) {
					const auto field = value.get_obj().find(binding.first);
					if (field == value.get_obj().end()) continue;
					if (field->second.type() != json::str_type || field->second.get_str().empty()) {
						error = "Template material bindings must be nonempty strings.";
						return false;
					}
					*binding.second = field->second.get_str();
				}
				return true;
			};
			if (materials != entry.end()) {
				if (!readMaterial(materials->second, blueprint.floorBrush, blueprint.wallBrush)) {
					return false;
				}
			}
			const auto floorMaterials = entry.find("floorMaterials");
			if (floorMaterials != entry.end()) {
				if (floorMaterials->second.type() != json::array_type || floorMaterials->second.get_array().empty() || floorMaterials->second.get_array().size() > 4) {
					error = "floorMaterials must be an array of 1..4 material objects, starting at the base floor.";
					return false;
				}
				for (const auto& value : floorMaterials->second.get_array()) {
					BlueprintMaterial material;
					if (!readMaterial(value, material.floorBrush, material.wallBrush)) return false;
					blueprint.floorMaterials.push_back(std::move(material));
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
			const auto prefab = entry.find("prefab");
			if (prefab != entry.end()) {
				if (prefab->second.type() != json::obj_type) { error = "prefab must be an object with file and origin."; return false; }
				const auto& binding = prefab->second.get_obj();
				const auto file = binding.find("file"), origin = binding.find("origin");
				if (file == binding.end() || file->second.type() != json::str_type || file->second.get_str().empty() || file->second.get_str().size() > 32768 || file->second.get_str().find('\0') != std::string::npos || origin == binding.end() || origin->second.type() != json::array_type || origin->second.get_array().size() != 3) { error = "prefab requires a nonempty file and integer origin [x,y,z]."; return false; }
				int* coordinates[] = {&blueprint.prefabX, &blueprint.prefabY, &blueprint.prefabZ};
				for (int i = 0; i < 3; ++i) {
					const auto& value = origin->second.get_array()[i];
					if (value.type() != json::int_type || value.get_int64() < 0 || value.get_int64() > (i == 2 ? 15 : 65535)) { error = "prefab origin is outside map coordinates."; return false; }
					*coordinates[i] = int(value.get_int64());
				}
				try {
					auto path = std::filesystem::u8path(file->second.get_str());
					if (path.is_relative()) path = std::filesystem::u8path(filename).parent_path() / path;
					auto utf8 = std::filesystem::absolute(path).lexically_normal().u8string();
					blueprint.prefabPath.assign(utf8.begin(), utf8.end());
				} catch (const std::filesystem::filesystem_error&) { error = "Cannot resolve the OTBM prefab path."; return false; }
				std::string bytes;
				if (!ReadPrefabFile(blueprint.prefabPath, bytes, blueprint.prefabFingerprint, error)) return false;
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
