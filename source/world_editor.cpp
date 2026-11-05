#include "main.h"

#include "world_editor.h"
#include "world_existing_editor.h"
#include "filehandle.h"
#include "iomap_otbm.h"
#include "town.h"
#include <wx/file.h>
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
#include <deque>
#include <memory>
#include <set>
#include <filesystem>
#include <limits>
#include <numeric>

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
		using BuildingMaterials = std::array<BuildingMaterial, 11>;

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

		bool SelectedNature(const GenerationSpec& spec,const WorldGen::Plan& plan,int x,int y,int floor) {
			if(spec.world.natureMask.empty())return true;
			const auto index=size_t(y)*plan.settings.width+x;
			const auto& cell=plan.FindLayer(floor)->cells[index];
			return floor==spec.world.natureFloor && spec.world.natureMask[index] && !(cell.flags & WorldGen::Reserved) && ((cell.flags & WorldGen::Highland) || Habitat(cell.terrain));
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

		struct ObjectPatch {
			std::string id;
			int weight=1, density=100, minElevation=0, maxElevation=10000, minMoisture=0, maxMoisture=10000, maxWaterDistance=-1, minWaterDistance=0, maxPathDistance=-1, maxEdgeDistance=-1, minSlope=0, maxSlope=10000;
			std::vector<DoodadBrush*> brushes;
		};
		constexpr const char* ObjectHabitats[] = {"none","forest","desert","ice","cave","coast","highland"};
		bool ReadObjectPalette(const json::mValue& value, std::vector<DoodadBrush*>& brushes, wxString& error) {
			if(value.type()!=json::array_type || value.get_array().size()>64) {error="Object brushes must be an array of at most 64 weighted names.";return false;}
			for(const auto& entry:value.get_array()) {
				if(entry.type()!=json::obj_type) {error="Invalid weighted object brush.";return false;}
				const auto& fields=entry.get_obj();const auto name=fields.find("name"),weight=fields.find("weight");
				if(name==fields.end() || name->second.type()!=json::str_type || name->second.get_str().empty() || name->second.get_str().size()>128 || name->second.get_str().find('\0')!=std::string::npos || (weight!=fields.end() && (weight->second.type()!=json::int_type || weight->second.get_int64()<1 || weight->second.get_int64()>100))) {error="Object brushes need an exact name and integer weight 1..100.";return false;}
				Brush* brush=g_brushes.getBrush(name->second.get_str());
				if(!brush || !brush->isDoodad()) {error="Missing exact doodad brush: "+wxString::FromUTF8(name->second.get_str());return false;}
				brushes.insert(brushes.end(),weight==fields.end()?1:size_t(weight->second.get_int64()),brush->asDoodad());
			}
			return true;
		}
		bool DecorationBrushes(const GenerationSpec& spec, const json::mValue& objectProfile, std::array<std::vector<DoodadBrush*>, 7>& brushes, std::array<int, 7>& density, std::array<std::vector<ObjectPatch>,7>& patches, std::array<int,7>& patchSizes, std::array<int,7>& baseWeights, ApplicationStats& stats, wxString& error) {
			density.fill(100);
			patchSizes.fill(16);baseWeights.fill(100);
			if (!spec.world.objectProfilePath.empty()) {
				const auto& object = objectProfile.get_obj();
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
					if(entries==biome.end() || !ReadObjectPalette(entries->second,brushes[index],error)) {if(error.empty())error="Biome needs a brushes array.";return false;}
					auto integer = [&](const json::mObject& fields,const char* key,int& target,int low,int high) {
						const auto field=fields.find(key);if(field==fields.end())return true;
						if(field->second.type()!=json::int_type || field->second.get_int64()<low || field->second.get_int64()>high) {error="Invalid ecology field: "+wxString::FromUTF8(key);return false;}
						target=int(field->second.get_int64());return true;
					};
					if(!integer(biome,"patchSize",patchSizes[index],8,64) || !integer(biome,"baseWeight",baseWeights[index],0,100))return false;
					const auto groups=biome.find("patches");
					if(groups!=biome.end()) {
						if(groups->second.type()!=json::array_type || groups->second.get_array().size()>8) {error="A biome accepts at most eight ecology patches.";return false;}
						std::set<std::string> ids;
						for(const auto& group:groups->second.get_array()) {
							if(group.type()!=json::obj_type) {error="Ecology patches must be objects.";return false;}
							const auto& fields=group.get_obj();const auto id=fields.find("id"),palette=fields.find("brushes");ObjectPatch patch;
							if(id==fields.end() || id->second.type()!=json::str_type) {error="Ecology patch needs an id.";return false;}
							patch.id=id->second.get_str();
							if(patch.id.empty() || patch.id.size()>40 || patch.id=="base" || patch.id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-")!=std::string::npos || !ids.insert(patch.id).second) {error="Ecology patch IDs must be unique bounded identifiers.";return false;}
							if(!integer(fields,"weight",patch.weight,1,100) || !integer(fields,"density",patch.density,0,200) || !integer(fields,"minElevation",patch.minElevation,0,10000) || !integer(fields,"maxElevation",patch.maxElevation,0,10000) || !integer(fields,"minMoisture",patch.minMoisture,0,10000) || !integer(fields,"maxMoisture",patch.maxMoisture,0,10000) || !integer(fields,"maxWaterDistance",patch.maxWaterDistance,0,32) || !integer(fields,"minWaterDistance",patch.minWaterDistance,0,32) || !integer(fields,"maxPathDistance",patch.maxPathDistance,0,32) || !integer(fields,"maxEdgeDistance",patch.maxEdgeDistance,0,32) || !integer(fields,"minSlope",patch.minSlope,0,10000) || !integer(fields,"maxSlope",patch.maxSlope,0,10000))return false;
							if(patch.minElevation>patch.maxElevation || patch.minMoisture>patch.maxMoisture || patch.minSlope>patch.maxSlope || (patch.maxWaterDistance>=0 && patch.minWaterDistance>patch.maxWaterDistance) || palette==fields.end()) {error="Invalid ecology ranges or missing brushes.";return false;}
							if(!ReadObjectPalette(palette->second,patch.brushes,error))return false;
							patches[index].push_back(std::move(patch));
						}
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


		struct SceneVariant {
			std::string id;
			int weight=1;
			std::vector<WorldGen::BlueprintProp> objects;
		};

		struct ObjectScene {
			std::string id;
			std::vector<int> habitats, settlements;
			int spacing=48, chance=45, radius=6;
			int minElevation=0,maxElevation=10000,minMoisture=0,maxMoisture=10000;
			bool rotate=true;
			std::vector<WorldGen::BlueprintProp> objects;
			std::vector<SceneVariant> variants;
		};

		bool ReadScenes(const GenerationSpec& spec,const json::mValue& root,std::vector<ObjectScene>& scenes,wxString& error) {
			if(spec.world.objectProfilePath.empty())return true;
			const auto list=root.get_obj().find("scenes");if(list==root.get_obj().end())return true;
			if(list->second.type()!=json::array_type || list->second.get_array().size()>32) {error="scenes must contain at most 32 bounded compositions.";return false;}
			std::set<std::string> ids;
			for(const auto& value:list->second.get_array()) {
				if(value.type()!=json::obj_type) {error="A scene must be an object.";return false;}
				const auto& fields=value.get_obj();ObjectScene scene;
				const auto id=fields.find("id"),habitats=fields.find("habitats"),objects=fields.find("objects");
				if(id==fields.end() || id->second.type()!=json::str_type) {error="Scene needs an id.";return false;}
				scene.id=id->second.get_str();
				if(scene.id.empty() || scene.id.size()>40 || scene.id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-")!=std::string::npos || !ids.insert(scene.id).second) {error="Scene IDs must be unique bounded identifiers.";return false;}
				auto integer=[&](const json::mObject& f,const char* key,int& target,int low,int high) {
					const auto i=f.find(key);if(i==f.end())return true;
					if(i->second.type()!=json::int_type || i->second.get_int64()<low || i->second.get_int64()>high) {error="Invalid scene field: "+wxString::FromUTF8(key);return false;}
					target=int(i->second.get_int64());return true;
				};
				if(!integer(fields,"spacing",scene.spacing,16,128) || !integer(fields,"chance",scene.chance,1,100) || !integer(fields,"radius",scene.radius,2,12) || !integer(fields,"minElevation",scene.minElevation,0,10000) || !integer(fields,"maxElevation",scene.maxElevation,0,10000) || !integer(fields,"minMoisture",scene.minMoisture,0,10000) || !integer(fields,"maxMoisture",scene.maxMoisture,0,10000))return false;
				if(scene.minElevation>scene.maxElevation || scene.minMoisture>scene.maxMoisture || scene.spacing<scene.radius*2+4) {error="Invalid scene climate range or spacing.";return false;}
				const auto settlements=fields.find("settlements");
				if(settlements!=fields.end()) {
					if(settlements->second.type()!=json::array_type || settlements->second.get_array().empty() || settlements->second.get_array().size()>32){error="Scene settlements must contain 1..32 ordinals.";return false;}
					for(const auto& ordinal:settlements->second.get_array()) {
						if(ordinal.type()!=json::int_type || ordinal.get_int64()<1 || ordinal.get_int64()>32 || std::find(scene.settlements.begin(),scene.settlements.end(),int(ordinal.get_int64()))!=scene.settlements.end()){error="Scene settlement ordinals must be unique integers 1..32.";return false;}
						scene.settlements.push_back(int(ordinal.get_int64()));
					}
				}
				const auto rotate=fields.find("rotate");if(rotate!=fields.end()) {if(rotate->second.type()!=json::bool_type){error="Scene rotate must be boolean.";return false;}scene.rotate=rotate->second.get_bool();}
				if(habitats==fields.end() || habitats->second.type()!=json::array_type || habitats->second.get_array().empty() || habitats->second.get_array().size()>6) {error="Scene needs 1..6 habitat names.";return false;}
				for(const auto& h:habitats->second.get_array()) {
					if(h.type()!=json::str_type){error="Scene habitats must be names.";return false;}
					int index=0;for(int i=1;i<=6;++i)if(h.get_str()==ObjectHabitats[i])index=i;
					if(!index || std::find(scene.habitats.begin(),scene.habitats.end(),index)!=scene.habitats.end()) {error="Unknown or duplicate scene habitat.";return false;}
					scene.habitats.push_back(index);
				}
				auto readObjects=[&](const json::mValue& value,std::vector<WorldGen::BlueprintProp>& target) {
				if(value.type()!=json::array_type || value.get_array().empty() || value.get_array().size()>24) {error="Scene needs 1..24 object anchors.";return false;}
				std::set<std::pair<int,int>> positions;
				for(const auto& object:value.get_array()) {
					if(object.type()!=json::obj_type){error="Scene objects must be objects.";return false;}
					const auto& f=object.get_obj();WorldGen::BlueprintProp prop;
					if(f.find("x")==f.end() || f.find("y")==f.end() || !integer(f,"x",prop.x,-12,12) || !integer(f,"y",prop.y,-12,12) || std::abs(prop.x)>scene.radius || std::abs(prop.y)>scene.radius || !positions.insert({prop.x,prop.y}).second) {error="Scene object anchors must be distinct and inside radius.";return false;}
					const auto names=f.find("brushes");
					if(names==f.end() || names->second.type()!=json::array_type || (names->second.get_array().size()!=1 && names->second.get_array().size()!=4)) {error="Scene object needs one or four clockwise exact brushes.";return false;}
					for(const auto& name:names->second.get_array()) {
						if(name.type()!=json::str_type || name.get_str().empty() || name.get_str().size()>128 || name.get_str().find('\0')!=std::string::npos) {error="Invalid scene brush name.";return false;}
						const auto* brush=g_brushes.getBrush(name.get_str());if(!brush || !brush->isDoodad()) {error="Missing exact scene doodad brush: "+wxString::FromUTF8(name.get_str());return false;}
						prop.brushes.push_back(name.get_str());
					}
					target.push_back(std::move(prop));
				}
                    return true;
                };
                const auto variants=fields.find("variants");
                if((objects==fields.end())==(variants==fields.end())) {error="A scene needs exactly one of objects or variants.";return false;}
                if(objects!=fields.end()) {if(!readObjects(objects->second,scene.objects))return false;}
                else {
                    if(variants->second.type()!=json::array_type || variants->second.get_array().empty() || variants->second.get_array().size()>8) {error="Scene variants must contain 1..8 compositions.";return false;}
                    std::set<std::string> variantIds;
                    for(const auto& value:variants->second.get_array()) {
                        if(value.type()!=json::obj_type){error="Scene variants must be objects.";return false;}
                        const auto& f=value.get_obj();const auto id=f.find("id"),props=f.find("objects");SceneVariant variant;
                        if(id==f.end() || id->second.type()!=json::str_type){error="Scene variant needs an id.";return false;}
                        variant.id=id->second.get_str();
                        if(variant.id.empty() || variant.id.size()>40 || variant.id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-")!=std::string::npos || !variantIds.insert(variant.id).second || !integer(f,"weight",variant.weight,1,100)) {error="Invalid scene variant identifier or weight.";return false;}
                        if(props==f.end() || !readObjects(props->second,variant.objects)){if(error.empty())error="Scene variant needs objects.";return false;}
                        scene.variants.push_back(std::move(variant));
                    }
                }
				scenes.push_back(std::move(scene));
			}
			return true;
		}

        void CountDecoration(ApplicationStats& stats,size_t regionStart,int width,int x,int y) {
            const size_t region=regionStart+size_t(y/32)*((width+31)/32)+x/32;
            ++stats.decorationRegions[region].objectAnchors;
        }

		bool PlaceScenes(const GenerationSpec& spec,const WorldGen::Plan& plan,const WorldGen::Layer& layer,const std::vector<ObjectScene>& scenes,WorldGen::DecorationGrid& grid,BaseMap& scratch,std::set<Position>& blocking,ApplicationStats& stats,const WorldGen::ProgressCallback& progress,wxString& error,size_t regionStart) {
			const auto seed=spec.world.objectSeed.value_or(spec.seed);
			for(size_t sceneIndex=0;sceneIndex<scenes.size();++sceneIndex) {
				const auto& scene=scenes[sceneIndex];
				const std::vector<WorldGen::DecorationCell> clearance {
					{{-scene.radius,-scene.radius},false},{{scene.radius,-scene.radius},false},
					{{-scene.radius,scene.radius},false},{{scene.radius,scene.radius},false}
				};
				for(int gy=0;gy<grid.height;gy+=scene.spacing) {
					if(!Update(progress,"Composing wilderness scenes",63,error))return false;
					for(int gx=0;gx<grid.width;gx+=scene.spacing) {
						const auto key=CellSeed(seed^uint32_t(sceneIndex*7919),gx,gy,layer.offsetZ,201);
						if(key%100>=uint32_t(scene.chance))continue;
						const int x=gx+int(CellSeed(key,0,0,0,202)%scene.spacing),y=gy+int(CellSeed(key,0,0,0,203)%scene.spacing);
						if(x>=grid.width || y>=grid.height)continue;
						const size_t index=size_t(y)*grid.width+x;const auto& cell=layer.cells[index];
						if(std::find(scene.habitats.begin(),scene.habitats.end(),grid.habitat[index])==scene.habitats.end() || cell.elevation<scene.minElevation || cell.elevation>scene.maxElevation || cell.moisture<scene.minMoisture || cell.moisture>scene.maxMoisture)continue;
						if(!scene.settlements.empty()) {
							int nearest=0,best=std::numeric_limits<int>::max();
							for(size_t t=0;t<plan.settlements.size();++t) {
								const auto center=plan.settlements[t].center;const int distance=std::abs(x-center.x)+std::abs(y-center.y);
								if(distance<best){nearest=int(t)+1;best=distance;}
							}
							if(std::find(scene.settlements.begin(),scene.settlements.end(),nearest)==scene.settlements.end())continue;
						}
						if(!WorldGen::CanPlaceDecoration(grid,{x,y},clearance)) {++stats.skippedScenes;continue;}
						const std::vector<WorldGen::BlueprintProp>* objects=&scene.objects;
                        std::string variantId;
                        if(!scene.variants.empty()) {
                            int total=0;for(const auto& variant:scene.variants)total+=variant.weight;
                            int roll=int(CellSeed(key,0,0,0,208)%uint32_t(total));
                            for(const auto& variant:scene.variants){if(roll<variant.weight){objects=&variant.objects;variantId=variant.id;break;}roll-=variant.weight;}
                        }
                        const int turn=scene.rotate?int(CellSeed(key,0,0,0,204)%4):0;
						std::vector<DoodadSample> samples;std::vector<WorldGen::Point> offsets;std::vector<std::string> names;
						std::map<std::pair<int,int>,bool> footprint;bool valid=true;
						for(size_t n=0;n<objects->size();++n) {
							const auto& object=(*objects)[n];WorldGen::Point offset{object.x,object.y};
							for(int t=0;t<turn;++t)offset={-offset.y,offset.x};
							const Position anchor=PositionFor(spec,{x+offset.x,y+offset.y},layer.offsetZ);
							if(!scratch.getTile(anchor)) {valid=false;break;}
							const auto& name=object.brushes[object.brushes.size()==4?size_t(turn):0];
							DoodadSample sample;wxString reason;
							if(!SampleDoodad(*g_brushes.getBrush(name)->asDoodad(),scratch,anchor,CellSeed(key,int(n),0,0,205),CellSeed(key,int(n),0,0,206),CellSeed(key,int(n),0,0,207),sample,reason)) {++stats.objectBrushRejected[name];valid=false;break;}
							for(const auto& f:sample.footprint) {
								const auto point=std::pair{offset.x+f.offset.x,offset.y+f.offset.y};
								if(std::abs(point.first)>scene.radius || std::abs(point.second)>scene.radius || !footprint.emplace(point,f.blocking).second)valid=false;
							}
							if(!valid)break;
							samples.push_back(std::move(sample));offsets.push_back(offset);names.push_back(name);
						}
						std::vector<WorldGen::DecorationCell> combined;
						for(const auto& [point,blocked]:footprint)combined.push_back({{point.first,point.second},blocked});
						// Corners impose a clear square around the composition without blocking it.
						for(const int dx:{-scene.radius,scene.radius})for(const int dy:{-scene.radius,scene.radius})if(!footprint.count({dx,dy}))combined.push_back({{dx,dy},false});
						if(!valid || !WorldGen::CanPlaceDecoration(grid,{x,y},combined)) {++stats.skippedScenes;continue;}
						for(size_t n=0;n<samples.size();++n) {
							PlaceDoodad(samples[n],scratch,PositionFor(spec,{x+offsets[n].x,y+offsets[n].y},layer.offsetZ),blocking);
							++stats.doodads;++stats.objectBrushUsage[names[n]];
                            CountDecoration(stats,regionStart,grid.width,x+offsets[n].x,y+offsets[n].y);
						}
						WorldGen::MarkDecoration(grid,{x,y},combined);
						for(int dy=-scene.radius;dy<=scene.radius;++dy)for(int dx=-scene.radius;dx<=scene.radius;++dx)grid.reserved[size_t(y+dy)*grid.width+x+dx]=1;
						++stats.decorationRegions[regionStart+size_t(y/32)*((grid.width+31)/32)+x/32].scenes;
						++stats.sceneUsage[scene.id];
						if(!variantId.empty())++stats.sceneVariantUsage[scene.id+"/"+variantId];
						if(stats.scenePlacements.size()<4096)stats.scenePlacements.push_back({scene.id,PositionFor(spec,{x,y},layer.offsetZ),turn,samples.size(),variantId});
					}
				}
			}
			return true;
		}

		bool Decorate(const GenerationSpec& spec, const WorldGen::Plan& plan, BaseMap& scratch, std::set<Position>& blocking, ApplicationStats& stats, const WorldGen::ProgressCallback& progress, wxString& error) {
			if (!spec.doodads.enabled || spec.doodads.density <= 0) {
				return true;
			}
			std::array<std::vector<DoodadBrush*>, 7> brushes;
			std::array<int, 7> densityScale, patchSizes, baseWeights;
			std::array<std::vector<ObjectPatch>,7> patches;
			json::mValue profile;
			if(!spec.world.objectProfilePath.empty()) {
				std::string problem;
				if(!ProceduralJSON::ReadFile(nstr(spec.world.objectProfilePath),1024*1024,profile,problem) || profile.type()!=json::obj_type) {error="Invalid biome object profile: "+wxString::FromUTF8(problem);return false;}
			}
			if (!DecorationBrushes(spec, profile, brushes, densityScale, patches, patchSizes, baseWeights, stats, error)) return false;
			std::vector<ObjectScene> scenes;if(!ReadScenes(spec,profile,scenes,error))return false;
			bool environmentNeeded=false;
            for(const auto& biome:patches)for(const auto& p:biome)environmentNeeded|=p.maxWaterDistance>=0 || p.minWaterDistance>0 || p.maxPathDistance>=0 || p.maxEdgeDistance>=0 || p.minSlope>0 || p.maxSlope<10000;
            int edgeWidth=0,edgeDensity=100,pathWidth=0,pathDensity=100,slopeThreshold=0,slopeDensity=100;
            if(profile.type()==json::obj_type) {
                const auto transitions=profile.get_obj().find("transitions");
                if(transitions!=profile.get_obj().end()) {
                    if(transitions->second.type()!=json::obj_type){error="transitions must be an object.";return false;}
                    const auto& fields=transitions->second.get_obj();
                    for(const auto& [name,value]:fields)if(name!="forestEdgeWidth" && name!="forestEdgeDensity" && name!="pathWidth" && name!="pathDensity" && name!="slopeThreshold" && name!="slopeDensity"){error="Unknown transition field: "+wxString::FromUTF8(name);return false;}
                    auto integer=[&](const char* name,int& target,int high) {const auto f=fields.find(name);if(f==fields.end())return true;if(f->second.type()!=json::int_type || f->second.get_int64()<0 || f->second.get_int64()>high){error="Invalid transition field: "+wxString::FromUTF8(name);return false;}target=int(f->second.get_int64());return true;};
                    if(!integer("forestEdgeWidth",edgeWidth,32) || !integer("forestEdgeDensity",edgeDensity,100) || !integer("pathWidth",pathWidth,32) || !integer("pathDensity",pathDensity,100) || !integer("slopeThreshold",slopeThreshold,10000) || !integer("slopeDensity",slopeDensity,100))return false;
                    environmentNeeded|=edgeWidth>0 || pathWidth>0 || slopeThreshold>0;
                }
            }
			const uint32_t objectSeed = spec.world.objectSeed.value_or(spec.seed);
			std::array<bool, 7> used {};
			for (const auto& layer : plan.layers) {
				if(!spec.world.natureMask.empty() && layer.offsetZ!=spec.world.natureFloor)continue;
				bool eligible=false;
				for(size_t i=0;i<layer.cells.size();++i) {
					const auto& cell=layer.cells[i];
					const bool habitat=((cell.flags & WorldGen::Highland) && WorldGen::IsWalkable(cell) && cell.terrain!=WorldGen::Terrain::Snow) || Habitat(cell.terrain);
					if(habitat && !(cell.flags & WorldGen::Reserved) && (spec.world.natureMask.empty() || spec.world.natureMask[i])){eligible=true;break;}
				}
				if(!eligible)continue;
				WorldGen::DecorationGrid grid;
				grid.width = plan.settings.width;
				grid.height = plan.settings.height;
				const size_t count = layer.cells.size();
				grid.walkable.resize(count);
				grid.reserved.resize(count);
				grid.occupied.assign(count, 0);
				grid.habitat.resize(count);
                WorldGen::DecorationEnvironment environment;
                if(environmentNeeded && !WorldGen::BuildDecorationEnvironment(layer,grid.width,grid.height,environment)){error="Invalid decoration environment.";return false;}

				for (int y = 0; y < grid.height; ++y) {
					for (int x = 0; x < grid.width; ++x) {
						const size_t index = size_t(y) * grid.width + x;
						const auto& cell = layer.cells[index];
						const uint8_t habitat = (cell.flags & WorldGen::Highland) && WorldGen::IsWalkable(cell) && cell.terrain != WorldGen::Terrain::Snow ? 6 : Habitat(cell.terrain);
						grid.habitat[index] = habitat;
						grid.reserved[index] = !habitat || (cell.flags & WorldGen::Reserved) || !SelectedNature(spec,plan,x,y,layer.offsetZ);
						grid.walkable[index] = WorldGen::IsWalkable(cell) && TileWalkable(scratch.getTile(PositionFor(spec, { x, y }, layer.offsetZ)));
						if (habitat && !grid.reserved[index]) {
							used[habitat] = true;
						}
					}
				}
				const size_t regionStart=stats.decorationRegions.size();
                for(int y=0;y<grid.height;y+=32)for(int x=0;x<grid.width;x+=32)stats.decorationRegions.push_back({PositionFor(spec,{x,y},layer.offsetZ),std::min(32,grid.width-x),std::min(32,grid.height-y),0,0,0});
                for(int y=0;y<grid.height;++y)for(int x=0;x<grid.width;++x){const size_t i=size_t(y)*grid.width+x;if(grid.habitat[i] && !grid.reserved[i] && grid.walkable[i])++stats.decorationRegions[regionStart+size_t(y/32)*((grid.width+31)/32)+x/32].naturalTiles;}
                if(!PlaceScenes(spec,plan,layer,scenes,grid,scratch,blocking,stats,progress,error,regionStart))return false;
				for (int y = 1; y < grid.height - 1; ++y) {
					if ((y & 15) == 0 && !Update(progress, "Decorating biomes", 65, error)) {
						return false;
					}
					for (int x = 1; x < grid.width - 1; ++x) {
						const size_t index = size_t(y) * grid.width + x;
						const uint8_t habitat = grid.habitat[index];
						if (!habitat || grid.reserved[index] || !grid.walkable[index] || grid.occupied[index]) {
							continue;
						}
						const auto& cell=layer.cells[index];
						const auto* palette=&brushes[habitat];const ObjectPatch* selected=nullptr;
						if(!patches[habitat].empty()) {
							auto eligible=[&](const ObjectPatch& p){return cell.elevation>=p.minElevation && cell.elevation<=p.maxElevation && cell.moisture>=p.minMoisture && cell.moisture<=p.maxMoisture && (p.maxWaterDistance<0 || environment.water[index]<=p.maxWaterDistance) && (p.minWaterDistance==0 || environment.water[index]>=p.minWaterDistance) && (p.maxPathDistance<0 || environment.path[index]<=p.maxPathDistance) && (p.maxEdgeDistance<0 || environment.forestEdge[index]<=p.maxEdgeDistance) && (p.minSlope==0 || environment.slope[index]>=p.minSlope) && (p.maxSlope==10000 || environment.slope[index]<=p.maxSlope);};
							int total=baseWeights[habitat];for(const auto& p:patches[habitat])if(eligible(p))total+=p.weight;
							if(!total)continue;
							int roll=int(CellSeed(objectSeed,x/patchSizes[habitat],y/patchSizes[habitat],layer.offsetZ,41)%uint32_t(total))-baseWeights[habitat];
							if(roll>=0)for(const auto& p:patches[habitat])if(eligible(p)){if(roll<p.weight){selected=&p;palette=&p.brushes;break;}roll-=p.weight;}
						}
						if(palette->empty())continue;
						const uint32_t choice = CellSeed(objectSeed, x, y, layer.offsetZ, 11);
						const auto terrain = layer.cells[index].terrain;
						const int factor = terrain == WorldGen::Terrain::Forest ? 180 : (terrain == WorldGen::Terrain::Grass ? 40 : (habitat == 4 ? 60 : 80));
						const int patch = 60 + int(CellSeed(objectSeed, x / 12, y / 12, layer.offsetZ, 12) % 81);
						int density = std::min(80, std::clamp(spec.doodads.density, 0, 100) * factor * patch * (densityScale[habitat] * (selected?selected->density:100) / 100) / 1000000);
                        int transition=100;
                        if(edgeWidth>0 && terrain==WorldGen::Terrain::Forest)transition=std::min(transition,WorldGen::TransitionDensity(environment.forestEdge[index],edgeWidth,edgeDensity));
                        if(pathWidth>0)transition=std::min(transition,WorldGen::TransitionDensity(environment.path[index],pathWidth,pathDensity));
                        if(slopeThreshold>0 && environment.slope[index]>slopeThreshold)transition=std::min(transition,WorldGen::TransitionDensity(10000-environment.slope[index],10000-slopeThreshold,slopeDensity));
                        density=density*transition/100;
						if (choice % 100 >= uint32_t(density)) {
							continue;
						}
						DoodadBrush* brush = (*palette)[CellSeed(objectSeed, x, y, layer.offsetZ, 13) % palette->size()];
						DoodadSample sample;
						wxString reason;
						const auto anchor = PositionFor(spec, { x, y }, layer.offsetZ);
						if (!SampleDoodad(*brush, scratch, anchor, CellSeed(objectSeed, x, y, layer.offsetZ, 14), CellSeed(objectSeed, x, y, layer.offsetZ, 15), CellSeed(objectSeed, x, y, layer.offsetZ, 16), sample, reason)) {
							++stats.objectBrushRejected[brush->getName()];++stats.skippedDoodads;continue;
						}
						if (!WorldGen::CanPlaceDecoration(grid, { x, y }, sample.footprint)) {
							++stats.skippedDoodads;
							continue;
						}
						PlaceDoodad(sample, scratch, anchor, blocking);
						WorldGen::MarkDecoration(grid, { x, y }, sample.footprint);
						++stats.doodads;
						++stats.objectBrushUsage[brush->getName()];
                        CountDecoration(stats,regionStart,grid.width,x,y);
						if(selected)++stats.objectPatchUsage[std::string(ObjectHabitats[habitat])+"/"+selected->id];
					}
				}
			}
			const char* names[] = { "", "forest/grass", "desert", "snow", "cave", "beach", "highland" };
			for (int i = 1; i <= 6; ++i) {
				if (used[i] && brushes[i].empty() && std::none_of(patches[i].begin(),patches[i].end(),[](const ObjectPatch& p){return !p.brushes.empty();})) {
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
						if (!SelectedNature(spec,plan,x,y,layer.offsetZ)) continue;
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
						if (!spec.world.natureMask.empty() && (!tile || !tile->ground)) {error="Localized nature regeneration requires an existing world, not an empty target.";return false;}
						if (metadata || (spec.world.protectExisting && tile && !tile->empty())) {
							error = wxString::Format("World generation overlaps a protected tile at %d,%d,%d. Choose an empty area%s.", pos.x, pos.y, pos.z, metadata ? " (house, spawn, creature, zone, map flags, unique/action ID or waypoint)" : " or disable 'Protect existing tiles' to replace ordinary tiles");
							return false;
						}
					}
				}
			}
			if (!spec.world.natureMask.empty() && !changes) {error="Nature mask selects no replaceable natural tiles on the chosen floor.";return false;}
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
				for(size_t r=0;r<blueprint.roofMaterials.size();++r) {
					const auto& name=blueprint.roofMaterials[r];if(name.empty())continue;
					auto* brush=g_brushes.getBrush(name);
					if(!brush || !brush->isGround()){error="Missing exact oriented roof ground brush: "+wxString::FromUTF8(name);return false;}
					materials[i][r+5].floor.brush=brush->asGround();
				}
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
			std::vector<std::vector<std::string>> roofRows;
			for(const auto& b:plan.buildings)roofRows.push_back(WorldGen::RotateBlueprint(plan.catalog[b.blueprint],b.rotation).roofRows);
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
						++stats.contextCopies;
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
						if (cell.terrain == WorldGen::Terrain::Void || !SelectedNature(spec,plan,x,y,layer.offsetZ)) {
							continue;
						}
						const auto pos = PositionFor(spec, { x, y }, layer.offsetZ);
						++stats.preparedTiles;
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
						size_t roofSlot=4;
						if((cell.flags & WorldGen::Roof) && building>=0 && !roofRows[building].empty())roofSlot=size_t(WorldGen::BlueprintRoofSlot(roofRows[building][y-plan.buildings[building].origin.y][x-plan.buildings[building].origin.x]));
						const BuildingMaterial* style = owner >= 0 && layer.offsetZ <= 0 ? &buildingMaterials[owner][(cell.flags & WorldGen::Roof) ? roofSlot : size_t(-layer.offsetZ)] : nullptr;
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
						if (cell.terrain == WorldGen::Terrain::Void || !SelectedNature(spec,plan,x,y,layer.offsetZ)) {
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
				if (!spec.world.natureMask.empty() || !blueprint.windowChance || !blueprint.prefabPath.empty()) continue;
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
					if(spec.world.natureMask.empty()) {
						scratch.getTile(upper)->addItem(Item::Create(uint16_t(physicalTravel[upper])));
						scratch.getTile(lower)->addItem(Item::Create(uint16_t(physicalTravel[lower])));
						stats.stairEndpoints += 2;
					}
					continue;
				}
				if(!spec.world.natureMask.empty())continue;
				Teleport* down = newd Teleport(uint16_t(teleportId));
				down->setDestination(lowerArrival);
				scratch.getTile(upper)->addItem(down);
				Teleport* up = newd Teleport(uint16_t(teleportId));
				up->setDestination(upperArrival);
				scratch.getTile(lower)->addItem(up);
			}
			if (!Decorate(spec, plan, scratch, decorativeBlocking, stats, progress, error) || (spec.world.natureMask.empty() && !Furnish(spec, plan, scratch, decorativeBlocking, stats, progress, error))) {
				return false;
			}
			if (!spec.world.natureMask.empty()) {
				// Validate original context retained from the first copy. Untouched tiles
				// were never rebuilt, re-bordered or re-furnished in this transaction.
				for(const auto& layer:plan.layers)for(int y=0;y<spec.region.height;++y)for(int x=0;x<spec.region.width;++x) {
					if(x==0 && (y&15)==0 && !Update(progress,"Preserving unselected tiles",82,error))return false;
					if(layer.cells[size_t(y)*spec.region.width+x].terrain==WorldGen::Terrain::Void || SelectedNature(spec,plan,x,y,layer.offsetZ))continue;
					const auto pos=PositionFor(spec,{x,y},layer.offsetZ);const auto* copy=scratch.getTile(pos);
					if(!copy || !copy->ground) {error="Localized regeneration requires the complete original world at the same origin and floors.";return false;}
					decorativeBlocking.erase(pos);
					const auto& cell=layer.cells[size_t(y)*spec.region.width+x];
					const bool ordinaryInterior=cell.terrain==WorldGen::Terrain::BuildingFloor && !(cell.flags & (WorldGen::Wall | WorldGen::Entrance | WorldGen::Connection));
					if(WorldGen::IsWalkable(cell) && (!(cell.flags & WorldGen::Reserved) || ordinaryInterior) && !TileWalkable(copy))decorativeBlocking.insert(pos);
				}
				stats.furniture=stats.roofTiles=stats.windows=stats.stairEndpoints=stats.prefabTiles=stats.prefabBuildings=0;
			}
			if(!plan.settings.physicalStairs)for(const auto& link:plan.links) {
				for(const auto& edge:{std::pair{PositionFor(spec,link.upper,link.upperOffsetZ),PositionFor(spec,link.lowerArrival,link.lowerOffsetZ)},std::pair{PositionFor(spec,link.lower,link.lowerOffsetZ),PositionFor(spec,link.upperArrival,link.upperOffsetZ)}}) {
					const auto* tile=scratch.getTile(edge.first);bool matching=false;
					if(tile)for(const auto* item:tile->items)if(const auto* teleport=dynamic_cast<const Teleport*>(item))matching |= teleport->getDestination()==edge.second;
					if(!matching){error="A preserved or generated portal differs from the planned safe destination.";return false;}
				}
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
		resolved.world.natureMask.clear();
		if(!spec.world.natureMaskPath.empty()) {
			wxImage mask;
			if(!WorldGen::LoadWorldPNG(spec.world.natureMaskPath,mask,error) || !WorldGen::DecodeNatureMask(mask,settings.width,settings.height,resolved.world.natureMask,error))return false;
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
		if (!resolved.world.natureMaskPath.empty() && resolved.world.natureMask.empty()) {
			error = "Nature mask has not been resolved for this preview. Rebuild before applying.";
			return false;
		}
		if (!resolved.world.natureMask.empty() && (resolved.world.natureMask.size()!=size_t(plan.settings.width)*plan.settings.height || !plan.FindLayer(resolved.world.natureFloor))) {error="Nature mask dimensions or selected floor do not match the plan.";return false;}
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
					if (!SelectedNature(resolved,plan,x,y,layer.offsetZ)) continue;
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

	size_t ApplicationTileCount(const GenerationSpec& spec,const WorldGen::Plan& plan) {
		if(!spec.world.natureMask.empty() && spec.world.natureMask.size()!=size_t(plan.settings.width)*plan.settings.height)return 0;
		size_t count=0;
		for(const auto& layer:plan.layers){if(!spec.world.natureMask.empty() && layer.offsetZ!=spec.world.natureFloor)continue;for(int y=0;y<plan.settings.height;++y)for(int x=0;x<plan.settings.width;++x)if(layer.cells[size_t(y)*plan.settings.width+x].terrain!=WorldGen::Terrain::Void && SelectedNature(spec,plan,x,y,layer.offsetZ))++count;}
		return count;
	}

	wxImage Render(const WorldGen::Plan& plan, int floor, PreviewLayer view,const GenerationSpec* scope) {
		const auto* layer = plan.FindLayer(floor);
		if (!layer) {
			return wxImage();
		}
		wxImage image(plan.settings.width, plan.settings.height);
		if (!image.IsOk()) return wxImage();
		image.InitAlpha();
		if (!image.HasAlpha()) return wxImage();
        WorldGen::DecorationEnvironment environment;
        if(view==PreviewLayer::PathDistance && !WorldGen::BuildDecorationEnvironment(*layer,plan.settings.width,plan.settings.height,environment))return wxImage();
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
				} else if(view==PreviewLayer::PathDistance) {
                    if(WorldGen::IsWalkable(cell)){const int distance=environment.path[size_t(y)*plan.settings.width+x];
                    color=distance>32?0xC050A0:(uint32_t(distance*7)<<16)|(uint32_t(220-distance*5)<<8)|40;}
                } else if (view == PreviewLayer::ConstructionRoles) {
					color = ((color >> 2) & 0x3F3F3F) + 0x202020;
				} else {
					const int value = view == PreviewLayer::Elevation ? cell.elevation : (view == PreviewLayer::Moisture ? cell.moisture : cell.temperature);
					const uint32_t amount = uint32_t(value) * 255 / 10000;
					color = view == PreviewLayer::Moisture ? (amount << 8) | 0xFF : (view == PreviewLayer::Temperature ? (amount << 16) | ((255 - amount) << 8) : amount * 0x010101);
				}
				if(scope && scope->world.natureMask.size()==layer->cells.size() && !SelectedNature(*scope,plan,x,y,floor)) color=((color>>16)/4<<16)|(((color>>8)&255)/4<<8)|((color&255)/4);
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

    struct PreparedExisting {
        Editor* owner=nullptr;
        GenerationSpec spec;
        GenerationRegion editRegion;
        ExistingOptions options;
        WorldExisting::Analysis analysis;
        ApplicationStats stats;
        BaseMap scratch;
        WorldGen::Plan semantic;
        std::vector<Position> changes;
        std::vector<uint8_t> roadChanges;
        std::map<int,size_t> unknown;
        uint64_t baseline=0,client=0,profileDigest=0,objectsDigest=0;
        wxString objectsPath;
        size_t roads=0;
        uint32_t requestedObjectSeed=0;
    };

    namespace {
        struct ExistingBinding {WorldGen::Terrain terrain=WorldGen::Terrain::Void;bool highland=false;};
        struct ExistingProfile {
            std::map<int,ExistingBinding> exact;
            std::map<Brush*,ExistingBinding> brushes;
            GroundBrush* road=nullptr;
            std::set<Brush*> forestObjects;
            wxString objects;
        };
        void HashExisting(uint64_t& hash,uint64_t value) {for(int b=0;b<8;++b){hash^=uint8_t(value>>(b*8));hash*=1099511628211ull;}}
        uint64_t ExistingFileDigest(const wxString& path) {
            if(path.empty())return 0;
            wxFile file(path);if(!file.IsOpened())return 0;const auto length=file.Length();if(length<0 || length>1024*1024)return 0;
            std::vector<uint8_t> bytes(size_t(length),uint8_t(0));
            if(file.Read(bytes.data(),bytes.size())!=ssize_t(bytes.size()))return 0;
            uint64_t hash=1469598103934665603ull;for(auto byte:bytes){hash^=byte;hash*=1099511628211ull;}return hash;
        }
        uint64_t ExistingClientDigest() {
            uint64_t hash=1469598103934665603ull;
            HashExisting(hash,g_items.MajorVersion);HashExisting(hash,g_items.MinorVersion);HashExisting(hash,g_items.BuildNumber);
            // Include editor/runtime definitions beyond the OTB file's max ID.
            for(unsigned id=1;id<=65535;++id)if(g_items.typeExists(id)) {
                const auto& type=g_items[id];HashExisting(hash,id);HashExisting(hash,type.clientID);
                HashExisting(hash,type.unpassable);HashExisting(hash,type.isGroundTile());HashExisting(hash,type.isFloorChange());HashExisting(hash,type.isTeleport());HashExisting(hash,type.ground_equivalent);
                HashExisting(hash,type.isWall);HashExisting(hash,type.isBorder);HashExisting(hash,type.isDoor());HashExisting(hash,type.isContainer());HashExisting(hash,type.isDepot());HashExisting(hash,type.isMetaItem());HashExisting(hash,type.brush?type.brush->getID():0);
                HashExisting(hash,type.sprite?type.sprite->width:0);HashExisting(hash,type.sprite?type.sprite->height:0);
            }
            return hash;
        }
        uint64_t ExistingMapDigest(Editor& editor,const GenerationRegion& region) {
            uint64_t hash=1469598103934665603ull;
            HashExisting(hash,editor.map.getWidth());HashExisting(hash,editor.map.getHeight());
            IOMapOTBM codec(editor.map.getVersion());MemoryNodeFileWriteHandle bytes;
            for(int y=0;y<region.height;++y)for(int x=0;x<region.width;++x) {
                const Position pos(region.originX+x,region.originY+y,region.z);const auto* tile=editor.map.getTile(pos);const auto* location=editor.map.getTileL(pos);
                HashExisting(hash,tile?1:0);
                if(location){HashExisting(hash,location->getWaypointCount());HashExisting(hash,location->getTownCount());HashExisting(hash,location->getSpawnCount());}
                if(!tile)continue;
                HashExisting(hash,tile->house_id);HashExisting(hash,tile->getMapFlags());HashExisting(hash,tile->isHouseExit());HashExisting(hash,reinterpret_cast<uintptr_t>(tile->spawn));HashExisting(hash,reinterpret_cast<uintptr_t>(tile->creature));
                std::vector<uint16_t> zones(tile->getZoneIds().begin(),tile->getZoneIds().end());std::sort(zones.begin(),zones.end());HashExisting(hash,zones.size());for(auto zone:zones)HashExisting(hash,zone);
                bytes.reset();bytes.addNode(0);
                if(tile->ground)tile->ground->serializeItemNode_OTBM(codec,bytes);
                for(const auto* item:tile->items)item->serializeItemNode_OTBM(codec,bytes);
                bytes.endNode();for(size_t n=0;n<bytes.getSize();++n){hash^=bytes.getMemory()[n];hash*=1099511628211ull;}
            }
            // Town order governs regional scene bindings and is part of the preview.
            for(const auto& entry:editor.map.towns){const auto p=entry.second->getTemplePosition();HashExisting(hash,entry.first);HashExisting(hash,p.x);HashExisting(hash,p.y);HashExisting(hash,p.z);}
            return hash;
        }
        bool ReadExistingProfile(const wxString& path,ExistingProfile& profile,wxString& error) {
            json::mValue root;std::string problem;
            if(!ProceduralJSON::ReadFile(nstr(path),1024*1024,root,problem) || root.type()!=json::obj_type){error="Invalid existing-map profile: "+wxstr(problem);return false;}
            const auto& object=root.get_obj();const auto version=object.find("version"),grounds=object.find("grounds");
            if(version==object.end() || version->second.type()!=json::int_type || version->second.get_int64()!=1 || grounds==object.end() || grounds->second.type()!=json::array_type || grounds->second.get_array().empty() || grounds->second.get_array().size()>256){error="Existing-map profile needs version 1 and 1..256 ground mappings.";return false;}
            for(const auto& value:grounds->second.get_array()) {
                if(value.type()!=json::obj_type){error="Existing ground mapping must be an object.";return false;}
                const auto& fields=value.get_obj();const auto terrain=fields.find("terrain"),id=fields.find("id"),brush=fields.find("brush");
                if(terrain==fields.end() || terrain->second.type()!=json::str_type || (id==fields.end())==(brush==fields.end())){error="Existing ground mapping needs terrain and exactly one of id or brush.";return false;}
                WorldGen::Terrain category=WorldGen::Terrain::Void;
                for(int t=0;t<int(WorldGen::Terrain::Void);++t)if(terrain->second.get_str()==WorldGen::TerrainName(WorldGen::Terrain(t)))category=WorldGen::Terrain(t);
                if(category==WorldGen::Terrain::Void){error="Unknown existing-map terrain category.";return false;}
                ExistingBinding binding{category,false};const auto habitat=fields.find("habitat");
                if(habitat!=fields.end()){if(habitat->second.type()!=json::str_type || habitat->second.get_str()!="highland" || !WorldExisting::Natural(category)){error="Existing habitat override must be highland on natural ground.";return false;}binding.highland=true;}
                if(id!=fields.end()) {
                    if(id->second.type()!=json::int_type || id->second.get_int64()<1 || id->second.get_int64()>65535 || !g_items.typeExists(int(id->second.get_int64())) || !g_items[int(id->second.get_int64())].isGroundTile() || !profile.exact.emplace(int(id->second.get_int64()),binding).second){error="Existing ground IDs must be unique active-client grounds.";return false;}
                } else {
                    if(brush->second.type()!=json::str_type){error="Existing ground brush must be an exact name.";return false;}
                    auto* resolved=g_brushes.getBrush(brush->second.get_str());
                    if(!resolved || !resolved->isGround() || !profile.brushes.emplace(resolved,binding).second){error="Missing or duplicate exact existing ground brush: "+wxstr(brush->second.get_str());return false;}
                }
            }
            const auto road=object.find("roadBrush");
            if(road!=object.end()) {
                if(road->second.type()!=json::str_type){error="roadBrush must be an exact ground name.";return false;}
                auto* brush=g_brushes.getBrush(road->second.get_str());
                if(!brush || !brush->isGround()){error="Missing existing-map road ground brush.";return false;}profile.road=brush->asGround();
            }
            const auto forest=object.find("forestObjects");
            if(forest!=object.end()) {
                if(forest->second.type()!=json::array_type || forest->second.get_array().size()>32){error="forestObjects must contain at most 32 exact doodad brushes.";return false;}
                for(const auto& value:forest->second.get_array()) {
                    if(value.type()!=json::str_type){error="forestObjects must contain exact brush names.";return false;}
                    auto* brush=g_brushes.getBrush(value.get_str());if(!brush || !brush->isDoodad() || !profile.forestObjects.insert(brush).second){error="Missing or duplicate forest evidence brush.";return false;}
                }
            }
            const auto props=object.find("objectProfile");
            if(props!=object.end()) {
                if(props->second.type()!=json::str_type || props->second.get_str().empty()){error="objectProfile must be a nonempty relative or absolute path.";return false;}
                wxFileName filename(wxstr(props->second.get_str()));if(filename.IsRelative())filename.MakeAbsolute(wxFileName(path).GetPath());profile.objects=filename.GetFullPath();
            }
            return true;
        }
        WorldGen::Terrain ExistingTerrain(const Tile* tile,const ExistingProfile& profile) {
            if(!tile || !tile->ground)return WorldGen::Terrain::Void;
            const auto exact=profile.exact.find(tile->ground->getID());if(exact!=profile.exact.end())return exact->second.terrain;
            const auto& type=g_items[tile->ground->getID()];const auto brush=profile.brushes.find(type.brush);
            if(brush!=profile.brushes.end())return brush->second.terrain;
            return type.isFloorChange() || type.isTeleport()?WorldGen::Terrain::BuildingFloor:WorldGen::Terrain::Void;
        }
        bool ExistingProtected(Editor& editor,const Position& pos,const Tile* tile) {
            const auto* location=editor.map.getTileL(pos);
            if(location && (location->getWaypointCount() || location->getTownCount() || location->getSpawnCount()))return true;
            if(!tile || !tile->ground || tile->house_id || tile->spawn || tile->creature || tile->getMapFlags() || !tile->getZoneIds().empty() || tile->isHouseExit())return true;
            auto unsafe=[](const Item* item){if(!item)return false;const auto& t=g_items[item->getID()];return item->isComplex() || t.isMetaItem() || t.isFloorChange() || t.isTeleport() || t.isDoor() || t.isContainer() || t.isDepot() || t.isWall;};
            if(unsafe(tile->ground))return true;
            // Every authored object is retained, including decorative or scripted borders.
            for(const auto* item:tile->items)if(!item->isBorder() || unsafe(item))return true;
            return false;
        }
    }

    const WorldExisting::Analysis& ExistingPreview::analysis() const {return prepared->analysis;}
    const ApplicationStats& ExistingPreview::stats() const {return prepared->stats;}
    const GenerationRegion& ExistingPreview::region() const {return prepared->spec.region;}
    uint64_t ExistingPreview::fingerprint() const {return prepared->baseline;}
    size_t ExistingPreview::changedTiles() const {return prepared->changes.size();}
    size_t ExistingPreview::roadTiles() const {return prepared->roads;}
    const std::map<int,size_t>& ExistingPreview::unknownGrounds() const {return prepared->unknown;}

    bool PrepareExisting(Editor& editor,const ExistingOptions& options,ExistingPreview& preview,wxString& error,const WorldGen::ProgressCallback& progress) {
        error.clear();
        if(editor.IsLiveClient()){error="Analyze and improve maps locally, outside live editing sessions.";return false;}
        auto next=std::make_shared<PreparedExisting>();next->owner=&editor;next->spec=options.spec;next->options=options;
        auto& spec=next->spec;spec.elevation.maxLevels=0;
        if(!ProceduralCommon::ResolveRegion(editor,spec,error))return false;
        next->editRegion=spec.region;
        const auto original=spec.region;
        if(original.originX<0 || original.originY<0 || int64_t(original.originX)+original.width>editor.map.getWidth() || int64_t(original.originY)+original.height>editor.map.getHeight()){error="The requested existing-map region is outside the map.";return false;}
        constexpr int halo=32;
        spec.region.originX=std::max(0,original.originX-halo);spec.region.originY=std::max(0,original.originY-halo);
        spec.region.width=std::min(editor.map.getWidth(),original.originX+original.width+halo)-spec.region.originX;
        spec.region.height=std::min(editor.map.getHeight(),original.originY+original.height+halo)-spec.region.originY;
        const auto& r=spec.region;
        if(original.width<1 || original.height<1 || original.originX<0 || original.originY<0 || int64_t(original.originX)+original.width>editor.map.getWidth() || int64_t(original.originY)+original.height>editor.map.getHeight() || r.width<1 || r.height<1 || int64_t(r.width)*r.height>DefaultTileBudget || r.originX<0 || r.originY<0 || r.z<0 || r.z>MAP_MAX_LAYER || int64_t(r.originX)+r.width>editor.map.getWidth() || int64_t(r.originY)+r.height>editor.map.getHeight()){error="Existing-map analysis requires an in-bounds region of at most 1,048,576 cells on one floor.";return false;}
        if(!Update(progress,"Reading existing-map materials",0,error))return false;
        ExistingProfile profile;
        wxFileName profileFile(options.profilePath.empty()?g_gui.GetProceduralDataDirectory()+"profiles/1098/existing_map.json":options.profilePath);
        if(profileFile.IsRelative())profileFile.MakeAbsolute();const auto profilePath=profileFile.GetFullPath();
        next->options.profilePath=profilePath;
        next->profileDigest=ExistingFileDigest(profilePath);
        if(!next->profileDigest){error="Could not read the existing-map analysis profile.";return false;}
        if(!ReadExistingProfile(profilePath,profile,error))return false;
        if(next->profileDigest!=ExistingFileDigest(profilePath)){error="Analysis profile changed while loading.";return false;}next->objectsPath=profile.objects;
        if(!spec.world.objectProfilePath.empty())next->objectsPath=spec.world.objectProfilePath;
        if(options.addNature && next->objectsPath.empty()){error="Choose an object profile before enriching an existing map.";return false;}
        spec.world.objectProfilePath=next->objectsPath;
        next->objectsDigest=options.addNature?ExistingFileDigest(next->objectsPath):0;
        if(options.addNature && !next->objectsDigest){error="Could not read the existing-map object profile.";return false;}
        next->baseline=ExistingMapDigest(editor,r);next->client=ExistingClientDigest();
        WorldExisting::Settings settings{r.width,r.height,options.protectionRadius,options.maxGap,options.maxConnections,options.connectPaths};
        std::vector<WorldExisting::Cell> cells(size_t(r.width)*r.height);
        std::set<Position> selected;
        if(options.spec.useSelection)for(auto* tile:editor.selection.getTiles())selected.insert(tile->getPosition());
        for(int y=0;y<r.height;++y) {
            if((y&31)==0 && !Update(progress,"Reading existing terrain and protected objects",10,error))return false;
            for(int x=0;x<r.width;++x) {
                const Position pos(r.originX+x,r.originY+y,r.z);const auto* tile=editor.map.getTile(pos);auto& cell=cells[size_t(y)*r.width+x];
                cell.exists=tile && tile->ground;cell.bare=tile && tile->items.empty();cell.terrain=ExistingTerrain(tile,profile);cell.walkable=TileWalkable(tile);
                if(cell.exists){const auto exact=profile.exact.find(tile->ground->getID());const auto brush=profile.brushes.find(g_items[tile->ground->getID()].brush);cell.highland=exact!=profile.exact.end()?exact->second.highland:brush!=profile.brushes.end() && brush->second.highland;}
                if(tile){auto travel=[](const Item* item){return item && (g_items[item->getID()].isFloorChange() || g_items[item->getID()].isTeleport());};cell.travel=travel(tile->ground) || std::any_of(tile->items.begin(),tile->items.end(),travel);}
                cell.editable=cell.exists && cell.terrain!=WorldGen::Terrain::Void && !ExistingProtected(editor,pos,tile) && pos.x>=original.originX && pos.y>=original.originY && pos.x<original.originX+original.width && pos.y<original.originY+original.height && (!options.spec.useSelection || selected.count(pos));
                cell.structure=cell.exists && (cell.terrain==WorldGen::Terrain::BuildingFloor || cell.terrain==WorldGen::Terrain::Bridge || tile->hasWall() || !cell.editable);
                if(cell.exists && cell.terrain==WorldGen::Terrain::Void)++next->unknown[tile->ground->getID()];
                // Existing border stacks are retained during enrichment, but a road
                // ground replacement is allowed only on truly bare ground (below).
            }
        }
        // Existing composite and large sprites occupy more than their anchor.
        // The inspected halo makes off-selection anchors visible to this guard.
        for(int y=0;y<r.height;++y)for(int x=0;x<r.width;++x) {
            const auto* tile=editor.map.getTile(PositionFor(spec,{x,y},0));if(!tile)continue;
            auto protectFootprint=[&](const Item* item){if(!item || !g_items[item->getID()].sprite)return;const auto* sprite=g_items[item->getID()].sprite;const int width=std::clamp(int(sprite->width),1,32),height=std::clamp(int(sprite->height),1,32);if(width==1 && height==1)return;for(int dy=0;dy<height;++dy)for(int dx=0;dx<width;++dx){const int nx=x-dx,ny=y-dy;if(nx>=0 && ny>=0){auto& c=cells[size_t(ny)*r.width+nx];c.editable=false;c.structure=true;}}};
            protectFootprint(tile->ground);for(const auto* item:tile->items)protectFootprint(item);
        }
        // Tree families are explicit client bindings, not name guessing. Their
        // neighborhood estimates forest context without changing any ground.
        for(int y=0;y<r.height;++y)for(int x=0;x<r.width;++x) {
            const auto* tile=editor.map.getTile(PositionFor(spec,{x,y},0));if(!tile)continue;
            bool forest=false;for(const auto* item:tile->items)forest|=profile.forestObjects.count(g_items[item->getID()].doodad_brush)!=0;
            if(!forest)continue;
            for(int dy=-6;dy<=6;++dy)for(int dx=-6;dx<=6;++dx)if(std::abs(dx)+std::abs(dy)<=6){const int nx=x+dx,ny=y+dy;if(nx>=0 && ny>=0 && nx<r.width && ny<r.height && cells[size_t(ny)*r.width+nx].terrain==WorldGen::Terrain::Grass)cells[size_t(ny)*r.width+nx].terrain=WorldGen::Terrain::Forest;}
        }
        std::string problem;
        if(!WorldExisting::Analyze(settings,cells,next->analysis,problem,progress)){error=wxstr(problem);return false;}
        auto& plan=next->semantic;plan.settings.width=r.width;plan.settings.height=r.height;plan.settings.seed=spec.seed;plan.settings.settlementCount=0;plan.layers.push_back({0,{}});auto& layer=plan.layers.front();layer.cells.resize(cells.size());
        for(size_t i=0;i<cells.size();++i) {
            auto& cell=layer.cells[i];cell.terrain=cells[i].terrain;cell.flags=next->analysis.eligible[i]?WorldGen::None:WorldGen::Reserved;if(cells[i].highland)cell.flags|=WorldGen::Highland;if(cells[i].travel)cell.flags|=WorldGen::Connection;
            cell.elevation=5000;cell.moisture=5000;cell.temperature=cell.terrain==WorldGen::Terrain::Snow?1500:cell.terrain==WorldGen::Terrain::Desert?8500:5000;
        }
        for(const auto& town:editor.map.towns) {
            const auto p=town.second->getTemplePosition();
            if(p.z==r.z && plan.settlements.size()<32)plan.settlements.push_back({{p.x-r.originX,p.y-r.originY},0,WorldGen::Terrain::Grass,{}});
        }
        // Only the analyzed floor is staged. All original tiles and their item
        // attributes are copied; enrichment adds items rather than rebuilding them.
        for(int y=0;y<r.height;++y) {
            if((y&31)==0 && !Update(progress,"Staging existing-map improvements",55,error))return false;
            for(int x=0;x<r.width;++x) {
                const Position pos(r.originX+x,r.originY+y,r.z);
                if(auto* tile=editor.map.getTile(pos))next->scratch.setTile(pos,tile->deepCopy(next->scratch),true);
            }
        }
        next->roadChanges.assign(cells.size(),0);
        if(options.connectPaths && !next->analysis.connections.empty() && !profile.road){error="The analysis profile needs roadBrush to improve path gaps.";return false;}
        std::vector<WorldExisting::Connection> accepted;
        for(const auto& connection:next->analysis.connections) {
            bool valid=true;
            for(const auto p:connection.cells){const auto* tile=editor.map.getTile(PositionFor(spec,p,0));if(!tile || !tile->items.empty())valid=false;}
            if(!valid)continue;
            for(const auto p:connection.cells) {
                const auto pos=PositionFor(spec,p,0);auto* tile=next->scratch.getTile(pos);ScopedMTRandom random(CellSeed(spec.seed,p.x,p.y,r.z,301));profile.road->draw(&next->scratch,tile,nullptr);tile->update();
                if(spec.pipeline.borderizeAfter)GroundBrush::doBorders(&next->scratch,tile);
                tile->update();
                if(!TileWalkable(tile) || !tile->ground || tile->ground->isComplex() || !WorldExisting::Road(ExistingTerrain(tile,profile))){error="The chosen path material must produce recognized walkable road ground.";return false;}
                for(const auto* item:tile->items)if(!item->isBorder() || item->isComplex() || g_items[item->getID()].isGroundTile() || g_items[item->getID()].isFloorChange() || g_items[item->getID()].isTeleport() || g_items[item->getID()].isMetaItem()){error="Path borders must be ordinary nonblocking border items.";return false;}
                const size_t i=size_t(p.y)*r.width+p.x;layer.cells[i].terrain=WorldGen::Terrain::Road;layer.cells[i].flags=WorldGen::Reserved;next->roadChanges[i]=1;
            }
            accepted.push_back(connection);
        }
        next->analysis.connections=std::move(accepted);
        WorldGen::DecorationEnvironment environment;
        if(!WorldGen::BuildDecorationEnvironment(layer,r.width,r.height,environment)){error="Could not estimate existing environment.";return false;}
        for(size_t i=0;i<layer.cells.size();++i){auto& cell=layer.cells[i];const int base=cell.terrain==WorldGen::Terrain::Desert?2000:cell.terrain==WorldGen::Terrain::Forest?6500:cell.terrain==WorldGen::Terrain::Snow?3500:5000;cell.moisture=uint16_t(environment.water[i]<=32?std::max(base,8500-int(environment.water[i])*200):base);}
        next->requestedObjectSeed=spec.world.objectSeed.value_or(spec.seed);
        spec.world.objectSeed=CellSeed(next->requestedObjectSeed,r.originX,r.originY,r.z,302);
        spec.world.natureMask.clear();spec.world.natureMaskPath.clear();spec.doodads.enabled=options.addNature;
        std::set<Position> blocking;
        if(options.addNature && !Decorate(spec,plan,next->scratch,blocking,next->stats,progress,error))return false;
        for(int y=0;y<r.height;++y)for(int x=0;x<r.width;++x) {
            const auto i=size_t(y)*r.width+x;const auto pos=PositionFor(spec,{x,y},0);const auto* original=editor.map.getTile(pos);const auto* staged=next->scratch.getTile(pos);
            if(next->roadChanges[i] || (original && staged && original->items.size()!=staged->items.size())) {
                if(!next->analysis.eligible[i]){error="An improvement attempted to modify a protected cell.";return false;}
                if(g_gui.gfx.client_version) {
                    auto covered=[](const Item* item){return item && g_items[item->getID()].sprite;};
                    if(!covered(staged->ground) || std::any_of(staged->items.begin(),staged->items.end(),[&](const Item* item){return !covered(item);})){error="A proposed existing-map improvement has no sprite metadata in the active client.";return false;}
                }
                next->changes.push_back(pos);next->roads+=next->roadChanges[i]!=0;
            }
        }
        next->stats.changedTiles=next->changes.size();next->stats.preparedTiles=next->changes.size();next->stats.contextCopies=std::count_if(cells.begin(),cells.end(),[](const auto& c){return c.exists;});
        if(!Update(progress,"Existing-map preview ready",90,error))return false;
        if(next->baseline!=ExistingMapDigest(editor,r) || next->client!=ExistingClientDigest() || next->profileDigest!=ExistingFileDigest(profilePath) || (options.addNature && next->objectsDigest!=ExistingFileDigest(next->objectsPath))){error="The map, client or profile changed during analysis. Analyze again.";return false;}
        preview.prepared=std::move(next);return true;
    }

    bool ApplyExisting(Editor& editor,const ExistingPreview& preview,wxString& error,ApplicationStats* stats,const WorldGen::ProgressCallback& progress) {
        error.clear();const auto state=preview.prepared;
        if(!state || state->owner!=&editor || editor.IsLiveClient()){error="Build an existing-map preview for this local editor first.";return false;}
        if(state->changes.empty()){error="This preview has no eligible changes to apply.";return false;}
        if(!Update(progress,"Applying existing-map improvements",95,error))return false;
        if(state->baseline!=ExistingMapDigest(editor,state->spec.region) || state->client!=ExistingClientDigest() || state->profileDigest!=ExistingFileDigest(state->options.profilePath) || (state->options.addNature && state->objectsDigest!=ExistingFileDigest(state->objectsPath))){error="The map, client or profile changed after analysis. Rebuild the preview.";return false;}
        std::unique_ptr<BatchAction> batch(editor.actionQueue->createBatch(ACTION_DRAW));std::unique_ptr<Action> action(editor.actionQueue->createAction(batch.get()));
        for(const auto pos:state->changes) {
            auto* tile=state->scratch.getTile(pos)->deepCopy(editor.map);tile->setLocation(editor.map.createTileL(pos));action->addChange(newd Change(tile));
        }
        batch->addAction(action.release());editor.actionQueue->resetTimer();editor.addBatch(batch.release(),0);editor.actionQueue->resetTimer();
        if(stats)*stats=state->stats;g_gui.RefreshView();return true;
    }

    wxImage RenderExisting(const ExistingPreview& preview,int view) {
        if(!preview.prepared)return wxImage();const auto& s=*preview.prepared;const auto& r=s.spec.region;wxImage image(r.width,r.height);if(!image.IsOk())return image;
        for(int y=0;y<r.height;++y)for(int x=0;x<r.width;++x) {
            const size_t i=size_t(y)*r.width+x;const auto& cell=s.analysis.cells[i];uint32_t color=cell.exists?WorldGen::TerrainColor(cell.terrain):0x15191D;
            if(cell.exists && cell.terrain==WorldGen::Terrain::Void)color=0xB04BD1;if(cell.travel)color=0xFF40FF;
            if(view==2)color=s.analysis.eligible[i]?0x46945B:cell.exists?0x905C48:0x15191D;
            if(view==1)color=((color>>16)/3<<16)|(((color>>8)&255)/3<<8)|((color&255)/3);
            image.SetRGB(x,y,uint8_t(color>>16),uint8_t(color>>8),uint8_t(color));
        }
        if(view==1)for(const auto pos:s.changes){const int x=pos.x-r.originX,y=pos.y-r.originY;const bool road=s.roadChanges[size_t(y)*r.width+x];image.SetRGB(x,y,road?255:60,road?200:230,road?50:220);}
        return image;
    }
    wxString DescribeExisting(const ExistingPreview& preview) {
        if(!preview.prepared)return "No existing-map analysis.";const auto& s=*preview.prepared;const auto& a=s.analysis;
        wxString text=wxString::Format("Existing map z=%d | %d x %d inspected with context | %llu recognized grounds | %llu unknown grounds\n%llu structural/protected cells | %llu eligible natural cells | %d road components\nProposed: %llu changed tiles, %llu path tiles in %llu short connections, %llu object anchors in %llu scenes.\nCyan: added objects; yellow: new path ground; purple: unknown material. Buildings and authored objects are retained. Climate is estimated from ground type and water proximity.",s.spec.region.z,a.settings.width,a.settings.height,(unsigned long long)a.recognized,(unsigned long long)a.unknown,(unsigned long long)a.structures,(unsigned long long)a.naturalTiles,a.roadComponentCount,(unsigned long long)s.changes.size(),(unsigned long long)s.roads,(unsigned long long)a.connections.size(),(unsigned long long)s.stats.doodads,(unsigned long long)std::accumulate(s.stats.sceneUsage.begin(),s.stats.sceneUsage.end(),size_t(0),[](size_t sum,const auto& entry){return sum+entry.second;}));
        if(!s.unknown.empty()){text+="\nUnknown ground IDs (preserved):";size_t shown=0;for(const auto& [id,count]:s.unknown){if(shown++==12){text+=" ...";break;}text+=wxString::Format(" %d (%llu)",id,(unsigned long long)count);}}
        return text;
    }
    bool ExportExisting(const ExistingPreview& preview,const wxString& path,wxString& error) {
        if(!preview.prepared){error="No existing-map analysis to export.";return false;}
        const auto& s=*preview.prepared;json::mObject report;report["schemaVersion"]=1;report["source"]="existing-map";report["fingerprint"]=std::to_string(s.baseline);
        report["description"]=nstr(DescribeExisting(preview));
        report["seed"]=json::mValue(uint64_t(s.spec.seed));report["objectSeed"]=json::mValue(uint64_t(s.requestedObjectSeed));report["effectiveObjectSeed"]=json::mValue(uint64_t(s.spec.world.objectSeed.value_or(s.spec.seed)));
        report["analysisProfile"]=nstr(wxFileName(s.options.profilePath).GetFullName());report["objectProfile"]=nstr(wxFileName(s.objectsPath).GetFullName());
        auto region=[](const GenerationRegion& r){json::mObject o;o["x"]=r.originX;o["y"]=r.originY;o["z"]=r.z;o["width"]=r.width;o["height"]=r.height;return o;};
        report["scope"]=region(s.editRegion);report["inspected"]=region(s.spec.region);
        json::mObject counts,options,terrainCounts,brushes,scenes,variants,rejected;
        counts["recognizedGrounds"]=json::mValue(uint64_t(s.analysis.recognized));counts["unknownGrounds"]=json::mValue(uint64_t(s.analysis.unknown));counts["protectedCells"]=json::mValue(uint64_t(s.analysis.structures));counts["eligibleNature"]=json::mValue(uint64_t(s.analysis.naturalTiles));counts["roadComponents"]=s.analysis.roadComponentCount;counts["travelAnchors"]=json::mValue(uint64_t(s.analysis.travelAnchors));
        counts["pathConnections"]=int(s.analysis.connections.size());counts["pathTiles"]=json::mValue(uint64_t(s.roads));counts["changedTiles"]=json::mValue(uint64_t(s.changes.size()));counts["objectAnchors"]=json::mValue(uint64_t(s.stats.doodads));report["counts"]=counts;
        options["addNature"]=s.options.addNature;options["connectPaths"]=s.options.connectPaths;options["maxGap"]=s.options.maxGap;options["maxConnections"]=s.options.maxConnections;options["protectionRadius"]=s.options.protectionRadius;report["options"]=options;
        std::map<std::string,size_t> terrainUsage;for(const auto& cell:s.analysis.cells)if(cell.exists)++terrainUsage[WorldGen::TerrainName(cell.terrain)];for(const auto& [name,count]:terrainUsage)terrainCounts[name]=json::mValue(uint64_t(count));report["terrainCounts"]=terrainCounts;
        for(const auto& [name,count]:s.stats.objectBrushUsage)brushes[name]=json::mValue(uint64_t(count));for(const auto& [name,count]:s.stats.sceneUsage)scenes[name]=json::mValue(uint64_t(count));for(const auto& [name,count]:s.stats.sceneVariantUsage)variants[name]=json::mValue(uint64_t(count));for(const auto& [name,count]:s.stats.objectBrushRejected)rejected[name]=json::mValue(uint64_t(count));
        report["objectBrushUsage"]=brushes;report["sceneUsage"]=scenes;report["sceneVariantUsage"]=variants;report["objectBrushRejected"]=rejected;
        json::mArray unknown,changes;
        for(const auto& [id,count]:s.unknown){json::mObject entry;entry["id"]=id;entry["tiles"]=json::mValue(uint64_t(count));unknown.push_back(entry);}
        for(const auto pos:s.changes){json::mObject entry;json::mArray point;point.push_back(pos.x);point.push_back(pos.y);point.push_back(pos.z);entry["position"]=point;entry["kind"]=s.roadChanges[size_t(pos.y-s.spec.region.originY)*s.spec.region.width+pos.x-s.spec.region.originX]?"path":"objects";changes.push_back(entry);}
        report["unknownGrounds"]=unknown;report["changes"]=changes;
        const auto bytes=json::write(json::mValue(report));wxFile file(path,wxFile::write);if(!file.IsOpened() || file.Write(bytes.data(),bytes.size())!=bytes.size()){error="Could not export existing-map report.";return false;}return true;
    }

}
