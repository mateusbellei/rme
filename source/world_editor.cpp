#include "main.h"

#include "world_editor.h"
#include "procedural_json.h"
#include "procedural_common.h"
#include "procedural_legend.h"
#include "editor.h"
#include "action.h"
#include "brush.h"
#include "ground_brush.h"
#include "wall_brush.h"
#include "items.h"
#include "complexitem.h"
#include "mt_rand.h"
#include "gui.h"
#include "world_decoration.h"
#include "world_inputs.h"
#include "world_prefab.h"
#include "doodad_brush.h"

#include <array>
#include <memory>
#include <set>
#include <filesystem>

namespace WorldEditor {
	namespace {
		constexpr size_t kTerrainCount = static_cast<size_t>(WorldGen::Terrain::Void);

		struct Material {
			GroundBrush* brush = nullptr;
			int tileId = 0;
		};

		struct BuildingMaterial {
			Material floor;
			WallBrush* wall = nullptr;
		};
		using BuildingMaterials = std::array<BuildingMaterial, 5>;

		bool Update(const WorldGen::ProgressCallback& progress, const char* stage, int percent, wxString& error) {
			if (progress && !progress(stage, percent)) {
				error = "World generation cancelled.";
				return false;
			}
			return true;
		}

		uint32_t CellSeed(uint32_t seed, int x, int y, int z, uint32_t stage) {
			uint32_t value = seed ^ uint32_t(x) * 0x9e3779b9u ^ uint32_t(y) * 0x85ebca6bu ^ uint32_t(z) * 0xc2b2ae35u ^ stage;
			value ^= value >> 16;
			value *= 0x7feb352du;
			value ^= value >> 15;
			value *= 0x846ca68bu;
			return value ^ (value >> 16);
		}

		Position PositionFor(const GenerationSpec& spec, WorldGen::Point p, int floor) {
			return { spec.region.originX + p.x, spec.region.originY + p.y, spec.region.z + floor };
		}

		uint8_t Habitat(WorldGen::Terrain terrain) {
			switch (terrain) {
				case WorldGen::Terrain::Grass:
				case WorldGen::Terrain::Forest:
					return 1;
				case WorldGen::Terrain::Desert:
					return 2;
				case WorldGen::Terrain::Snow:
					return 3;
				case WorldGen::Terrain::CaveFloor:
					return 4;
				case WorldGen::Terrain::Beach:
					return 5;
				default:
					return 0;
			}
		}

		bool TileWalkable(const Tile* tile) {
			if (!tile || !tile->ground || g_items[tile->ground->getID()].unpassable) {
				return false;
			}
			for (const Item* item : tile->items) {
				if (g_items[item->getID()].unpassable) {
					return false;
				}
			}
			return true;
		}

		bool DecorationBrushes(const GenerationSpec& spec, std::array<std::vector<DoodadBrush*>, 7>& brushes, std::array<int, 7>& density, ApplicationStats& stats, wxString& error) {
			density.fill(100);
			if (!spec.world.objectProfilePath.empty()) {
				json::mValue root; std::string problem;
				if (!ProceduralJSON::ReadFile(nstr(spec.world.objectProfilePath), 1024 * 1024, root, problem) || root.type() != json::obj_type) { error = "Invalid biome object profile: " + wxString::FromUTF8(problem); return false; }
				const auto& object = root.get_obj();
				const auto version = object.find("version"), biomes = object.find("biomes");
				if (version == object.end() || version->second.type() != json::int_type || version->second.get_int64() != 1 || biomes == object.end() || biomes->second.type() != json::obj_type || biomes->second.get_obj().size() > 6) { error = "Object profiles require version 1 and a biomes object."; return false; }
				const std::array<std::string, 7> names { "", "forest", "desert", "ice", "cave", "coast", "highland" };
				for (const auto& entry : biomes->second.get_obj()) {
					const auto found = std::find(names.begin() + 1, names.end(), entry.first);
					if (found == names.end() || entry.second.type() != json::obj_type) { error = "Unknown biome in object profile."; return false; }
					const size_t index = size_t(found - names.begin()); const auto& biome = entry.second.get_obj();
					const auto scale = biome.find("density"), entries = biome.find("brushes");
					if (scale != biome.end()) {
						if (scale->second.type() != json::int_type || scale->second.get_int64() < 0 || scale->second.get_int64() > 200) { error = "Biome density must be 0..200 percent."; return false; }
						density[index] = int(scale->second.get_int64());
					}
					if (entries == biome.end() || entries->second.type() != json::array_type || entries->second.get_array().size() > 64) { error = "Biome brushes must be an array of at most 64 weighted names."; return false; }
					for (const auto& value : entries->second.get_array()) {
						if (value.type() != json::obj_type) { error = "Invalid weighted object brush."; return false; }
						const auto& item = value.get_obj(); const auto name = item.find("name"), weight = item.find("weight");
						if (name == item.end() || name->second.type() != json::str_type || name->second.get_str().empty() || name->second.get_str().size() > 128 || name->second.get_str().find('\0') != std::string::npos || (weight != item.end() && (weight->second.type() != json::int_type || weight->second.get_int64() < 1 || weight->second.get_int64() > 100))) { error = "Object brushes need a bounded exact name and weight 1..100."; return false; }
						Brush* brush = g_brushes.getBrush(name->second.get_str());
						if (!brush || !brush->isDoodad()) { error = "Missing exact doodad brush: " + wxString::FromUTF8(name->second.get_str()); return false; }
						brushes[index].insert(brushes[index].end(), weight == item.end() ? 1 : size_t(weight->second.get_int64()), brush->asDoodad());
					}
				}
				return true;
			}
			std::array<std::vector<std::string>, 7> keywords;
			keywords[1] = { "tree", "bush", "fern", "mushroom", "log" };
			keywords[2] = { "cactus", "desert", "dead", "palm", "rock" };
			keywords[3] = { "snow", "ice", "frost", "pine", "fir" };
			keywords[4] = { "stalagmite", "crystal", "rock" };
			keywords[5] = { "palm", "shell", "coral", "beach" };
			keywords[6] = { "rock", "rubble", "grass" };
			const char* names[] = { "", "forest", "desert", "ice", "cave", "coast" };
			const wxString path = g_gui.GetProceduralDataDirectory() + "biome_doodads.json";
			json::mValue root;
			std::string readError;
			if (ProceduralJSON::ReadFile(nstr(path), 1024 * 1024, root, readError) && root.type() == json::obj_type) {
				const auto presets = root.get_obj().find("presets");
				if (presets != root.get_obj().end() && presets->second.type() == json::obj_type) {
					for (int habitat = 1; habitat <= 5; ++habitat) {
						const auto preset = presets->second.get_obj().find(names[habitat]);
						if (preset == presets->second.get_obj().end() || preset->second.type() != json::obj_type) {
							continue;
						}
						const auto list = preset->second.get_obj().find("keywords");
						if (list == preset->second.get_obj().end() || list->second.type() != json::array_type || list->second.get_array().size() > 64) {
							continue;
						}
						std::vector<std::string> parsed;
						for (const auto& value : list->second.get_array()) {
							if (value.type() == json::str_type && !value.get_str().empty() && value.get_str().size() <= 128) {
								parsed.push_back(as_lower_str(value.get_str()));
							}
						}
						keywords[habitat] = std::move(parsed);
					}
				}
			} else {
				stats.warnings.push_back("Biome decoration settings could not be read; built-in keyword groups were used.");
			}

			for (int i = 1; i <= 6; ++i) {
				if (!keywords[i].empty()) {
					brushes[i] = ProceduralCommon::FindDoodadBrushes(keywords[i]);
				}
			}
			return true;
		}

		struct DoodadSample {
			struct Object {
				Position offset;
				const Item* prototype;
			};
			std::vector<Object> objects;
			std::vector<WorldGen::DecorationCell> footprint;
			std::unique_ptr<Tile> tile;
		};

		bool SampleDoodad(DoodadBrush& brush, BaseMap& scratch, Position anchor, uint32_t variationSeed, uint32_t drawSeed, uint32_t selectionSeed, DoodadSample& sample, wxString& reason) {
			auto fail = [&](const char* why) {reason=wxString::FromUTF8(why);return false; };
			const int variants = brush.getMaxVariation();
			if (variants <= 0) {
				return fail("brush has no variations");
			}
			int variation = int(variationSeed % uint32_t(variants));
			const int total = brush.getTotalChance(variation), composite = brush.getCompositeChance(variation);
			if (total <= 0) {
				return fail("brush has no usable items");
			}
			{
				ScopedMTRandom random(drawSeed);
				if (composite > 0 && int(selectionSeed % uint32_t(total)) < composite) {
					for (const auto& entry : brush.getComposite(variation)) {
						for (const Item* item : entry.second) {
							sample.objects.push_back({ entry.first, item });
							if (sample.objects.size() > 256) {
								return fail("composite exceeds 256 objects");
							}
						}
					}
				} else {
					sample.tile.reset(scratch.allocator(scratch.getTile(anchor)->getLocation()));
					brush.draw(&scratch, sample.tile.get(), &variation);
					if (sample.tile->ground) {
						return fail("doodad replaces ground");
					}
					for (const Item* item : sample.tile->items) {
						sample.objects.push_back({ Position(0, 0, 0), item });
					}
				}
			}
			if (sample.objects.empty() || sample.objects.size() > 256) {
				return fail("brush produces no usable bounded footprint");
			}
			std::map<std::pair<int, int>, bool> points;
			for (const auto& object : sample.objects) {
				const int id = object.prototype->getID();
				if (object.offset.z != 0 || object.offset.x < -32 || object.offset.x > 32 || object.offset.y < -32 || object.offset.y > 32 || !g_items.typeExists(id)) {
					return fail("invalid item or composite offset");
				}
				const auto& type = g_items[id];
				if (type.isMetaItem() || type.isGroundTile() || type.ground_equivalent || type.isTeleport() || type.isDoor() || type.isDepot() || type.isContainer() || type.isFloorChange() || type.isWall || object.prototype->getUniqueID() || object.prototype->getActionID()) {
					return fail("object contains ground, travel, container, wall or quest attributes");
				}
				auto& blocked = points[{ object.offset.x, object.offset.y }];
				blocked = blocked || type.unpassable;
				if (type.sprite) {
					if (type.sprite->width > 32 || type.sprite->height > 32) {
						return fail("sprite exceeds supported footprint bounds");
					}
					for (int dy = 0; dy < type.sprite->height; ++dy) {
						for (int dx = 0; dx < type.sprite->width; ++dx) {
							points.try_emplace({ object.offset.x - dx, object.offset.y - dy }, false);
						}
					}
				}
				if (points.size() > 256) {
					return fail("visual footprint exceeds 256 cells");
				}
			}
			for (const auto& [point, blocked] : points) {
				sample.footprint.push_back({ { point.first, point.second }, blocked });
			}
			return true;
		}

		void PlaceDoodad(const DoodadSample& sample, BaseMap& scratch, Position anchor, std::set<Position>& blocking) {
			for (const auto& object : sample.objects) {
				const Position pos(anchor.x + object.offset.x, anchor.y + object.offset.y, anchor.z);
				scratch.getTile(pos)->addItem(object.prototype->deepCopy());
				if (g_items[object.prototype->getID()].unpassable) {
					blocking.insert(pos);
				}
			}
		}

		bool Furnish(const GenerationSpec& spec, const WorldGen::Plan& plan, BaseMap& scratch, std::set<Position>& blocking, ApplicationStats& stats, const WorldGen::ProgressCallback& progress, wxString& error) {
			const uint32_t objectSeed = spec.world.objectSeed.value_or(spec.seed);
			std::set<wxString> warnings;
			for (const auto& building : plan.buildings) {
				if (!Update(progress, "Furnishing construction templates", 75, error)) {
					return false;
				}
				const auto blueprint = WorldGen::RotateBlueprint(plan.catalog[building.blueprint], building.rotation);
				if (blueprint.objects.empty()) {
					continue;
				}
				for (int floor = 0; floor < building.levels; ++floor) {
					const int offsetZ = -floor;
					const auto* layer = plan.FindLayer(offsetZ);
					WorldGen::DecorationGrid grid;
					grid.width = building.width;
					grid.height = building.height;
					const size_t count = size_t(grid.width) * grid.height;
					grid.walkable.resize(count);
					grid.reserved.resize(count);
					grid.occupied.assign(count, 0);
					grid.habitat.assign(count, 1);
					for (int y = 0; y < grid.height; ++y) {
						for (int x = 0; x < grid.width; ++x) {
							const WorldGen::Point point { building.origin.x + x, building.origin.y + y };
							const size_t i = size_t(y) * grid.width + x;
							const auto& cell = layer->cells[plan.Index(point)];
							grid.walkable[i] = WorldGen::IsWalkable(cell) && TileWalkable(scratch.getTile(PositionFor(spec, point, offsetZ)));
							grid.reserved[i] = (cell.flags & (WorldGen::Entrance | WorldGen::Connection)) != 0;
						}
					}
					for (const auto& link : plan.links) {
						for (const auto& arrival : { std::pair { link.upperOffsetZ, link.upperArrival }, std::pair { link.lowerOffsetZ, link.lowerArrival } }) {
							const int x = arrival.second.x - building.origin.x, y = arrival.second.y - building.origin.y;
							if (arrival.first == offsetZ && x >= 0 && y >= 0 && x < grid.width && y < grid.height) {
								grid.reserved[size_t(y) * grid.width + x] = 1;
							}
						}
					}
					for (const auto& object : blueprint.objects) {
						if (object.floor != floor) {
							continue;
						}
						const auto& name = object.brushes.front();
						Brush* found = g_brushes.getBrush(name);
						const WorldGen::Point authored { building.origin.x + object.x, building.origin.y + object.y };
						WorldGen::Point point=authored, anchor{object.x,object.y};
						DoodadSample sample;
						wxString reason;
						std::string clearance;
						bool sampled=false, fitted=false;
						for (int radius=0; radius<=object.placementRadius && !fitted; ++radius) for (int dy=-radius;dy<=radius && !fitted;++dy) for (int dx=-radius;dx<=radius && !fitted;++dx) {
							if (std::abs(dx)+std::abs(dy)!=radius) continue;
							const WorldGen::Point candidate{object.x+dx,object.y+dy};
							if(candidate.x<0 || candidate.y<0 || candidate.x>=grid.width || candidate.y>=grid.height || !grid.walkable[size_t(candidate.y)*grid.width+candidate.x]) continue;
							DoodadSample trial;
							const WorldGen::Point target{building.origin.x+candidate.x,building.origin.y+candidate.y};
							sampled=found && found->isDoodad() && SampleDoodad(*found->asDoodad(),scratch,PositionFor(spec,target,offsetZ),CellSeed(objectSeed,authored.x,authored.y,offsetZ,21),CellSeed(objectSeed,authored.x,authored.y,offsetZ,22),CellSeed(objectSeed,authored.x,authored.y,offsetZ,23),trial,reason);
							if(sampled && WorldGen::CanPlaceDecoration(grid,candidate,trial.footprint,&clearance)) { sample=std::move(trial); anchor=candidate; point=target; fitted=true; }
						}
						if (!fitted) {
							if (!found || !found->isDoodad()) {
								reason = "missing doodad brush";
							} else if (sampled) {
								reason = wxString::FromUTF8(clearance);
							}
							if (reason.empty()) reason="no safe furniture footprint within the authored placement radius";
							const wxString message = "Furniture '" + wxString::FromUTF8(name) + "' in template '" + wxString::FromUTF8(blueprint.id) + wxString::Format("' (floor %d, local %d,%d, rotation %d): ",floor,object.x,object.y,building.rotation) + reason;
							if (object.required) {
								error = message;
								return false;
							}
							++stats.skippedFurniture;
							warnings.insert(message);
							continue;
						}
						PlaceDoodad(sample, scratch, PositionFor(spec, point, offsetZ), blocking);
						WorldGen::MarkDecoration(grid, anchor, sample.footprint);
						if (!(point==authored)) ++stats.relocatedFurniture;
						++stats.furniture;
					}
				}
			}
			stats.warnings.insert(stats.warnings.end(), warnings.begin(), warnings.end());
			return true;
		}

		bool Decorate(const GenerationSpec& spec, const WorldGen::Plan& plan, BaseMap& scratch, std::set<Position>& blocking, ApplicationStats& stats, const WorldGen::ProgressCallback& progress, wxString& error) {
			if (!spec.doodads.enabled || spec.doodads.density <= 0) {
				return true;
			}
			std::array<std::vector<DoodadBrush*>, 7> brushes;
			std::array<int, 7> densityScale;
			if (!DecorationBrushes(spec, brushes, densityScale, stats, error)) return false;
			const uint32_t objectSeed = spec.world.objectSeed.value_or(spec.seed);
			std::array<bool, 7> used {};
			for (const auto& layer : plan.layers) {
				WorldGen::DecorationGrid grid;
				grid.width = plan.settings.width;
				grid.height = plan.settings.height;
				const size_t count = layer.cells.size();
				grid.walkable.resize(count);
				grid.reserved.resize(count);
				grid.occupied.assign(count, 0);
				grid.habitat.resize(count);
				for (int y = 0; y < grid.height; ++y) {
					for (int x = 0; x < grid.width; ++x) {
						const size_t index = size_t(y) * grid.width + x;
						const auto& cell = layer.cells[index];
						const uint8_t habitat = (cell.flags & WorldGen::Highland) && WorldGen::IsWalkable(cell) && cell.terrain != WorldGen::Terrain::Snow ? 6 : Habitat(cell.terrain);
						grid.habitat[index] = habitat;
						grid.reserved[index] = !habitat || (cell.flags & WorldGen::Reserved);
						grid.walkable[index] = WorldGen::IsWalkable(cell) && TileWalkable(scratch.getTile(PositionFor(spec, { x, y }, layer.offsetZ)));
						if (habitat && !grid.reserved[index]) {
							used[habitat] = true;
						}
					}
				}
				for (int y = 1; y < grid.height - 1; ++y) {
					if ((y & 15) == 0 && !Update(progress, "Decorating biomes", 65, error)) {
						return false;
					}
					for (int x = 1; x < grid.width - 1; ++x) {
						const size_t index = size_t(y) * grid.width + x;
						const uint8_t habitat = grid.habitat[index];
						if (!habitat || grid.reserved[index] || !grid.walkable[index] || grid.occupied[index] || brushes[habitat].empty()) {
							continue;
						}
						const uint32_t choice = CellSeed(objectSeed, x, y, layer.offsetZ, 11);
						const auto terrain = layer.cells[index].terrain;
						const int factor = terrain == WorldGen::Terrain::Forest ? 180 : (terrain == WorldGen::Terrain::Grass ? 40 : (habitat == 4 ? 60 : 80));
						const int patch = 60 + int(CellSeed(objectSeed, x / 12, y / 12, layer.offsetZ, 12) % 81);
						const int density = std::min(80, std::clamp(spec.doodads.density, 0, 100) * factor * patch * densityScale[habitat] / 1000000);
						if (choice % 100 >= uint32_t(density)) {
							continue;
						}
						DoodadBrush* brush = brushes[habitat][CellSeed(objectSeed, x, y, layer.offsetZ, 13) % brushes[habitat].size()];
						DoodadSample sample;
						wxString reason;
						const auto anchor = PositionFor(spec, { x, y }, layer.offsetZ);
						if (!SampleDoodad(*brush, scratch, anchor, CellSeed(objectSeed, x, y, layer.offsetZ, 14), CellSeed(objectSeed, x, y, layer.offsetZ, 15), CellSeed(objectSeed, x, y, layer.offsetZ, 16), sample, reason) || !WorldGen::CanPlaceDecoration(grid, { x, y }, sample.footprint)) {
							++stats.skippedDoodads;
							continue;
						}
						PlaceDoodad(sample, scratch, anchor, blocking);
						WorldGen::MarkDecoration(grid, { x, y }, sample.footprint);
						++stats.doodads;
					}
				}
			}
			const char* names[] = { "", "forest/grass", "desert", "snow", "cave", "beach", "highland" };
			for (int i = 1; i <= 6; ++i) {
				if (used[i] && brushes[i].empty()) {
					stats.warnings.push_back("No matching doodad brushes for " + wxString::FromUTF8(names[i]) + " in the active client.");
				}
			}
			return true;
		}

		bool CheckRegion(Editor& editor, const GenerationSpec& spec, const WorldGen::Plan* plan, wxString& error) {
			const int64_t right = int64_t(spec.region.originX) + spec.region.width;
			const int64_t bottom = int64_t(spec.region.originY) + spec.region.height;
			if (spec.region.originX < 0 || spec.region.originY < 0 || spec.region.width <= 0 || spec.region.height <= 0 || right > editor.map.getWidth() || bottom > editor.map.getHeight()) {
				error = "World target extends beyond the map dimensions. Resize the map or choose a smaller region first.";
				return false;
			}
			if (plan) {
				for (const auto& layer : plan->layers) {
					const int64_t floor = int64_t(spec.region.z) + layer.offsetZ;
					if (floor < 0 || floor > MAP_MAX_LAYER) {
						error = "World floor is outside the map z range.";
						return false;
					}
				}
			}
			return true;
		}

		bool CheckTarget(Editor& editor, const GenerationSpec& spec, const WorldGen::Plan& plan, size_t tileBudget, wxString& error) {
			if (editor.IsLiveClient()) {
				error = "World generation is unavailable in a live client: visibility clipping can create partial worlds. Generate locally and import the result.";
				return false;
			}
			size_t changes = 0;
			for (const auto& layer : plan.layers) {
				const int z = spec.region.z + layer.offsetZ;
				for (int y = 0; y < spec.region.height; ++y) {
					for (int x = 0; x < spec.region.width; ++x) {
						if (layer.cells[size_t(y) * spec.region.width + x].terrain == WorldGen::Terrain::Void) {
							continue;
						}
						if (++changes > tileBudget) {
							error = wxString::Format("World application exceeds its budget of %llu tiles. Reduce the region/floors or increase the explicit batch tile budget.", static_cast<unsigned long long>(tileBudget));
							return false;
						}
						Position pos(spec.region.originX + x, spec.region.originY + y, z);
						const Tile* tile = editor.map.getTile(pos);
						const TileLocation* location = editor.map.getTileL(pos);
						auto scripted = [](const Item* item) { return item && (item->getUniqueID() || item->getActionID()); };
						const bool scriptAttributes = tile && (scripted(tile->ground) || std::any_of(tile->items.begin(), tile->items.end(), scripted));
						const bool metadata = scriptAttributes || (tile && (tile->house_id || tile->spawn || tile->creature || tile->hasUniqueItem() || tile->getMapFlags() || !tile->getZoneIds().empty() || tile->isHouseExit())) || (location && (location->getWaypointCount() || location->getTownCount() || location->getSpawnCount()));
						if (metadata || (spec.world.protectExisting && tile && !tile->empty())) {
							error = wxString::Format("World generation overlaps a protected tile at %d,%d,%d. Choose an empty area%s.", pos.x, pos.y, pos.z, metadata ? " (house, spawn, creature, zone, map flags, unique/action ID or waypoint)" : " or disable 'Protect existing tiles' to replace ordinary tiles");
							return false;
						}
					}
				}
			}
			return true;
		}

		bool ResolveMaterials(const GenerationSpec& spec, const WorldGen::Plan& plan, const WorldPrefab::Library& prefabs, std::array<Material, kTerrainCount>& materials, WallBrush*& wall, int& teleportId, wxString& error) {
			const wxString path = spec.imageMask.legendPath.empty() ? g_gui.GetProceduralDataDirectory() + "world_legend.json" : spec.imageMask.legendPath;
			LegendMapping legend;
			if (!ProceduralCommon::LoadLegend(path, legend, error)) {
				return false;
			}
			ProceduralCommon::ResolveLegendBrushes(legend);
			std::array<bool, kTerrainCount> used {};
			bool needsWalls = false;
			for (const auto& layer : plan.layers) {
				for (size_t i = 0; i < layer.cells.size(); ++i) {
					const auto& cell = layer.cells[i];
					if (cell.terrain != WorldGen::Terrain::Void && !prefabs.Covers(plan, int(i % plan.settings.width), int(i / plan.settings.width), layer.offsetZ)) used[size_t(cell.terrain)] = true;
				}
			}
			for (const auto& building : plan.buildings) {
				if (!plan.catalog[building.blueprint].prefabPath.empty()) continue;
				for (int level = 0; level < building.levels; ++level) {
					needsWalls = needsWalls || WorldGen::BlueprintMaterialForFloor(plan.catalog[building.blueprint], level).wallBrush.empty();
				}
			}
			for (size_t i = 0; i < used.size(); ++i) {
				if (!used[i]) {
					continue;
				}
				const uint32_t rgb = WorldGen::TerrainColor(static_cast<WorldGen::Terrain>(i));
				const LegendEntry* entry = ProceduralLegend::Find(&legend, uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb));
				if (!entry || (entry->tileId <= 0 && !entry->brush)) {
					error = "World legend is missing a usable mapping for terrain '" + wxString::FromUTF8(WorldGen::TerrainName(static_cast<WorldGen::Terrain>(i))) + "'. Use world_legend.json or provide every used category explicitly.";
					return false;
				}
				if (entry->tileId > 0 && (!g_items.typeExists(entry->tileId) || !g_items[entry->tileId].isGroundTile())) {
					error = wxString::Format("World ground ID %d does not exist or is not a ground in this client.", entry->tileId);
					return false;
				}
				materials[i] = { entry->brush, std::max(0, entry->tileId) };
			}
			wall = nullptr;
			if (needsWalls) {
				wall = ProceduralCommon::FindWallBrush(as_lower_str(nstr(spec.world.wallBrushName)));
				if (spec.world.wallBrushName.empty() || !wall) {
					error = "No matching wall brush for world buildings. Set the wall brush in World settings, or set buildings per settlement to zero for terrain-only output.";
					return false;
				}
			}
			teleportId = spec.world.connectionItemId;
			if (!plan.links.empty() && !plan.settings.physicalStairs) {
				if (!teleportId) {
					for (int i = 1; i <= g_items.getMaxID(); ++i) {
						if (g_items.typeExists(i) && g_items[i].isTeleport() && !g_items[i].isGroundTile() && !g_items[i].unpassable) {
							teleportId = i;
							break;
						}
					}
				}
				if (teleportId <= 0 || teleportId > 65535 || !g_items.typeExists(teleportId) || !g_items[teleportId].isTeleport() || g_items[teleportId].isGroundTile() || g_items[teleportId].unpassable) {
					error = "Multi-floor worlds require a non-blocking teleport item in the loaded client. Set its server ID in World settings, or use one building floor and no caves.";
					return false;
				}
			}
			return true;
		}

		bool ResolveBuildingMaterials(const WorldGen::Plan& plan, std::vector<BuildingMaterials>& materials, wxString& error) {
			materials.resize(plan.catalog.size());
			std::vector<int> usedLevels(plan.catalog.size(), 0);
			for (const auto& building : plan.buildings) {
				usedLevels[building.blueprint] = std::max(usedLevels[building.blueprint], building.levels);
			}
			for (size_t i = 0; i < plan.catalog.size(); ++i) {
				if (!usedLevels[i]) {
					continue;
				}
				const auto& blueprint = plan.catalog[i];
				if (!blueprint.prefabPath.empty()) continue;
				for (int level = 0; level < usedLevels[i] + int(!blueprint.roofBrush.empty()); ++level) {
					const auto binding = level == usedLevels[i] ? WorldGen::BlueprintMaterial {blueprint.roofBrush, ""} : WorldGen::BlueprintMaterialForFloor(blueprint, level);
					const size_t slot = level == usedLevels[i] ? 4 : size_t(level);
					for (int type = 0; type < 2; ++type) {
						const auto& name = type == 0 ? binding.floorBrush : binding.wallBrush;
						if (name.empty()) {
							continue;
						}
						Brush* brush = g_brushes.getBrush(name);
						if (!brush || (type == 0 ? !brush->isGround() : !brush->isWall())) {
							error = "Template '" + wxString::FromUTF8(blueprint.id) + "' floor " + std::to_string(level) + " requires an exact " + (type == 0 ? "ground" : "wall") + " brush: " + wxString::FromUTF8(name);
							return false;
						}
						if (type == 0) {
							materials[i][slot].floor.brush = brush->asGround();
						} else {
							materials[i][slot].wall = brush->asWall();
						}
					}
				}
			}
			return true;
		}

		bool ResolveStairs(const GenerationSpec& spec, const WorldGen::Plan& plan, std::array<int,5>& ids, wxString& error) {
			ids = spec.world.stairItems;
			std::array<bool,5> needed{};
			for (const auto& link : plan.links) { needed[0] = true; needed[size_t(link.stairDirection + 1)] = true; }
			for (int slot = 0; slot < 5; ++slot) {
				if (!needed[slot]) continue;
				auto compatible = [&](int id) {
					if (id < 1 || id > 65535 || !g_items.typeExists(id)) return false;
					const auto& t=g_items[id];
					const bool directions[]={t.floorChangeDown,t.floorChangeNorth,t.floorChangeEast,t.floorChangeSouth,t.floorChangeWest};
					return t.isFloorChange() && directions[slot] && std::count(std::begin(directions),std::end(directions),true)==1 && !t.unpassable && !t.isTeleport() && !t.isDoor() && !t.isContainer() && !t.isMetaItem() && !t.ground_equivalent && !t.moveable;
				};
				if (!ids[slot]) {
					int best=-1;
					for (int id=1; id<=g_items.getMaxID(); ++id) if (compatible(id)) {
						const auto& name=g_items[id].name;
						const int score=name.find("stairs")!=std::string::npos ? 100 : (name.find("ramp")!=std::string::npos ? 80 : 0);
						if (score>best) { best=score; ids[slot]=id; }
					}
				}
				if (!compatible(ids[slot])) { error="Physical stairs require matching nonblocking directional floor-change items; check world.stairItems [down,north,east,south,west]."; return false; }
			}
			return true;
		}

		bool BuildScratch(Editor& editor, const GenerationSpec& spec, const WorldGen::Plan& plan, const std::array<Material, kTerrainCount>& materials, const std::vector<BuildingMaterials>& buildingMaterials, const WorldPrefab::Library& prefabs, WallBrush* wall, int teleportId, BaseMap& scratch, wxString& error, ApplicationStats& stats, const WorldGen::ProgressCallback& progress) {
			std::set<Position> decorativeBlocking;
			std::array<int,5> stairIds{};
			std::map<Position,int> physicalTravel;
			if (plan.settings.physicalStairs && !ResolveStairs(spec, plan, stairIds, error)) return false;
			Material highland;
			if (plan.settings.mountainLevels && !spec.world.highlandBrushName.empty()) {
				Brush* brush = g_brushes.getBrush(nstr(spec.world.highlandBrushName));
				if (!brush || !brush->isGround()) { error="Highland material requires an exact ground brush: "+spec.world.highlandBrushName; return false; }
				highland.brush=brush->asGround();
			}
			for (const auto& building : plan.buildings) if (!plan.catalog[building.blueprint].prefabPath.empty()) ++stats.prefabBuildings;

			// Copy context, including untouched void cells and the outer ring. Borders see
			// final neighbors without ever exposing a partly generated map to the editor.
			for (const auto& layer : plan.layers) {
				const int z = spec.region.z + layer.offsetZ;
				for (int y = -1; y <= spec.region.height; ++y) {
					if ((y & 15) == 0 && !Update(progress, "Copying existing context", 5, error)) {
						return false;
					}
					for (int x = -1; x <= spec.region.width; ++x) {
						Position pos(spec.region.originX + x, spec.region.originY + y, z);
						if (!pos.isValid()) {
							continue;
						}
						Tile* original = editor.map.getTile(pos);
						if (!original) {
							continue;
						}
						Tile* copy = original->deepCopy(scratch);
						copy->setLocation(scratch.createTileL(pos));
						scratch.setTile(pos, copy);
					}
				}
			}
			for (const auto& layer : plan.layers) {
				for (int y = 0; y < spec.region.height; ++y) {
					if ((y & 15) == 0 && !Update(progress, "Preparing grounds and walls", 15, error)) {
						return false;
					}
					for (int x = 0; x < spec.region.width; ++x) {
						const auto& cell = layer.cells[size_t(y) * spec.region.width + x];
						if (cell.terrain == WorldGen::Terrain::Void) {
							continue;
						}
						const auto pos = PositionFor(spec, { x, y }, layer.offsetZ);
						if (Tile* authored = prefabs.TileAt(plan, x, y, layer.offsetZ)) {
							Tile* tile = authored->deepCopy(scratch);
							tile->setLocation(scratch.createTileL(pos));
							scratch.setTile(pos, tile, true);
							if (WorldGen::IsWalkable(cell) && !TileWalkable(tile)) decorativeBlocking.insert(pos);
							++stats.prefabTiles;
							continue;
						}
						Tile* tile = scratch.allocator(scratch.createTileL(pos));
						if (cell.flags & WorldGen::Roof) ++stats.roofTiles;
						const int building = prefabs.owners[size_t(y) * spec.region.width + x];
						const int owner = building < 0 ? -1 : plan.buildings[building].blueprint;
						const BuildingMaterial* style = owner >= 0 && layer.offsetZ <= 0 ? &buildingMaterials[owner][(cell.flags & WorldGen::Roof) ? 4 : size_t(-layer.offsetZ)] : nullptr;
						const auto& material = style && cell.terrain == WorldGen::Terrain::BuildingFloor && style->floor.brush ? style->floor : ((cell.flags & WorldGen::Highland) && WorldGen::IsWalkable(cell) && cell.terrain != WorldGen::Terrain::Snow && highland.brush ? highland : materials[size_t(cell.terrain)]);
						WallBrush* selectedWall = style && style->wall ? style->wall : wall;
						if (material.tileId) {
							tile->addItem(Item::Create(uint16_t(material.tileId)));
						} else {
							const uint32_t seed = spec.pipeline.randomizeGround ? CellSeed(spec.seed, x, y, layer.offsetZ, 1) : CellSeed(spec.seed, int(cell.terrain), 0, 0, 1);
							ScopedMTRandom random(seed);
							material.brush->draw(&scratch, tile, nullptr);
						}
						scratch.setTile(pos, tile, true);
						if (!tile->ground || (WorldGen::IsWalkable(cell) && g_items[tile->ground->getID()].unpassable)) {
							error = wxString::Format("World material at %d,%d,%d has no ground or blocks a planned walkable cell. Correct the legend for %s.", pos.x, pos.y, pos.z, wxString::FromUTF8(WorldGen::TerrainName(cell.terrain)));
							return false;
						}
						if (selectedWall && (cell.flags & WorldGen::Wall)) {
							ScopedMTRandom random(CellSeed(spec.seed, x, y, layer.offsetZ, 2));
							selectedWall->draw(&scratch, tile, nullptr);
						}
					}
				}
			}
			for (const auto& layer : plan.layers) {
				for (int y = 0; y < spec.region.height; ++y) {
					if ((y & 15) == 0 && !Update(progress, "Resolving borders and walls", 45, error)) {
						return false;
					}
					for (int x = 0; x < spec.region.width; ++x) {
						const auto& cell = layer.cells[size_t(y) * spec.region.width + x];
						if (cell.terrain == WorldGen::Terrain::Void) {
							continue;
						}
						if (prefabs.Covers(plan, x, y, layer.offsetZ)) continue;
						Tile* tile = scratch.getTile(PositionFor(spec, { x, y }, layer.offsetZ));
						ScopedMTRandom random(CellSeed(spec.seed, x, y, layer.offsetZ, 3));
						if (spec.pipeline.borderizeAfter) {
							tile->borderize(&scratch);
						}
						if (cell.flags & WorldGen::Wall) {
							tile->wallize(&scratch);
						}
					}
				}
			}
			// Window brushes reuse the active wall's own orientation and client bindings.
			DoorBrush windows(WALL_WINDOW);
			for (const auto& b : plan.buildings) {
				const auto blueprint=WorldGen::RotateBlueprint(plan.catalog[b.blueprint], b.rotation);
				if (!blueprint.windowChance || !blueprint.prefabPath.empty()) continue;
				for (int level=0; level<b.levels; ++level) {
					const auto& rows=WorldGen::BlueprintRowsForFloor(blueprint, level);
					for (int y=0; y<b.height; ++y) for (int x=0; x<b.width; ++x) {
						if (rows[y][x]!='#' || CellSeed(spec.seed,b.origin.x+x,b.origin.y+y,-level,31)%100 >= unsigned(blueprint.windowChance)) continue;
						bool exterior=false;
						for (const auto [dx,dy] : {std::pair{1,0},std::pair{-1,0},std::pair{0,1},std::pair{0,-1}}) exterior |= x+dx<0 || y+dy<0 || x+dx>=b.width || y+dy>=b.height || rows[y+dy][x+dx]==' ';
						if (!exterior) continue;
						Tile* tile=scratch.getTile(PositionFor(spec,{b.origin.x+x,b.origin.y+y},-level));
						if (!tile || !tile->hasWall()) continue;
						const auto before=tile->getWall()->getID();
						windows.draw(&scratch,tile,nullptr);
						if (tile->hasWall() && tile->getWall()->getID()!=before) ++stats.windows;
					}
				}
			}
			for (const auto& link : plan.links) {
				const auto upper = PositionFor(spec, link.upper, link.upperOffsetZ), lower = PositionFor(spec, link.lower, link.lowerOffsetZ);
				const auto upperArrival = PositionFor(spec, link.upperArrival, link.upperOffsetZ), lowerArrival = PositionFor(spec, link.lowerArrival, link.lowerOffsetZ);
				if (plan.settings.physicalStairs) {
					physicalTravel[upper]=stairIds[0]; physicalTravel[lower]=stairIds[size_t(link.stairDirection+1)];
					scratch.getTile(upper)->addItem(Item::Create(uint16_t(physicalTravel[upper])));
					scratch.getTile(lower)->addItem(Item::Create(uint16_t(physicalTravel[lower])));
					stats.stairEndpoints += 2;
					continue;
				}
				Teleport* down = newd Teleport(uint16_t(teleportId));
				down->setDestination(lowerArrival);
				scratch.getTile(upper)->addItem(down);
				Teleport* up = newd Teleport(uint16_t(teleportId));
				up->setDestination(upperArrival);
				scratch.getTile(lower)->addItem(up);
			}
			if (!Decorate(spec, plan, scratch, decorativeBlocking, stats, progress, error) || !Furnish(spec, plan, scratch, decorativeBlocking, stats, progress, error)) {
				return false;
			}
			// Final client-material pass: border rules must not block planned doors,
			// streets, cave floors or arrivals, even if a client has unusual definitions.
			for (const auto& layer : plan.layers) {
				for (int y = 0; y < spec.region.height; ++y) {
					if ((y & 15) == 0 && !Update(progress, "Validating physical navigation", 85, error)) {
						return false;
					}
					for (int x = 0; x < spec.region.width; ++x) {
						const auto& cell = layer.cells[size_t(y) * spec.region.width + x];
						if (cell.terrain == WorldGen::Terrain::Void) {
							continue;
						}
						const auto pos = PositionFor(spec, { x, y }, layer.offsetZ);
						Tile* tile = scratch.getTile(pos);
						bool blocked = !tile || !tile->ground || g_items[tile->ground->getID()].unpassable;
						if (tile && tile->ground && tile->ground->isMetaItem()) {
							error = "World grounds cannot use editor-only items that are omitted from OTBM.";
							return false;
						}
						int portals = 0;
						auto expectedStair = [&](int id) { const auto it=physicalTravel.find(pos); return it!=physicalTravel.end() && it->second==id; };
						if (tile && tile->ground && (g_items[tile->ground->getID()].isFloorChange() || g_items[tile->ground->getID()].isTeleport()) && !expectedStair(tile->ground->getID())) {
							error = wxString::Format("Ground at %d,%d,%d changes floors outside the planned travel graph.", pos.x, pos.y, pos.z);
							return false;
						}
						if (tile && tile->ground && expectedStair(tile->ground->getID())) ++portals;
						if (tile) {
							for (Item* item : tile->items) {
								blocked = blocked || g_items[item->getID()].unpassable;
								const auto& type = g_items[item->getID()];
								if (type.isMetaItem()) {
									error = "World materials cannot add editor-only objects that are omitted from OTBM.";
									return false;
								}
								if (type.isGroundTile()) {
									error = wxString::Format("Client materials add a second ground at %d,%d,%d. Correct the border definitions before applying this world.", pos.x, pos.y, pos.z);
									return false;
								}
								if ((type.isFloorChange() && !expectedStair(item->getID())) || (type.isTeleport() && (plan.settings.physicalStairs || !(cell.flags & WorldGen::Connection) || !dynamic_cast<Teleport*>(item)))) {
									error = wxString::Format("Client items at %d,%d,%d add unplanned floor travel.", pos.x, pos.y, pos.z);
									return false;
								}
								if (type.isTeleport() || type.isFloorChange()) {
									++portals;
								}
							}
						}
						if (portals != ((cell.flags & WorldGen::Connection) ? 1 : 0)) {
							error = "World travel endpoints must contain exactly their planned portal.";
							return false;
						}
						if (WorldGen::IsWalkable(cell) && blocked && !decorativeBlocking.count(pos)) {
							error = wxString::Format("Client borders or materials block the planned route at %d,%d,%d. Adjust the world legend or disable borders.", pos.x, pos.y, pos.z);
							return false;
						}
						if (!WorldGen::IsWalkable(cell) && !(cell.flags & (WorldGen::Wall | WorldGen::Roof)) && !blocked) {
							error = wxString::Format("World barrier at %d,%d,%d is walkable in the active client. Choose a blocking ground for water, mountains or cave rock.", pos.x, pos.y, pos.z);
							return false;
						}
						if ((cell.flags & WorldGen::Wall) && ((!prefabs.Covers(plan, x, y, layer.offsetZ) && !tile->hasWall()) || !blocked)) {
							error = wxString::Format("The selected building wall brush does not produce a blocking wall at %d,%d,%d.", pos.x, pos.y, pos.z);
							return false;
						}
					}
				}
			}
			if (!decorativeBlocking.empty()) {
				auto physical = plan;
				for (const auto& pos : decorativeBlocking) {
					const int floor = pos.z - spec.region.z;
					const WorldGen::Point local { pos.x - spec.region.originX, pos.y - spec.region.originY };
					physical.FindLayer(floor)->cells[physical.Index(local)].terrain = floor > 0 ? WorldGen::Terrain::CaveWall : WorldGen::Terrain::Mountain;
				}
				const auto connectivity = WorldGen::Validate(physical);
				if (!connectivity.valid) {
					error = "Decoration would break world connectivity.";
					for (const auto& issue : connectivity.errors) {
						error << "\n"
							  << wxstr(issue);
					}
					return false;
				}
			}

			return true;
		}
	}

	bool BuildPlan(Editor& editor, const GenerationSpec& spec, GenerationSpec& resolved, WorldGen::Plan& plan, wxString& error, const WorldGen::ProgressCallback& progress) {
		error.clear();
		resolved = spec;
		// World floors have their own bounds; legacy elevation is unrelated.
		resolved.elevation.maxLevels = 0;
		if (!ProceduralCommon::ResolveRegion(editor, resolved, error)) {
			return false;
		}
		if (!CheckRegion(editor, resolved, nullptr, error)) {
			return false;
		}
		auto settings = spec.world.settings;
		settings.width = resolved.region.width;
		settings.height = resolved.region.height;
		settings.seed = spec.seed;
		if (resolved.region.z - std::max(settings.buildingLevels - 1, settings.mountainLevels) < 0 || resolved.region.z + settings.caveDepth > MAP_MAX_LAYER) {
			error = "Requested world building/cave floors exceed the map z range.";
			return false;
		}
		std::string buildError;
		WorldGen::BlueprintCatalog catalog;
		wxString catalogPath = spec.world.catalogPath;
		const bool explicitCatalog = !catalogPath.empty();
		if (catalogPath.empty()) {
			catalogPath = g_gui.GetProceduralDataDirectory() + "buildings.json";
		}
		if (explicitCatalog || wxFileExists(catalogPath)) {
			if (!WorldGen::LoadBlueprintCatalog(nstr(catalogPath), catalog, buildError)) {
				error = wxstr(buildError);
				return false;
			}
		} else {
			catalog = WorldGen::DefaultBlueprints();
		}
		WorldGen::GeographyInput input;
		for (int type = 0; type < 3; ++type) {
			const wxString path = type == 0 ? spec.world.heightmapPath : (type == 1 ? spec.world.biomeHintsPath : spec.world.landMaskPath);
			if (path.empty()) {
				continue;
			}
			wxImage image;
			if (!WorldGen::LoadWorldPNG(path, image, error)) {
				return false;
			}
			if (!(type == 0 ? WorldGen::DecodeHeightmap(image, settings.width, settings.height, input, error) : (type == 1 ? WorldGen::DecodeBiomeHints(image, settings.width, settings.height, input, error) : WorldGen::DecodeLandMask(image, settings.width, settings.height, input, error)))) {
				return false;
			}
		}
		if (!WorldGen::Build(settings, plan, buildError, &catalog, &input, progress)) {
			error = wxstr(buildError);
			return false;
		}
		if (resolved.region.z + WorldGen::HighestOffset(plan) < 0) { error = "Requested roofs/highlands exceed the map z range."; return false; }
		const auto report = WorldGen::Validate(plan);
		if (!report.valid) {
			error = "World plan validation failed:";
			for (const auto& e : report.errors) {
				error << "\n"
					  << wxstr(e);
			}
			return false;
		}
		return true;
	}

	bool ApplyPlan(Editor& editor, const GenerationSpec& resolved, const WorldGen::Plan& plan, wxString& error, ApplicationStats* stats, const WorldGen::ProgressCallback& progress, size_t tileBudget) {
		error.clear();
		if (!tileBudget || tileBudget > MaximumTileBudget) {
			error = wxString::Format("World tile budget must be between 1 and %llu for this build.", static_cast<unsigned long long>(MaximumTileBudget));
			return false;
		}

		if (!Update(progress, "Checking target and materials", 0, error)) {
			return false;
		}
		const auto report = WorldGen::Validate(plan);
		if (!report.valid || resolved.region.width != plan.settings.width || resolved.region.height != plan.settings.height) {
			error = "Cannot apply an invalid or mismatched world plan.";
			return false;
		}
		if (resolved.seed != plan.settings.seed) {
			error = "World seed differs from the displayed plan. Rebuild the preview before applying.";
			return false;
		}
		if (!CheckRegion(editor, resolved, &plan, error)) {
			return false;
		}
		if (!CheckTarget(editor, resolved, plan, tileBudget, error)) {
			return false;
		}
		WorldPrefab::Library prefabs;
		if (!WorldPrefab::Load(plan, prefabs, error, progress)) return false;
		std::array<Material, kTerrainCount> materials {};
		WallBrush* wall = nullptr;
		int teleportId = 0;
		if (!ResolveMaterials(resolved, plan, prefabs, materials, wall, teleportId, error)) {
			return false;
		}
		BaseMap scratch;
		ApplicationStats result;
		std::vector<BuildingMaterials> buildingMaterials;
		if (!ResolveBuildingMaterials(plan, buildingMaterials, error)) {
			return false;
		}
		if (!BuildScratch(editor, resolved, plan, materials, buildingMaterials, prefabs, wall, teleportId, scratch, error, result, progress)) {
			return false;
		}
		if (!Update(progress, "Applying world as one undo operation", 95, error)) {
			return false;
		}
		// No editor tile changed during preparation. One action owns each replacement
		// exactly once, so generation, borders, walls and travel undo together.
		std::unique_ptr<BatchAction> batch(editor.actionQueue->createBatch(ACTION_DRAW));
		std::unique_ptr<Action> action(editor.actionQueue->createAction(batch.get()));
		for (const auto& layer : plan.layers) {
			for (int y = 0; y < resolved.region.height; ++y) {
				for (int x = 0; x < resolved.region.width; ++x) {
					if (layer.cells[size_t(y) * resolved.region.width + x].terrain == WorldGen::Terrain::Void) {
						continue;
					}
					const auto pos = PositionFor(resolved, { x, y }, layer.offsetZ);
					Tile* tile = scratch.swapTile(pos, nullptr);
					tile->setLocation(editor.map.createTileL(pos));
					action->addChange(newd Change(tile));
					++result.changedTiles;
				}
			}
		}
		batch->addAction(action.release());
		editor.actionQueue->resetTimer();
		editor.addBatch(batch.release(), 0);
		editor.actionQueue->resetTimer();
		if (stats) {
			*stats = result;
		}
		if (g_gui.GetCurrentEditor() == &editor) {
			g_gui.SetScreenCenterPosition(PositionFor(resolved, { resolved.region.width / 2, resolved.region.height / 2 }, 0));
		}
		g_gui.RefreshView();
		wxString status = wxString::Format("World generated: %u tiles, %u doodads, %u furniture (%u optional placements skipped).", unsigned(result.changedTiles), unsigned(result.doodads), unsigned(result.furniture), unsigned(result.skippedDoodads + result.skippedFurniture));
		if (result.prefabTiles) status << wxString::Format(" %u authored OTBM tiles in %u buildings.", unsigned(result.prefabTiles), unsigned(result.prefabBuildings));
		if (!result.warnings.empty()) {
			status << " " << result.warnings.front();
		}
		g_gui.SetStatusText(status);
		return true;
	}

	bool Generate(Editor& editor, const GenerationSpec& spec, wxString& error) {
		GenerationSpec resolved;
		WorldGen::Plan plan;
		if (!BuildPlan(editor, spec, resolved, plan, error)) {
			return false;
		}
		return ApplyPlan(editor, resolved, plan, error);
	}

	wxImage Render(const WorldGen::Plan& plan, int floor, PreviewLayer view) {
		const auto* layer = plan.FindLayer(floor);
		if (!layer) {
			return wxImage();
		}
		wxImage image(plan.settings.width, plan.settings.height);
		if (!image.IsOk()) return wxImage();
		image.InitAlpha();
		if (!image.HasAlpha()) return wxImage();
		for (int y = 0; y < plan.settings.height; ++y) {
			for (int x = 0; x < plan.settings.width; ++x) {
				const auto& cell = layer->cells[size_t(y) * plan.settings.width + x];
				uint32_t color = WorldGen::TerrainColor(cell.terrain);
				if (view == PreviewLayer::Terrain) {
					if (cell.flags & WorldGen::Roof) color = 0xC07040;
					if (cell.flags & WorldGen::Wall) {
						color = 0x404040;
					}
					if (cell.flags & WorldGen::Entrance) {
						color = 0xFF40FF;
					}
					if (cell.flags & WorldGen::Connection) {
						color = 0xFFFF00;
					}
				} else if (view == PreviewLayer::ConstructionRoles) {
					color = ((color >> 2) & 0x3F3F3F) + 0x202020;
				} else {
					const int value = view == PreviewLayer::Elevation ? cell.elevation : (view == PreviewLayer::Moisture ? cell.moisture : cell.temperature);
					const uint32_t amount = uint32_t(value) * 255 / 10000;
					color = view == PreviewLayer::Moisture ? (amount << 8) | 0xFF : (view == PreviewLayer::Temperature ? (amount << 16) | ((255 - amount) << 8) : amount * 0x010101);
				}
				image.SetRGB(x, y, uint8_t(color >> 16), uint8_t(color >> 8), uint8_t(color));
				image.SetAlpha(x, y, cell.terrain == WorldGen::Terrain::Void ? 0 : 255);
			}
		}
		if (view == PreviewLayer::ConstructionRoles) {
			for (const auto& building : plan.buildings) {
				if (floor > 0 || -floor >= building.levels) {
					continue;
				}
				const uint32_t color = WorldGen::RoleColor(WorldGen::BlueprintRole(plan.catalog[building.blueprint]));
				for (int y = building.origin.y; y < building.origin.y + building.height; ++y) {
					for (int x = building.origin.x; x < building.origin.x + building.width; ++x) {
						const auto& cell = layer->cells[size_t(y) * plan.settings.width + x];
						if (cell.terrain != WorldGen::Terrain::BuildingFloor) {
							continue;
						}
						const uint32_t pixel = cell.flags & WorldGen::Connection ? 0xFFFF00 : (cell.flags & WorldGen::Entrance ? 0xFF40FF : (cell.flags & WorldGen::Wall ? 0x404040 : color));
						image.SetRGB(x, y, uint8_t(pixel >> 16), uint8_t(pixel >> 8), uint8_t(pixel));
					}
				}
			}
		}
		if (view == PreviewLayer::Terrain) {
			for (const auto& building : plan.buildings) {
				const auto blueprint = WorldGen::RotateBlueprint(plan.catalog[building.blueprint], building.rotation);
				for (const auto& object : blueprint.objects) {
					if (object.floor < building.levels && -object.floor == floor) {
						image.SetRGB(building.origin.x + object.x, building.origin.y + object.y, 0, 255, 255);
					}
				}
			}
		}
		return image;
	}

	wxString Describe(const WorldGen::Plan& plan, size_t* plannedTiles) {
		const auto report = WorldGen::Validate(plan);
		if (plannedTiles) *plannedTiles = report.plannedTiles;
		wxString text = wxString::Format("Seed %u | Algorithm %u | Plan %llu\n%d x %d | %u settlements | %u buildings | %u floors | %u planned vertical connections\nLand: %u tiles | Water: %u tiles | Roads/bridges: %u tiles\nYellow: travel endpoints; magenta: entrances; cyan: furniture anchors; brown: roofs; dark gray: walls. Actual furniture placement is validated against client brushes.", unsigned(plan.settings.seed), unsigned(WorldGen::AlgorithmVersion), static_cast<unsigned long long>(WorldGen::Fingerprint(plan)), plan.settings.width, plan.settings.height, unsigned(plan.settlements.size()), unsigned(plan.buildings.size()), unsigned(plan.layers.size()), unsigned(plan.links.size()), unsigned(report.landTiles), unsigned(report.waterTiles), unsigned(report.roadTiles));
		text << wxString::Format("\nPlanned tiles (all floors): %llu | Editor budget: %llu.", static_cast<unsigned long long>(report.plannedTiles), static_cast<unsigned long long>(DefaultTileBudget));
		if (report.plannedTiles > DefaultTileBudget) {
			text << " Reduce the region/floors or use 64-bit batch export with an explicit larger tile budget.";
		}
		std::map<std::string, size_t> roles;
		for (const auto& building : plan.buildings) {
			++roles[WorldGen::BlueprintRole(plan.catalog[building.blueprint])];
		}
		if (!roles.empty()) {
			text << "\nConstruction roles:";
		}
		for (const auto& role : roles) {
			text << wxString::Format(" %s=%u (#%06X)", wxString::FromUTF8(role.first), unsigned(role.second), unsigned(WorldGen::RoleColor(role.first)));
		}
		std::map<std::string, size_t> habitats;
		for (const auto& town : plan.settlements) ++habitats[WorldGen::TerrainName(town.biome)];
		if (!habitats.empty()) text << "\nSettlement habitats:";
		for (const auto& habitat : habitats) {
			text << wxString::Format(" %s=%u", wxString::FromUTF8(habitat.first), unsigned(habitat.second));
		}
		for (const auto& warning : plan.warnings) {
			text << "\n"
				 << wxstr(warning);
		}
		return text;
	}
}
