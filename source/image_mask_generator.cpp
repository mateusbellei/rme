//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "image_mask_generator.h"
#include "procedural_generator.h"
#include "brush.h"
#include "ground_brush.h"
#include "map.h"
#include "action.h"
#include "editor.h"
#include "materials.h"
#include "editor.h"
#include "gui.h"

namespace {
	static inline wxString ColorToHex(const wxColour& c) {
		wxString s;
		s.Printf("#%02X%02X%02X", c.Red(), c.Green(), c.Blue());
		return s;
	}
}

static GroundBrush* findGroundBrushByName(const std::string& targetLower) {
	for (const auto& kv : g_brushes.getMap()) {
		Brush* b = kv.second;
		if (b && b->isGround()) {
			std::string name = as_lower_str(b->getName());
			if (name.find(targetLower) != std::string::npos) {
				return b->asGround();
			}
		}
	}
	return nullptr;
}

static GroundBrush* pickAnyGroundBrush() {
	for (const auto& kv : g_brushes.getMap()) {
		Brush* b = kv.second;
		if (b && b->isGround()) {
			return b->asGround();
		}
	}
	return nullptr;
}

static void chooseDefaultBiomes(GroundBrush*& land, GroundBrush*& water) {
	// Try common names first, then fall back to any available.
	land = findGroundBrushByName("grass");
	if (!land) {
		land = findGroundBrushByName("sand");
	}
	if (!land) {
		land = pickAnyGroundBrush();
	}
	water = findGroundBrushByName("water");
	if (!water) {
		water = findGroundBrushByName("ocean");
	}
	if (!water) {
		water = land; // worst-case, same brush
	}
}

// Minimal MVP: Load image, rescale, pick two ground brushes (land/water) and paint layer z.
bool ProceduralBackends::GenerateFromImage(Editor& editor, const GenerationSpec& spec, wxString& error) {
	if (spec.imageMask.imagePath.empty()) {
		error = "No image selected.";
		return false;
	}

	wxImage img;
	if (!img.LoadFile(spec.imageMask.imagePath)) {
		error = "Failed to load image: " + spec.imageMask.imagePath;
		return false;
	}

	if (spec.size.width <= 0 || spec.size.height <= 0 || spec.size.z < 0 || spec.size.z > MAP_MAX_LAYER) {
		error = "Invalid target size.";
		return false;
	}

	// Rescale image to target map size for 1:1 mapping.
	wxImage scaled = img;
	if (img.GetWidth() != spec.size.width || img.GetHeight() != spec.size.height) {
		scaled.Rescale(spec.size.width, spec.size.height, wxIMAGE_QUALITY_HIGH);
	}

	// Pick ground brushes.
	GroundBrush* landBrush = nullptr;
	GroundBrush* waterBrush = nullptr;
	chooseDefaultBiomes(landBrush, waterBrush);
	if (!landBrush) {
		error = "Could not find any ground brushes in current client; load a version first.";
		return false;
	}

	const int z = spec.size.z;
	editor.selection.clear();

	BatchAction* batch = editor.actionQueue->createBatch(ACTION_DRAW);
	Action* action = editor.actionQueue->createAction(batch);

	// Paint tiles according to simple color heuristic.
	for (int y = 0; y < spec.size.height; ++y) {
		if (y % 64 == 0) {
			g_gui.SetLoadDone(static_cast<int>(100.0 * y / spec.size.height));
		}
		for (int x = 0; x < spec.size.width; ++x) {
			const uint8_t r = scaled.GetRed(x, y);
			const uint8_t g = scaled.GetGreen(x, y);
			const uint8_t b = scaled.GetBlue(x, y);
			const int brightness = (int(r) + int(g) + int(b)) / 3;

			// Heuristic: Blue-ish -> water; dark -> water; else land.
			bool isWater = (b > r && b > g && b > 100) || (brightness < 35);
			GroundBrush* target = isWater ? (waterBrush ? waterBrush : landBrush) : landBrush;

			Position pos(x, y, z);
			TileLocation* location = editor.map.createTileL(pos);
			Tile* existing = location->get();

			Tile* newTile = existing ? existing->deepCopy(editor.map) : editor.map.allocator(location);
			// Clean borders if automagic is on to avoid stacking artifacts.
			if (g_settings.getInteger(Config::USE_AUTOMAGIC)) {
				newTile->cleanBorders();
			}

			// Replace ground with the chosen biome ground.
			if (newTile->ground) {
				delete newTile->ground;
				newTile->ground = nullptr;
			}
			target->draw(&editor.map, newTile, nullptr);

			// Optional: immediate borderize for nicer preview.
			if (g_settings.getInteger(Config::USE_AUTOMAGIC)) {
				newTile->borderize(&editor.map);
			}
			action->addChange(newd Change(newTile));
		}
	}

	batch->addAndCommitAction(action);
	editor.addBatch(batch, 2);
	g_gui.RefreshView();
	return true;
}

// Stub: future prompt-to-spec integration (Python/LLM)
bool ProceduralBackends::GenerateFromPrompt(Editor& editor, const GenerationSpec& spec, wxString& error) {
	if (spec.textPrompt.prompt.empty()) {
		error = "Empty prompt.";
		return false;
	}
	wxString msg;
	msg << "Prompt: " << spec.textPrompt.prompt << "\n";
	msg << "Seed: " << spec.seed << "\n";
	g_gui.PopupDialog("Procedural Generation (Prompt - stub)", msg, wxOK);
	return true;
}
