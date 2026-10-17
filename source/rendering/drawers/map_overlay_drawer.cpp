#include "rendering/rendering_gl_first.h"

#include "creature_brush.h"
#include "raw_brush.h"
#include "wall_brush.h"
#include "editor.h"
#include "gui.h"
#include "live_socket.h"
#include "map.h"
#include "map_display.h"
#include "rendering/core/atlas_manager.h"
#include "rendering/core/drawing_options.h"
#include "rendering/core/render_view.h"
#include "rendering/core/sprite_batch.h"
#include "rendering/drawers/map_overlay_drawer.h"
#include "settings.h"
#include "ui_theme.h"
#include "rendering/drawers/tile_renderer.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kLineThickness = 1.0f;

glm::vec4 brushTintFor(Brush* brush) {
	if (!brush) {
		return glm::vec4(1.0f, 1.0f, 1.0f, 0.5f);
	}

	if (brush->isTerrain() || brush->isTable() || brush->isCarpet()) {
		return glm::vec4(
			g_settings.getInteger(Config::CURSOR_RED) / 255.0f,
			g_settings.getInteger(Config::CURSOR_GREEN) / 255.0f,
			g_settings.getInteger(Config::CURSOR_BLUE) / 255.0f,
			g_settings.getInteger(Config::CURSOR_ALPHA) / 255.0f
		);
	}

	if (brush->isHouse() || brush->isFlag()) {
		return glm::vec4(
			g_settings.getInteger(Config::CURSOR_ALT_RED) / 255.0f,
			g_settings.getInteger(Config::CURSOR_ALT_GREEN) / 255.0f,
			g_settings.getInteger(Config::CURSOR_ALT_BLUE) / 255.0f,
			g_settings.getInteger(Config::CURSOR_ALT_ALPHA) / 255.0f
		);
	}

	if (brush->isSpawn() || brush->isEraser()) {
		return glm::vec4(166.0f / 255.0f, 0.0f, 0.0f, 128.0f / 255.0f);
	}

	return glm::vec4(1.0f, 1.0f, 1.0f, 0.5f);
}

glm::vec4 tileCheckColor(Brush* brush, Editor& editor, const Position& pos) {
	if (brush && brush->canDraw(&editor.map, pos)) {
		return glm::vec4(0.0f, 166.0f / 255.0f, 0.0f, 128.0f / 255.0f);
	}
	return glm::vec4(166.0f / 255.0f, 0.0f, 0.0f, 128.0f / 255.0f);
}

void drawWallBrushQuads(SpriteBatch& batch, AtlasManager& atlas, float start_sx, float start_sy, float end_sx, float end_sy, const glm::vec4& color) {
	const float ts = static_cast<float>(TILE_SIZE);
	const float delta_x = end_sx - start_sx;
	const float delta_y = end_sy - start_sy;

	batch.drawRect(start_sx, start_sy, end_sx - start_sx, ts, color, atlas);

	if (delta_y > ts) {
		batch.drawRect(start_sx, start_sy + ts, ts, end_sy - start_sy - ts, color, atlas);
	}

	if (delta_x > ts && delta_y > ts) {
		batch.drawRect(end_sx - ts, start_sy + ts, ts, end_sy - start_sy - ts, color, atlas);
	}

	if (delta_y > ts) {
		batch.drawRect(start_sx, end_sy - ts, end_sx - start_sx, ts, color, atlas);
	}
}

void drawHorizontalLine(SpriteBatch& batch, AtlasManager& atlas, float x, float y, float width, const glm::vec4& color) {
	batch.drawRect(x, y, width, kLineThickness, color, atlas);
}

void drawVerticalLine(SpriteBatch& batch, AtlasManager& atlas, float x, float y, float height, const glm::vec4& color) {
	batch.drawRect(x, y, kLineThickness, height, color, atlas);
}

} // namespace

void MapOverlayDrawer::DrawGrid(SpriteBatch& batch, AtlasManager& atlas, const RenderView& view) {
	const glm::vec4 color = UiTheme::GetGridColor();
	const float start_screen_x = static_cast<float>(view.start_x * TILE_SIZE - view.view_scroll_x);
	const float start_screen_y = static_cast<float>(view.start_y * TILE_SIZE - view.view_scroll_y);
	const float end_screen_x = static_cast<float>(view.end_x * TILE_SIZE - view.view_scroll_x);
	const float end_screen_y = static_cast<float>(view.end_y * TILE_SIZE - view.view_scroll_y);

	for (int y = view.start_y; y < view.end_y; ++y) {
		const float line_y = static_cast<float>(y * TILE_SIZE - view.view_scroll_y);
		drawHorizontalLine(batch, atlas, start_screen_x, line_y, end_screen_x - start_screen_x, color);
	}

	for (int x = view.start_x; x < view.end_x; ++x) {
		const float line_x = static_cast<float>(x * TILE_SIZE - view.view_scroll_x);
		drawVerticalLine(batch, atlas, line_x, start_screen_y, end_screen_y - start_screen_y, color);
	}
}

void MapOverlayDrawer::DrawFloorShade(SpriteBatch& batch, AtlasManager& atlas, const RenderView& view, const DrawingOptions& options) {
	if (!options.show_shade || view.start_z == view.end_z || options.show_as_minimap || options.show_only_colors) {
		return;
	}

	const glm::vec4 shade(0.0f, 0.0f, 0.0f, 128.0f / 255.0f);
	batch.drawRect(
		0.0f,
		0.0f,
		static_cast<float>(view.screensize_x * view.zoom),
		static_cast<float>(view.screensize_y * view.zoom),
		shade,
		atlas
	);
}

void MapOverlayDrawer::DrawSelectionBox(SpriteBatch& batch, AtlasManager& atlas, MapCanvas* canvas, const RenderView& view) {
	const float last_click_x = static_cast<float>(canvas->last_click_abs_x - view.view_scroll_x);
	const float last_click_y = static_cast<float>(canvas->last_click_abs_y - view.view_scroll_y);
	const float cursor_x = static_cast<float>(canvas->cursor_x * view.zoom);
	const float cursor_y = static_cast<float>(canvas->cursor_y * view.zoom);
	const glm::vec4 color(1.0f, 1.0f, 1.0f, 1.0f);

	drawHorizontalLine(batch, atlas, last_click_x, last_click_y, cursor_x - last_click_x, color);
	drawVerticalLine(batch, atlas, cursor_x, last_click_y, cursor_y - last_click_y, color);
	drawHorizontalLine(batch, atlas, cursor_x, cursor_y, last_click_x - cursor_x, color);
	drawVerticalLine(batch, atlas, last_click_x, cursor_y, last_click_y - cursor_y, color);
}

void MapOverlayDrawer::DrawLiveCursors(SpriteBatch& batch, AtlasManager& atlas, MapCanvas* /*canvas*/, Editor& editor, const RenderView& view) {
	if (!editor.IsLive()) {
		return;
	}

	LiveSocket& live = editor.GetLive();
	for (LiveCursor& cursor : live.getCursorList()) {
		if (cursor.pos.z <= GROUND_LAYER && view.floor > GROUND_LAYER) {
			continue;
		}
		if (cursor.pos.z > GROUND_LAYER && view.floor <= 8) {
			continue;
		}

		wxColor color = cursor.color;
		if (cursor.pos.z < view.floor) {
			color = wxColor(
				color.Red(),
				color.Green(),
				color.Blue(),
				std::max<uint8_t>(color.Alpha() / 2, 64)
			);
		}

		int offset = 0;
		if (cursor.pos.z <= GROUND_LAYER) {
			offset = (GROUND_LAYER - cursor.pos.z) * TILE_SIZE;
		} else {
			offset = TILE_SIZE * (view.floor - cursor.pos.z);
		}

		const float draw_x = static_cast<float>((cursor.pos.x * TILE_SIZE) - view.view_scroll_x - offset);
		const float draw_y = static_cast<float>((cursor.pos.y * TILE_SIZE) - view.view_scroll_y - offset);
		batch.drawRect(
			draw_x,
			draw_y,
			static_cast<float>(TILE_SIZE),
			static_cast<float>(TILE_SIZE),
			glm::vec4(color.Red() / 255.0f, color.Green() / 255.0f, color.Blue() / 255.0f, color.Alpha() / 255.0f),
			atlas
		);
	}
}

void MapOverlayDrawer::DrawIngameBox(SpriteBatch& batch, AtlasManager& atlas, const RenderView& view) {
	const int center_x = view.start_x + static_cast<int>(view.screensize_x * view.zoom / 64);
	const int center_y = view.start_y + static_cast<int>(view.screensize_y * view.zoom / 64);

	constexpr int offset_y = 2;
	const int box_start_map_x = center_x;
	const int box_start_map_y = center_y + offset_y;
	const int box_end_map_x = center_x + ClientMapWidth;
	const int box_end_map_y = center_y + ClientMapHeight + offset_y;

	const float box_start_x = static_cast<float>(box_start_map_x * TILE_SIZE - view.view_scroll_x);
	const float box_start_y = static_cast<float>(box_start_map_y * TILE_SIZE - view.view_scroll_y);
	const float box_end_x = static_cast<float>(box_end_map_x * TILE_SIZE - view.view_scroll_x);
	const float box_end_y = static_cast<float>(box_end_map_y * TILE_SIZE - view.view_scroll_y);
	const float screen_w = static_cast<float>(view.screensize_x * view.zoom);
	const float screen_h = static_cast<float>(view.screensize_y * view.zoom);

	const glm::vec4 side_color(0.0f, 0.0f, 0.0f, 200.0f / 255.0f);
	const glm::vec4 red_color(1.0f, 0.0f, 0.0f, 1.0f);
	const glm::vec4 green_color(0.0f, 1.0f, 0.0f, 1.0f);

	if (box_start_map_x >= view.start_x) {
		batch.drawRect(0.0f, 0.0f, box_start_x, screen_h, side_color, atlas);
	}
	if (box_end_map_x < view.end_x) {
		batch.drawRect(box_end_x, 0.0f, screen_w, screen_h, side_color, atlas);
	}
	if (box_start_map_y >= view.start_y) {
		batch.drawRect(box_start_x, 0.0f, box_end_x - box_start_x, box_start_y, side_color, atlas);
	}
	if (box_end_map_y < view.end_y) {
		batch.drawRect(box_start_x, box_end_y, box_end_x - box_start_x, screen_h, side_color, atlas);
	}

	batch.drawRectLines(box_start_x, box_start_y, box_end_x - box_start_x, box_end_y - box_start_y, red_color, atlas);

	const float inner_start_x = box_start_x + TILE_SIZE;
	const float inner_start_y = box_start_y + TILE_SIZE;
	const float inner_end_x = box_end_x - TILE_SIZE;
	const float inner_end_y = box_end_y - TILE_SIZE;
	batch.drawRectLines(inner_start_x, inner_start_y, inner_end_x - inner_start_x, inner_end_y - inner_start_y, green_color, atlas);

	const float player_x = inner_start_x + ((ClientMapWidth - 3) / 2) * TILE_SIZE;
	const float player_y = inner_start_y + ((ClientMapHeight - 3) / 2) * TILE_SIZE;
	batch.drawRectLines(player_x, player_y, static_cast<float>(TILE_SIZE), static_cast<float>(TILE_SIZE), green_color, atlas);
}

void MapOverlayDrawer::DrawBrush(
	SpriteBatch& batch,
	AtlasManager& atlas,
	GraphicManager& gfx,
	TileRenderer& tile_renderer,
	MapCanvas* canvas,
	Editor& editor,
	const RenderView& view,
	const DrawingOptions& options
) {
	if (!g_gui.IsDrawingMode() || options.ingame) {
		return;
	}

	Brush* brush = g_gui.GetCurrentBrush();
	if (!brush || brush->isDoodad()) {
		return;
	}

	const int floor = view.floor;
	const int floor_adjustment = view.getFloorAdjustment();
	int mouse_map_x = 0;
	int mouse_map_y = 0;
	canvas->MouseToMap(&mouse_map_x, &mouse_map_y);
	const glm::vec4 brush_color = brushTintFor(brush);

	if (canvas->dragging_draw && brush->canDrag()) {
		const int start_map_x = std::min(canvas->last_click_map_x, mouse_map_x);
		const int start_map_y = std::min(canvas->last_click_map_y, mouse_map_y);
		const int end_map_x = std::max(canvas->last_click_map_x, mouse_map_x) + 1;
		const int end_map_y = std::max(canvas->last_click_map_y, mouse_map_y) + 1;

		const float start_sx = static_cast<float>(start_map_x * TILE_SIZE - view.view_scroll_x - floor_adjustment);
		const float start_sy = static_cast<float>(start_map_y * TILE_SIZE - view.view_scroll_y - floor_adjustment);
		const float end_sx = static_cast<float>(end_map_x * TILE_SIZE - view.view_scroll_x - floor_adjustment);
		const float end_sy = static_cast<float>(end_map_y * TILE_SIZE - view.view_scroll_y - floor_adjustment);

		if (brush->isWall()) {
			drawWallBrushQuads(batch, atlas, start_sx, start_sy, end_sx, end_sy, brush_color);
			return;
		}

		if (g_gui.GetBrushShape() == BRUSHSHAPE_SQUARE && (brush->isRaw() || brush->isOptionalBorder())) {
			for (int y = start_map_y; y <= end_map_y; ++y) {
				const int cy = y * TILE_SIZE - view.view_scroll_y - floor_adjustment;
				for (int x = start_map_x; x <= end_map_x; ++x) {
					const int cx = x * TILE_SIZE - view.view_scroll_x - floor_adjustment;
					if (brush->isOptionalBorder()) {
						const glm::vec4 color = tileCheckColor(brush, editor, Position(x, y, floor));
						batch.drawRect(static_cast<float>(cx), static_cast<float>(cy), static_cast<float>(TILE_SIZE), static_cast<float>(TILE_SIZE), color, atlas);
					} else {
						tile_renderer.DrawRawBrushPreview(batch, atlas, gfx, cx, cy, brush->asRaw()->getItemType(), 160, 160, 160, 160);
					}
				}
			}
			return;
		}

		batch.drawRect(start_sx, start_sy, end_sx - start_sx, end_sy - start_sy, brush_color, atlas);
		return;
	}

	if (brush->isWall()) {
		const int start_map_x = mouse_map_x - g_gui.GetBrushSize();
		const int start_map_y = mouse_map_y - g_gui.GetBrushSize();
		const int end_map_x = mouse_map_x + g_gui.GetBrushSize() + 1;
		const int end_map_y = mouse_map_y + g_gui.GetBrushSize() + 1;
		const float start_sx = static_cast<float>(start_map_x * TILE_SIZE - view.view_scroll_x - floor_adjustment);
		const float start_sy = static_cast<float>(start_map_y * TILE_SIZE - view.view_scroll_y - floor_adjustment);
		const float end_sx = static_cast<float>(end_map_x * TILE_SIZE - view.view_scroll_x - floor_adjustment);
		const float end_sy = static_cast<float>(end_map_y * TILE_SIZE - view.view_scroll_y - floor_adjustment);
		drawWallBrushQuads(batch, atlas, start_sx, start_sy, end_sx, end_sy, brush_color);
		return;
	}

	if (brush->isDoor()) {
		const float cx = static_cast<float>(mouse_map_x * TILE_SIZE - view.view_scroll_x - floor_adjustment);
		const float cy = static_cast<float>(mouse_map_y * TILE_SIZE - view.view_scroll_y - floor_adjustment);
		const glm::vec4 color = tileCheckColor(brush, editor, Position(mouse_map_x, mouse_map_y, floor));
		batch.drawRect(cx, cy, static_cast<float>(TILE_SIZE), static_cast<float>(TILE_SIZE), color, atlas);
		return;
	}

	if (brush->isCreature()) {
		const int cy = mouse_map_y * TILE_SIZE - view.view_scroll_y - floor_adjustment;
		const int cx = mouse_map_x * TILE_SIZE - view.view_scroll_x - floor_adjustment;
		CreatureBrush* creature_brush = brush->asCreature();
		if (creature_brush->canDraw(&editor.map, Position(mouse_map_x, mouse_map_y, floor))) {
			tile_renderer.DrawCreatureOutfitPreview(batch, atlas, gfx, cx, cy, creature_brush->getType()->outfit, SOUTH, 255, 255, 255, 160);
		} else {
			tile_renderer.DrawCreatureOutfitPreview(batch, atlas, gfx, cx, cy, creature_brush->getType()->outfit, SOUTH, 255, 64, 64, 160);
		}
		return;
	}

	RAWBrush* raw_brush = brush->isRaw() ? brush->asRaw() : nullptr;
	const int brush_size = g_gui.GetBrushSize();
	const BrushShape shape = g_gui.GetBrushShape();

	for (int y = -brush_size - 1; y <= brush_size + 1; ++y) {
		for (int x = -brush_size - 1; x <= brush_size + 1; ++x) {
			bool inside = false;
			if (shape == BRUSHSHAPE_SQUARE) {
				inside = x >= -brush_size && x <= brush_size && y >= -brush_size && y <= brush_size;
			} else {
				const double distance = std::sqrt(static_cast<double>(x * x + y * y));
				inside = distance < brush_size + 0.005;
			}

			if (!inside) {
				continue;
			}

			const Position pos(mouse_map_x + x, mouse_map_y + y, floor);
			const int cx = (mouse_map_x + x) * TILE_SIZE - view.view_scroll_x - floor_adjustment;
			const int cy = (mouse_map_y + y) * TILE_SIZE - view.view_scroll_y - floor_adjustment;

			if (raw_brush) {
				tile_renderer.DrawRawBrushPreview(batch, atlas, gfx, cx, cy, raw_brush->getItemType(), 160, 160, 160, 160);
			} else if (brush->isHouseExit() || brush->isOptionalBorder()) {
				const glm::vec4 color = tileCheckColor(brush, editor, pos);
				batch.drawRect(static_cast<float>(cx), static_cast<float>(cy), static_cast<float>(TILE_SIZE), static_cast<float>(TILE_SIZE), color, atlas);
			} else {
				batch.drawRect(static_cast<float>(cx), static_cast<float>(cy), static_cast<float>(TILE_SIZE), static_cast<float>(TILE_SIZE), brush_color, atlas);
			}
		}
	}
}

void MapOverlayDrawer::Draw(
	SpriteBatch& sprite_batch,
	AtlasManager& atlas,
	GraphicManager& gfx,
	TileRenderer& tile_renderer,
	MapCanvas* canvas,
	Editor& editor,
	const RenderView& view,
	const DrawingOptions& options
) {
	if (options.show_grid) {
		DrawGrid(sprite_batch, atlas, view);
	}

	DrawFloorShade(sprite_batch, atlas, view, options);

	if (options.dragging) {
		DrawSelectionBox(sprite_batch, atlas, canvas, view);
	}

	DrawLiveCursors(sprite_batch, atlas, canvas, editor, view);
	DrawBrush(sprite_batch, atlas, gfx, tile_renderer, canvas, editor, view, options);

	if (options.show_ingame_box && options.shouldDrawHeavyOverlays(view.zoom)) {
		DrawIngameBox(sprite_batch, atlas, view);
	}
}
