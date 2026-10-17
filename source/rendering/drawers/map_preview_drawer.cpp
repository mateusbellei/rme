#include "rendering/rendering_gl_first.h"

#include "brush.h"
#include "editor.h"
#include "gui.h"
#include "map.h"
#include "map_display.h"
#include "rendering/core/atlas_manager.h"
#include "rendering/core/drawing_options.h"
#include "rendering/core/render_view.h"
#include "rendering/core/sprite_batch.h"
#include "rendering/drawers/map_preview_drawer.h"
#include "rendering/drawers/tile_renderer.h"
#include "sprites.h"
#include "tile.h"

void MapPreviewDrawer::DrawSecondaryMapLayer(
	SpriteBatch& sprite_batch,
	AtlasManager& atlas,
	GraphicManager& gfx,
	TileRenderer& tile_renderer,
	MapCanvas* canvas,
	Editor& editor,
	int map_z,
	const RenderView& view,
	const DrawingOptions& options
) {
	if (options.ingame || g_gui.secondary_map == nullptr) {
		return;
	}

	Brush* brush = g_gui.GetCurrentBrush();
	int mouse_map_x = 0;
	int mouse_map_y = 0;
	canvas->MouseToMap(&mouse_map_x, &mouse_map_y);

	Position normal_pos;
	const Position cursor_pos(mouse_map_x, mouse_map_y, view.floor);
	if (canvas->isPasting()) {
		normal_pos = editor.copybuffer.getPosition();
	} else if (brush && brush->isDoodad()) {
		normal_pos = Position(0x8000, 0x8000, 0x8);
	} else {
		return;
	}

	for (int map_x = view.start_x; map_x <= view.end_x; ++map_x) {
		for (int map_y = view.start_y; map_y <= view.end_y; ++map_y) {
			const Position final_pos(map_x, map_y, map_z);
			const Position source_pos = normal_pos + final_pos - cursor_pos;
			if (source_pos.z >= MAP_LAYERS || source_pos.z < 0) {
				continue;
			}

			Tile* tile = g_gui.secondary_map->getTile(source_pos);
			if (!tile) {
				continue;
			}

			int draw_x = 0;
			int draw_y = 0;
			if (!view.IsTileVisible(map_x, map_y, map_z, draw_x, draw_y)) {
				continue;
			}

			tile_renderer.DrawPreviewTile(sprite_batch, atlas, gfx, tile, draw_x, draw_y, view, options);
		}
	}
}

void MapPreviewDrawer::DrawDraggingShadow(
	SpriteBatch& sprite_batch,
	AtlasManager& atlas,
	GraphicManager& gfx,
	TileRenderer& tile_renderer,
	MapCanvas* canvas,
	Editor& editor,
	const RenderView& view,
	const DrawingOptions& options
) {
	if (options.ingame || editor.selection.isBusy() || !canvas->dragging) {
		return;
	}

	int mouse_map_x = 0;
	int mouse_map_y = 0;
	canvas->MouseToMap(&mouse_map_x, &mouse_map_y);

	const int move_x = canvas->drag_start_x - mouse_map_x;
	const int move_y = canvas->drag_start_y - mouse_map_y;
	const int move_z = canvas->drag_start_z - view.floor;
	if (move_x == 0 && move_y == 0 && move_z == 0) {
		return;
	}

	for (Tile* tile : editor.selection) {
		Position pos = tile->getPosition();
		pos.x -= move_x;
		pos.y -= move_y;
		pos.z -= move_z;

		if (pos.z < 0 || pos.z >= MAP_LAYERS) {
			continue;
		}

		if (pos.x + 2 <= view.start_x || pos.x >= view.end_x || pos.y + 2 <= view.start_y || pos.y >= view.end_y) {
			continue;
		}

		int draw_x = 0;
		int draw_y = 0;
		if (!view.IsTileVisible(pos.x, pos.y, pos.z, draw_x, draw_y)) {
			continue;
		}

		const ItemVector to_render = tile->getSelectedItems(view.zoom > 3.0);
		Tile* dest_tile = editor.map.getTile(pos);
		for (Item* item : to_render) {
			tile_renderer.DrawEphemeralItem(
				sprite_batch,
				atlas,
				gfx,
				draw_x,
				draw_y,
				dest_tile ? dest_tile : tile,
				item,
				options,
				view,
				160,
				160,
				160,
				160
			);
		}

		if (view.zoom <= 3.0) {
			if (tile->creature && tile->creature->isSelected() && options.show_creatures) {
				tile_renderer.DrawCreatureOutfitPreview(
					sprite_batch,
					atlas,
					gfx,
					draw_x,
					draw_y,
					tile->creature->getLookType(),
					tile->creature->getDirection(),
					160,
					160,
					160,
					160
				);
			}
			if (tile->spawn && tile->spawn->isSelected()) {
				tile_renderer.DrawEphemeralSpriteType(sprite_batch, atlas, gfx, draw_x, draw_y, SPRITE_SPAWN, 160, 160, 160, 160);
			}
		}
	}
}
