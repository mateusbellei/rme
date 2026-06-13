//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "procedural_common.h"
#include "procedural_generator.h"
#include "brush.h"
#include "ground_brush.h"
#include "wall_brush.h"
#include "doodad_brush.h"
#include "map.h"
#include "action.h"
#include "editor.h"
#include "gui.h"
#include "settings.h"
#include "json.h"
#include "mt_rand.h"

#include <algorithm>
#include <fstream>
#include <cctype>

namespace {
	static bool ParseHexColor(const std::string& text, uint8_t& r, uint8_t& g, uint8_t& b) {
		if (text.size() != 7 || text[0] != '#') {
			return false;
		}
		auto hex = [](char c) -> int {
			if (c >= '0' && c <= '9') {
				return c - '0';
			}
			if (c >= 'a' && c <= 'f') {
				return c - 'a' + 10;
			}
			if (c >= 'A' && c <= 'F') {
				return c - 'A' + 10;
			}
			return -1;
		};
		int rh = hex(text[1]);
		int rl = hex(text[2]);
		int gh = hex(text[3]);
		int gl = hex(text[4]);
		int bh = hex(text[5]);
		int bl = hex(text[6]);
		if (rh < 0 || rl < 0 || gh < 0 || gl < 0 || bh < 0 || bl < 0) {
			return false;
		}
		r = static_cast<uint8_t>((rh << 4) | rl);
		g = static_cast<uint8_t>((gh << 4) | gl);
		b = static_cast<uint8_t>((bh << 4) | bl);
		return true;
	}

	static bool IsWallGround(GroundBrush* brush) {
		if (!brush) {
			return false;
		}
		const std::string name = as_lower_str(brush->getName());
		return name.find("stone") != std::string::npos || name.find("mountain") != std::string::npos || name.find("cave") != std::string::npos || name.find("wall") != std::string::npos;
	}

	static bool IsFloorGround(GroundBrush* brush) {
		if (!brush) {
			return false;
		}
		const std::string name = as_lower_str(brush->getName());
		if (IsWallGround(brush)) {
			return false;
		}
		return name.find("cobble") != std::string::npos || name.find("grass") != std::string::npos || name.find("sand") != std::string::npos || name.find("earth") != std::string::npos || name.find("dirt") != std::string::npos;
	}

	static bool IsSnowGround(GroundBrush* brush) {
		if (!brush) {
			return false;
		}
		const std::string name = as_lower_str(brush->getName());
		return name.find("snow") != std::string::npos || name.find("ice") != std::string::npos || name.find("frost") != std::string::npos || name.find("winter") != std::string::npos;
	}
}

bool ProceduralCommon::ResolveRegion(Editor& editor, GenerationSpec& spec, wxString& error) {
	if (spec.useSelection) {
		if (!editor.hasSelection()) {
			error = "No map selection. Select an area first or disable 'Use selection'.";
			return false;
		}

		const Position minPos = editor.selection.minPosition();
		const Position maxPos = editor.selection.maxPosition();
		if (minPos.z != maxPos.z) {
			error = "Selection spans multiple floors. Use a single-floor selection.";
			return false;
		}

		spec.region.originX = minPos.x;
		spec.region.originY = minPos.y;
		spec.region.z = minPos.z;
		spec.region.width = maxPos.x - minPos.x + 1;
		spec.region.height = maxPos.y - minPos.y + 1;
	}

	if (spec.region.width <= 0 || spec.region.height <= 0 || spec.region.z < 0 || spec.region.z > MAP_MAX_LAYER) {
		error = "Invalid target size.";
		return false;
	}

	if (spec.region.width > 65000 || spec.region.height > 65000) {
		error = "Target area is too large.";
		return false;
	}

	if (spec.elevation.maxLevels > 0 && spec.region.z < spec.elevation.maxLevels) {
		error = wxString::Format(
			"Base floor z=%d is too low for %d elevation levels. Use z >= %d (ground is usually z=7).",
			spec.region.z,
			spec.elevation.maxLevels,
			spec.elevation.maxLevels
		);
		return false;
	}

	return true;
}

GroundBrush* ProceduralCommon::FindGroundBrush(const std::string& targetLower) {
	for (const auto& kv : g_brushes.getMap()) {
		Brush* brush = kv.second;
		if (brush && brush->isGround()) {
			const std::string name = as_lower_str(brush->getName());
			if (name.find(targetLower) != std::string::npos) {
				return brush->asGround();
			}
		}
	}
	return nullptr;
}

WallBrush* ProceduralCommon::FindWallBrush(const std::string& targetLower) {
	for (const auto& kv : g_brushes.getMap()) {
		Brush* brush = kv.second;
		if (brush && brush->isWall()) {
			const std::string name = as_lower_str(brush->getName());
			if (name.find(targetLower) != std::string::npos) {
				return brush->asWall();
			}
		}
	}
	return nullptr;
}

GroundBrush* ProceduralCommon::PickDefaultLandBrush() {
	GroundBrush* brush = FindGroundBrush("grass");
	if (!brush) {
		brush = FindGroundBrush("sand");
	}
	if (!brush) {
		for (const auto& kv : g_brushes.getMap()) {
			if (kv.second && kv.second->isGround()) {
				return kv.second->asGround();
			}
		}
	}
	return brush;
}

GroundBrush* ProceduralCommon::PickDefaultWaterBrush() {
	GroundBrush* brush = FindGroundBrush("water");
	if (!brush) {
		brush = FindGroundBrush("ocean");
	}
	if (!brush) {
		brush = PickDefaultLandBrush();
	}
	return brush;
}

DoodadBrush* ProceduralCommon::FindDoodadBrush(const std::string& targetLower) {
	for (const auto& kv : g_brushes.getMap()) {
		Brush* brush = kv.second;
		if (brush && brush->isDoodad()) {
			const std::string name = as_lower_str(brush->getName());
			if (name.find(targetLower) != std::string::npos) {
				return brush->asDoodad();
			}
		}
	}
	return nullptr;
}

std::vector<DoodadBrush*> ProceduralCommon::FindDoodadBrushes(const std::vector<std::string>& keywords) {
	std::vector<DoodadBrush*> result;
	for (const auto& kv : g_brushes.getMap()) {
		Brush* brush = kv.second;
		if (!brush || !brush->isDoodad()) {
			continue;
		}
		const std::string name = as_lower_str(brush->getName());
		for (const std::string& keyword : keywords) {
			if (name.find(keyword) != std::string::npos) {
				result.push_back(brush->asDoodad());
				break;
			}
		}
	}
	return result;
}

GroundBrush* ProceduralCommon::PickSnowBrush() {
	GroundBrush* brush = FindGroundBrush("snow");
	if (!brush) {
		brush = FindGroundBrush("winter");
	}
	if (!brush) {
		brush = FindGroundBrush("frost");
	}
	return brush;
}

GroundBrush* ProceduralCommon::PickIceBrush() {
	GroundBrush* brush = FindGroundBrush("ice");
	if (!brush) {
		brush = PickSnowBrush();
	}
	return brush;
}

GroundBrush* ProceduralCommon::PickMountainBrush() {
	GroundBrush* brush = FindGroundBrush("mountain");
	if (!brush) {
		brush = FindGroundBrush("stone");
	}
	return brush;
}

bool ProceduralCommon::PaintGroundTile(Editor& editor, Action* action, int x, int y, int z, GroundBrush* brush) {
	if (!brush) {
		return false;
	}

	Position pos(x, y, z);
	TileLocation* location = editor.map.createTileL(pos);
	Tile* existing = location->get();
	Tile* newTile = existing ? existing->deepCopy(editor.map) : editor.map.allocator(location);

	if (g_settings.getInteger(Config::USE_AUTOMAGIC)) {
		newTile->cleanBorders();
	}

	if (newTile->ground) {
		delete newTile->ground;
		newTile->ground = nullptr;
	}
	brush->draw(&editor.map, newTile, nullptr);

	if (g_settings.getInteger(Config::USE_AUTOMAGIC)) {
		newTile->borderize(&editor.map);
	}
	action->addChange(newd Change(newTile));
	return true;
}

bool ProceduralCommon::LoadLegend(const wxString& path, LegendMapping& mapping, wxString& error) {
	mapping = LegendMapping();
	if (path.empty()) {
		mapping.defaultBrushName = "grass";
		return true;
	}

	std::ifstream input(nstr(path).c_str());
	if (!input.good()) {
		error = "Failed to open legend JSON: " + path;
		return false;
	}

	json::mValue root;
	if (!json::read(input, root) || root.type() != json::obj_type) {
		error = "Invalid legend JSON: " + path;
		return false;
	}

	const json::mObject& object = root.get_obj();
	json::mObject::const_iterator defaultIt = object.find("default");
	if (defaultIt != object.end() && defaultIt->second.type() == json::str_type) {
		mapping.defaultBrushName = defaultIt->second.get_str();
	} else {
		mapping.defaultBrushName = "grass";
	}

	json::mObject::const_iterator entriesIt = object.find("entries");
	if (entriesIt == object.end() || entriesIt->second.type() != json::array_type) {
		error = "Legend JSON must contain an 'entries' array.";
		return false;
	}

	const json::mArray& entries = entriesIt->second.get_array();
	for (json::mArray::const_iterator it = entries.begin(); it != entries.end(); ++it) {
		if (it->type() != json::obj_type) {
			continue;
		}
		const json::mObject& entry = it->get_obj();
		json::mObject::const_iterator colorIt = entry.find("color");
		json::mObject::const_iterator brushIt = entry.find("brush");
		if (colorIt == entry.end() || brushIt == entry.end()) {
			continue;
		}
		if (colorIt->second.type() != json::str_type || brushIt->second.type() != json::str_type) {
			continue;
		}

		LegendEntry legendEntry;
		if (!ParseHexColor(colorIt->second.get_str(), legendEntry.r, legendEntry.g, legendEntry.b)) {
			continue;
		}
		legendEntry.brushName = brushIt->second.get_str();
		legendEntry.brush = nullptr;
		mapping.entries.push_back(legendEntry);
	}

	return true;
}

void ProceduralCommon::ResolveLegendBrushes(LegendMapping& mapping) {
	mapping.defaultBrush = FindGroundBrush(as_lower_str(mapping.defaultBrushName));
	for (LegendEntry& entry : mapping.entries) {
		if (entry.brushName.empty()) {
			entry.brush = nullptr;
			continue;
		}
		entry.brush = FindGroundBrush(as_lower_str(entry.brushName));
	}
}

bool ProceduralCommon::IsTransparentPixel(uint8_t r, uint8_t g, uint8_t b) {
	return r < 8 && g < 8 && b < 8;
}

GroundBrush* ProceduralCommon::BrushForPixel(uint8_t r, uint8_t g, uint8_t b, const LegendMapping* legend, GroundBrush* landFallback, GroundBrush* waterFallback) {
	if (IsTransparentPixel(r, g, b)) {
		return nullptr;
	}

	if (legend && !legend->entries.empty()) {
		for (const LegendEntry& entry : legend->entries) {
			if (entry.r == r && entry.g == g && entry.b == b) {
				if (entry.brush) {
					return entry.brush;
				}
				if (entry.brushName.empty()) {
					return nullptr;
				}
			}
		}
		if (legend->defaultBrush) {
			return legend->defaultBrush;
		}
	}

	const int brightness = (int(r) + int(g) + int(b)) / 3;
	const bool isWater = (b > r && b > g && b > 100) || (brightness < 35);
	return isWater ? waterFallback : landFallback;
}

bool ProceduralCommon::ApplyColorMask(Editor& editor, const GenerationSpec& spec, const wxImage& mask, const LegendMapping* legend, wxString& error) {
	if (mask.GetWidth() != spec.region.width || mask.GetHeight() != spec.region.height) {
		error = "Mask size does not match target region.";
		return false;
	}

	GroundBrush* landBrush = PickDefaultLandBrush();
	GroundBrush* waterBrush = PickDefaultWaterBrush();
	if (!landBrush) {
		error = "Could not find any ground brushes in current client; load a version first.";
		return false;
	}

	const int totalTiles = spec.region.width * spec.region.height;
	BatchAction* batch = editor.actionQueue->createBatch(ACTION_DRAW);
	Action* action = editor.actionQueue->createAction(batch);

	int processed = 0;
	for (int y = 0; y < spec.region.height; ++y) {
		if (y % 32 == 0) {
			g_gui.SetLoadDone(static_cast<int>(100.0 * processed / totalTiles));
		}
		for (int x = 0; x < spec.region.width; ++x) {
			++processed;
			const uint8_t r = mask.GetRed(x, y);
			const uint8_t g = mask.GetGreen(x, y);
			const uint8_t b = mask.GetBlue(x, y);
			GroundBrush* target = BrushForPixel(r, g, b, legend, landBrush, waterBrush);
			if (!target) {
				continue;
			}

			Position pos(spec.region.originX + x, spec.region.originY + y, spec.region.z);
			TileLocation* location = editor.map.createTileL(pos);
			Tile* existing = location->get();
			Tile* newTile = existing ? existing->deepCopy(editor.map) : editor.map.allocator(location);

			if (g_settings.getInteger(Config::USE_AUTOMAGIC)) {
				newTile->cleanBorders();
			}

			if (newTile->ground) {
				delete newTile->ground;
				newTile->ground = nullptr;
			}
			target->draw(&editor.map, newTile, nullptr);

			if (g_settings.getInteger(Config::USE_AUTOMAGIC)) {
				newTile->borderize(&editor.map);
			}
			action->addChange(newd Change(newTile));
		}
	}

	batch->addAndCommitAction(action);
	editor.addBatch(batch, 2);
	return true;
}

bool ProceduralCommon::ApplyMountainElevation(Editor& editor, const GenerationSpec& spec, const std::vector<int>& heights, wxString& error) {
	if (heights.size() != static_cast<size_t>(spec.region.width * spec.region.height)) {
		error = "Heightmap size does not match target region.";
		return false;
	}

	GroundBrush* mountainBrush = PickMountainBrush();
	GroundBrush* grassBrush = PickDefaultLandBrush();
	GroundBrush* earthBrush = FindGroundBrush("earth");
	if (!mountainBrush || !grassBrush) {
		error = "Could not find mountain/grass ground brushes for elevated terrain.";
		return false;
	}

	const int baseZ = spec.region.z;
	const int maxLevels = std::max(1, spec.elevation.maxLevels);
	BatchAction* batch = editor.actionQueue->createBatch(ACTION_DRAW);
	Action* action = editor.actionQueue->createAction(batch);

	const int totalCells = spec.region.width * spec.region.height;
	int processed = 0;

	for (int y = 0; y < spec.region.height; ++y) {
		if (y % 16 == 0) {
			g_gui.SetLoadDone(static_cast<int>(100.0 * processed / totalCells));
		}
		for (int x = 0; x < spec.region.width; ++x) {
			++processed;
			const int mapX = spec.region.originX + x;
			const int mapY = spec.region.originY + y;
			int height = heights[y * spec.region.width + x];
			height = std::max(0, std::min(height, maxLevels));

			const int surfaceZ = baseZ - height;
			GroundBrush* surfaceBrush = grassBrush;
			if (spec.preset == GenerationPreset::Ice) {
				surfaceBrush = PickSnowBrush();
				if (!surfaceBrush) {
					surfaceBrush = PickIceBrush();
				}
				if (!surfaceBrush) {
					surfaceBrush = grassBrush;
				}
				if (height >= 2) {
					surfaceBrush = mountainBrush;
				}
			} else if (height >= 2) {
				surfaceBrush = mountainBrush;
			} else if (height == 1 && earthBrush) {
				surfaceBrush = earthBrush;
			}

			for (int z = surfaceZ + 1; z <= baseZ; ++z) {
				PaintGroundTile(editor, action, mapX, mapY, z, mountainBrush);
			}
			PaintGroundTile(editor, action, mapX, mapY, surfaceZ, surfaceBrush);
		}
	}

	batch->addAndCommitAction(action);
	editor.addBatch(batch, 2);
	return true;
}

bool ProceduralCommon::ScatterDoodads(Editor& editor, const GenerationSpec& spec, int surfaceFloor, const std::vector<int>* heights, const std::vector<std::string>& keywords, wxString& error) {
	if (!spec.doodads.enabled || spec.doodads.density <= 0) {
		return true;
	}

	std::vector<DoodadBrush*> brushes = FindDoodadBrushes(keywords);
	if (brushes.empty()) {
		return true;
	}

	const bool useHeights = heights && heights->size() == static_cast<size_t>(spec.region.width * spec.region.height);
	mt_seed(spec.seed + 911);
	BatchAction* batch = editor.actionQueue->createBatch(ACTION_DRAW);
	Action* action = editor.actionQueue->createAction(batch);

	for (int y = 0; y < spec.region.height; ++y) {
		for (int x = 0; x < spec.region.width; ++x) {
			if (static_cast<int>(mt_randi() % 100) >= spec.doodads.density) {
				continue;
			}

			const int mapX = spec.region.originX + x;
			const int mapY = spec.region.originY + y;
			int floorZ = surfaceFloor;
			if (useHeights) {
				const int height = (*heights)[y * spec.region.width + x];
				floorZ = spec.region.z - height;
			}

			Position pos(mapX, mapY, floorZ);
			Tile* tile = editor.map.getTile(pos);
			if (!tile || !tile->hasGround()) {
				continue;
			}

			GroundBrush* groundBrush = tile->getGroundBrush();
			const bool allowSnow = IsSnowGround(groundBrush);
			const bool allowGreen = IsFloorGround(groundBrush);
			if (!allowSnow && !allowGreen) {
				continue;
			}
			if (spec.preset == GenerationPreset::Ice && !allowSnow) {
				continue;
			}

			DoodadBrush* doodad = brushes[mt_randi() % brushes.size()];
			Tile* newTile = tile->deepCopy(editor.map);
			doodad->draw(&editor.map, newTile, nullptr);
			action->addChange(newd Change(newTile));
		}
	}

	batch->addAndCommitAction(action);
	editor.addBatch(batch, 2);
	return true;
}

bool ProceduralCommon::PostProcessFloors(Editor& editor, const GenerationSpec& spec, int minZ, int maxZ, wxString& error) {
	if (!spec.pipeline.borderizeAfter && !spec.pipeline.randomizeGround && !spec.pipeline.placeWalls) {
		return true;
	}

	BatchAction* batch = editor.actionQueue->createBatch(ACTION_DRAW);
	Action* action = editor.actionQueue->createAction(batch);

	const int x0 = spec.region.originX;
	const int y0 = spec.region.originY;
	const int x1 = spec.region.originX + spec.region.width - 1;
	const int y1 = spec.region.originY + spec.region.height - 1;

	for (int z = minZ; z <= maxZ; ++z) {
		for (int y = y0; y <= y1; ++y) {
			for (int x = x0; x <= x1; ++x) {
				Position pos(x, y, z);
				Tile* tile = editor.map.getTile(pos);
				if (!tile) {
					continue;
				}

				Tile* newTile = tile->deepCopy(editor.map);

				if (spec.pipeline.randomizeGround) {
					GroundBrush* groundBrush = newTile->getGroundBrush();
					if (groundBrush && groundBrush->isReRandomizable()) {
						groundBrush->draw(&editor.map, newTile, nullptr);
					}
				}

				if (spec.pipeline.borderizeAfter && g_settings.getInteger(Config::USE_AUTOMAGIC)) {
					newTile->borderize(&editor.map);
				}

				action->addChange(newd Change(newTile));
			}
		}
	}

	if (spec.pipeline.placeWalls) {
		WallBrush* wallBrush = FindWallBrush("stone");
		if (!wallBrush) {
			wallBrush = FindWallBrush("mountain");
		}
		if (!wallBrush) {
			wallBrush = FindWallBrush("wall");
		}

		if (wallBrush) {
			Action* wallAction = editor.actionQueue->createAction(batch);
			for (int z = minZ; z <= maxZ; ++z) {
				for (int y = y0; y <= y1; ++y) {
					for (int x = x0; x <= x1; ++x) {
						Position pos(x, y, z);
						Tile* tile = editor.map.getTile(pos);
						if (!tile || !tile->hasGround()) {
							continue;
						}

						GroundBrush* groundBrush = tile->getGroundBrush();
						if (!IsWallGround(groundBrush)) {
							continue;
						}

						bool touchesFloor = false;
						static const int dx[] = {0, 1, 0, -1};
						static const int dy[] = {-1, 0, 1, 0};
						for (int dir = 0; dir < 4; ++dir) {
							Position neighbor(x + dx[dir], y + dy[dir], z);
							if (neighbor.x < x0 || neighbor.x > x1 || neighbor.y < y0 || neighbor.y > y1) {
								touchesFloor = true;
								break;
							}
							Tile* neighborTile = editor.map.getTile(neighbor);
							if (!neighborTile || !neighborTile->hasGround()) {
								touchesFloor = true;
								break;
							}
							if (IsFloorGround(neighborTile->getGroundBrush())) {
								touchesFloor = true;
								break;
							}
						}

						if (!touchesFloor) {
							continue;
						}

						Tile* newTile = tile->deepCopy(editor.map);
						newTile->cleanWalls(true);
						wallBrush->draw(&editor.map, newTile, nullptr);
						if (g_settings.getInteger(Config::USE_AUTOMAGIC)) {
							newTile->wallize(&editor.map);
						}
						wallAction->addChange(newd Change(newTile));
					}
				}
			}
			batch->addAndCommitAction(wallAction);
		}
	}

	batch->addAndCommitAction(action);
	editor.addBatch(batch, 2);
	return true;
}

bool ProceduralCommon::PostProcess(Editor& editor, const GenerationSpec& spec, wxString& error) {
	return PostProcessFloors(editor, spec, spec.region.z, spec.region.z, error);
}
