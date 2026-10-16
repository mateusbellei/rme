#include "main.h"

#include "world_editor.h"
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
#include "doodad_brush.h"

#include <array>
#include <memory>
#include <set>
#include <filesystem>

namespace WorldEditor {
	namespace {
		constexpr size_t kMaxApplyTiles = 1024 * 1024;
		constexpr size_t kTerrainCount = static_cast<size_t>(WorldGen::Terrain::Void);

		struct Material {
			GroundBrush* brush = nullptr;
			int tileId = 0;
		};

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

		std::array<std::vector<DoodadBrush*>, 6> DecorationBrushes(ApplicationStats& stats) {
			std::array<std::vector<std::string>, 6> keywords;
			keywords[1] = { "tree", "bush", "fern", "mushroom", "log" };
			keywords[2] = { "cactus", "desert", "dead", "palm", "rock" };
			keywords[3] = { "snow", "ice", "frost", "pine", "fir" };
			keywords[4] = { "stalagmite", "crystal", "rock" };
			keywords[5] = { "palm", "shell", "coral", "beach" };
			const char* names[] = { "", "forest", "desert", "ice", "cave", "coast" };
			const wxString path = g_gui.GetProceduralDataDirectory() + "biome_doodads.json";
			std::ifstream input(std::filesystem::u8path(nstr(path)), std::ios::binary);
			if (input) {
				input.seekg(0, std::ios::end);
				const auto size = input.tellg();
				input.seekg(0, std::ios::beg);
				std::string contents;
				if (size >= 0 && size <= 1024 * 1024) {
					contents.assign(std::istreambuf_iterator<char>(input), {});
					if (contents.compare(0, 3, "\xef\xbb\xbf") == 0) {
						contents.erase(0, 3);
					}
				}
				json::mValue root;
				if (!contents.empty() && json::read(contents, root) && root.type() == json::obj_type) {
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
			}
			std::array<std::vector<DoodadBrush*>, 6> brushes;
			for (int i = 1; i <= 5; ++i) {
				if (!keywords[i].empty()) {
					brushes[i] = ProceduralCommon::FindDoodadBrushes(keywords[i]);
				}
			}
			return brushes;
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
				if (type.isGroundTile() || type.isTeleport() || type.isDoor() || type.isDepot() || type.isContainer() || type.isFloorChange() || type.isWall || object.prototype->getUniqueID() || object.prototype->getActionID()) {
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
						const WorldGen::Point point { building.origin.x + object.x, building.origin.y + object.y };
						DoodadSample sample;
						wxString reason;
						std::string clearance;
						const bool sampled = found && found->isDoodad() && SampleDoodad(*found->asDoodad(), scratch, PositionFor(spec, point, offsetZ), CellSeed(spec.seed, point.x, point.y, offsetZ, 21), CellSeed(spec.seed, point.x, point.y, offsetZ, 22), CellSeed(spec.seed, point.x, point.y, offsetZ, 23), sample, reason);
						if (!sampled || !WorldGen::CanPlaceDecoration(grid, { object.x, object.y }, sample.footprint, &clearance)) {
							if (!found || !found->isDoodad()) {
								reason = "missing doodad brush";
							} else if (sampled) {
								reason = wxString::FromUTF8(clearance);
							}
							const wxString message = "Furniture '" + wxString::FromUTF8(name) + "' in template '" + wxString::FromUTF8(blueprint.id) + "': " + reason;
							if (object.required) {
								error = message;
								return false;
							}
							++stats.skippedFurniture;
							warnings.insert(message);
							continue;
						}
						PlaceDoodad(sample, scratch, PositionFor(spec, point, offsetZ), blocking);
						WorldGen::MarkDecoration(grid, { object.x, object.y }, sample.footprint);
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
			const auto brushes = DecorationBrushes(stats);
			std::array<bool, 6> used {};
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
						const uint8_t habitat = Habitat(cell.terrain);
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
						const uint32_t choice = CellSeed(spec.seed, x, y, layer.offsetZ, 11);
						const auto terrain = layer.cells[index].terrain;
						const int factor = terrain == WorldGen::Terrain::Forest ? 180 : (terrain == WorldGen::Terrain::Grass ? 40 : (habitat == 4 ? 60 : 80));
						const int patch = 60 + int(CellSeed(spec.seed, x / 12, y / 12, layer.offsetZ, 12) % 81);
						const int density = std::min(80, std::clamp(spec.doodads.density, 0, 100) * factor * patch / 10000);
						if (choice % 100 >= uint32_t(density)) {
							continue;
						}
						DoodadBrush* brush = brushes[habitat][CellSeed(spec.seed, x, y, layer.offsetZ, 13) % brushes[habitat].size()];
						DoodadSample sample;
						wxString reason;
						const auto anchor = PositionFor(spec, { x, y }, layer.offsetZ);
						if (!SampleDoodad(*brush, scratch, anchor, CellSeed(spec.seed, x, y, layer.offsetZ, 14), CellSeed(spec.seed, x, y, layer.offsetZ, 15), CellSeed(spec.seed, x, y, layer.offsetZ, 16), sample, reason) || !WorldGen::CanPlaceDecoration(grid, { x, y }, sample.footprint)) {
							++stats.skippedDoodads;
							continue;
						}
						PlaceDoodad(sample, scratch, anchor, blocking);
						WorldGen::MarkDecoration(grid, { x, y }, sample.footprint);
						++stats.doodads;
					}
				}
			}
			const char* names[] = { "", "forest/grass", "desert", "snow", "cave", "beach" };
			for (int i = 1; i <= 5; ++i) {
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

		bool CheckTarget(Editor& editor, const GenerationSpec& spec, const WorldGen::Plan& plan, wxString& error) {
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
						if (++changes > kMaxApplyTiles) {
							error = "World application exceeds 1,048,576 tiles. Reduce the region or floor count; larger plans can still be inspected with the standalone planner.";
							return false;
						}
						Position pos(spec.region.originX + x, spec.region.originY + y, z);
						const Tile* tile = editor.map.getTile(pos);
						const TileLocation* location = editor.map.getTileL(pos);
						const bool metadata = (tile && (tile->house_id || tile->spawn || tile->creature || tile->hasUniqueItem() || tile->getMapFlags() || !tile->getZoneIds().empty() || tile->isHouseExit())) || (location && (location->getWaypointCount() || location->getTownCount() || location->getSpawnCount()));
						if (metadata || (spec.world.protectExisting && tile && !tile->empty())) {
							error = wxString::Format("World generation overlaps a protected tile at %d,%d,%d. Choose an empty area%s.", pos.x, pos.y, pos.z, metadata ? " (house, spawn, creature, zone, map flags, unique item or waypoint)" : " or disable 'Protect existing tiles' to replace ordinary tiles");
							return false;
						}
					}
				}
			}
			return true;
		}

		bool ResolveMaterials(const GenerationSpec& spec, const WorldGen::Plan& plan, std::array<Material, kTerrainCount>& materials, WallBrush*& wall, int& teleportId, wxString& error) {
			const wxString path = spec.imageMask.legendPath.empty() ? g_gui.GetProceduralDataDirectory() + "world_legend.json" : spec.imageMask.legendPath;
			LegendMapping legend;
			if (!ProceduralCommon::LoadLegend(path, legend, error)) {
				return false;
			}
			ProceduralCommon::ResolveLegendBrushes(legend);
			std::array<bool, kTerrainCount> used {};
			bool needsWalls = false;
			for (const auto& layer : plan.layers) {
				for (const auto& cell : layer.cells) {
					if (cell.terrain == WorldGen::Terrain::Void) {
						continue;
					}
					used[size_t(cell.terrain)] = true;
					needsWalls = needsWalls || (cell.flags & WorldGen::Wall);
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
			if (!plan.links.empty()) {
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

		bool BuildScratch(Editor& editor, const GenerationSpec& spec, const WorldGen::Plan& plan, const std::array<Material, kTerrainCount>& materials, WallBrush* wall, int teleportId, BaseMap& scratch, wxString& error, ApplicationStats& stats, const WorldGen::ProgressCallback& progress) {
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
						Tile* tile = scratch.allocator(scratch.createTileL(pos));
						const auto& material = materials[size_t(cell.terrain)];
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
						if (wall && (cell.flags & WorldGen::Wall)) {
							ScopedMTRandom random(CellSeed(spec.seed, x, y, layer.offsetZ, 2));
							wall->draw(&scratch, tile, nullptr);
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
						Tile* tile = scratch.getTile(PositionFor(spec, { x, y }, layer.offsetZ));
						ScopedMTRandom random(CellSeed(spec.seed, x, y, layer.offsetZ, 3));
						if (spec.pipeline.borderizeAfter) {
							tile->borderize(&scratch);
						}
						if (wall && (cell.flags & WorldGen::Wall)) {
							tile->wallize(&scratch);
						}
					}
				}
			}
			for (const auto& link : plan.links) {
				const auto upper = PositionFor(spec, link.upper, link.upperOffsetZ), lower = PositionFor(spec, link.lower, link.lowerOffsetZ);
				const auto upperArrival = PositionFor(spec, link.upperArrival, link.upperOffsetZ), lowerArrival = PositionFor(spec, link.lowerArrival, link.lowerOffsetZ);
				Teleport* down = newd Teleport(uint16_t(teleportId));
				down->setDestination(lowerArrival);
				scratch.getTile(upper)->addItem(down);
				Teleport* up = newd Teleport(uint16_t(teleportId));
				up->setDestination(upperArrival);
				scratch.getTile(lower)->addItem(up);
			}
			std::set<Position> decorativeBlocking;
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
						if (tile && tile->ground && (g_items[tile->ground->getID()].isFloorChange() || g_items[tile->ground->getID()].isTeleport())) {
							error = wxString::Format("Ground at %d,%d,%d changes floors outside the planned travel graph.", pos.x, pos.y, pos.z);
							return false;
						}
						int portals = 0;
						if (tile) {
							for (Item* item : tile->items) {
								blocked = blocked || g_items[item->getID()].unpassable;
								const auto& type = g_items[item->getID()];
								if (type.isFloorChange() || (type.isTeleport() && (!(cell.flags & WorldGen::Connection) || !dynamic_cast<Teleport*>(item)))) {
									error = wxString::Format("Client items at %d,%d,%d add unplanned floor travel.", pos.x, pos.y, pos.z);
									return false;
								}
								if (type.isTeleport()) {
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
						if (!WorldGen::IsWalkable(cell) && !(cell.flags & WorldGen::Wall) && !blocked) {
							error = wxString::Format("World barrier at %d,%d,%d is walkable in the active client. Choose a blocking ground for water, mountains or cave rock.", pos.x, pos.y, pos.z);
							return false;
						}
						if ((cell.flags & WorldGen::Wall) && (!tile->hasWall() || !blocked)) {
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
		if (resolved.region.z - (settings.buildingLevels - 1) < 0 || resolved.region.z + settings.caveDepth > MAP_MAX_LAYER) {
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
		for (int type = 0; type < 2; ++type) {
			const wxString path = type == 0 ? spec.world.heightmapPath : spec.world.biomeHintsPath;
			if (path.empty()) {
				continue;
			}
			wxImage image;
			if (!WorldGen::LoadWorldPNG(path, image, error)) {
				return false;
			}
			if (!(type == 0 ? WorldGen::DecodeHeightmap(image, settings.width, settings.height, input, error) : WorldGen::DecodeBiomeHints(image, settings.width, settings.height, input, error))) {
				return false;
			}
		}
		if (!WorldGen::Build(settings, plan, buildError, &catalog, &input, progress)) {
			error = wxstr(buildError);
			return false;
		}
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

	bool ApplyPlan(Editor& editor, const GenerationSpec& resolved, const WorldGen::Plan& plan, wxString& error, ApplicationStats* stats, const WorldGen::ProgressCallback& progress) {
		error.clear();
		if (!Update(progress, "Checking target and materials", 0, error)) {
			return false;
		}
		const auto report = WorldGen::Validate(plan);
		if (!report.valid || resolved.region.width != plan.settings.width || resolved.region.height != plan.settings.height) {
			error = "Cannot apply an invalid or mismatched world plan.";
			return false;
		}
		if (!CheckRegion(editor, resolved, &plan, error)) {
			return false;
		}
		if (!CheckTarget(editor, resolved, plan, error)) {
			return false;
		}
		std::array<Material, kTerrainCount> materials {};
		WallBrush* wall = nullptr;
		int teleportId = 0;
		if (!ResolveMaterials(resolved, plan, materials, wall, teleportId, error)) {
			return false;
		}
		BaseMap scratch;
		ApplicationStats result;
		if (!BuildScratch(editor, resolved, plan, materials, wall, teleportId, scratch, error, result, progress)) {
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
		image.InitAlpha();
		for (int y = 0; y < plan.settings.height; ++y) {
			for (int x = 0; x < plan.settings.width; ++x) {
				const auto& cell = layer->cells[size_t(y) * plan.settings.width + x];
				uint32_t color = WorldGen::TerrainColor(cell.terrain);
				if (view == PreviewLayer::Terrain) {
					if (cell.flags & WorldGen::Wall) {
						color = 0x404040;
					}
					if (cell.flags & WorldGen::Entrance) {
						color = 0xFF40FF;
					}
					if (cell.flags & WorldGen::Connection) {
						color = 0xFFFF00;
					}
				} else {
					const int value = view == PreviewLayer::Elevation ? cell.elevation : (view == PreviewLayer::Moisture ? cell.moisture : cell.temperature);
					const uint32_t amount = uint32_t(value) * 255 / 10000;
					color = view == PreviewLayer::Moisture ? (amount << 8) | 0xFF : (view == PreviewLayer::Temperature ? (amount << 16) | ((255 - amount) << 8) : amount * 0x010101);
				}
				image.SetRGB(x, y, uint8_t(color >> 16), uint8_t(color >> 8), uint8_t(color));
				image.SetAlpha(x, y, cell.terrain == WorldGen::Terrain::Void ? 0 : 255);
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

	wxString Describe(const WorldGen::Plan& plan) {
		const auto report = WorldGen::Validate(plan);
		wxString text = wxString::Format("Algorithm %u | Plan %llu\n%d x %d | %u settlements | %u buildings | %u floors | %u planned vertical connections\nLand: %u tiles | Water: %u tiles | Roads/bridges: %u tiles\nYellow: travel portals; magenta: entrances; cyan: authored furniture anchors; dark gray: walls. Actual furniture placement is validated against client brushes.", unsigned(WorldGen::AlgorithmVersion), static_cast<unsigned long long>(WorldGen::Fingerprint(plan)), plan.settings.width, plan.settings.height, unsigned(plan.settlements.size()), unsigned(plan.buildings.size()), unsigned(plan.layers.size()), unsigned(plan.links.size()), unsigned(report.landTiles), unsigned(report.waterTiles), unsigned(report.roadTiles));
		for (const auto& warning : plan.warnings) {
			text << "\n"
				 << wxstr(warning);
		}
		return text;
	}
}
