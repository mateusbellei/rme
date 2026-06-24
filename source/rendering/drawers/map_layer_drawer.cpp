#include "rendering/rendering_gl_first.h"

#include "editor.h"
#include "map.h"
#include "map_region.h"
#include "rendering/core/drawing_options.h"
#include "rendering/core/light_buffer.h"
#include "rendering/core/modern_sprite_bridge.h"
#include "rendering/core/render_constants.h"
#include "rendering/core/render_view.h"
#include "rendering/core/sprite_batch.h"
#include "rendering/drawers/map_layer_drawer.h"
#include "rendering/drawers/tile_renderer.h"
#include "rendering/spatial_hash_grid.h"
#include "rendering/utilities/tooltip_drawer.h"

#include <glm/glm.hpp>

MapLayerDrawer::MapLayerDrawer(TileRenderer* tile_renderer, Editor* editor) :
	tile_renderer_(tile_renderer),
	editor_(editor) {
}

void MapLayerDrawer::Draw(
	SpriteBatch& sprite_batch,
	AtlasManager& atlas,
	GraphicManager& gfx,
	int map_z,
	bool live_client,
	const RenderView& view,
	const DrawingOptions& options,
	LightBuffer& light_buffer,
	TooltipDrawer* tooltip_drawer,
	bool begin_layer,
	bool finish_layer
) {
	const int offset = (map_z <= GROUND_LAYER)
		? (GROUND_LAYER - map_z) * TILE_SIZE
		: TILE_SIZE * (view.floor - map_z);

	const int base_screen_x = -view.view_scroll_x - offset;
	const int base_screen_y = -view.view_scroll_y - offset;
	const int visibility_margin_pixels = PAINTERS_ALGORITHM_SAFETY_MARGIN_PIXELS;
	const SpatialHashGrid::NodeBounds node_bounds = SpatialHashGrid::computeNodeBounds(view.start_x, view.start_y, view.end_x, view.end_y);

	const bool draw_lights = options.isDrawLight() && view.zoom <= 10.0f && options.shouldDrawHeavyOverlays(view.zoom);
	if (begin_layer) {
		tile_renderer_->BeginLayer();
	}

	for (int nd_map_x = node_bounds.start_x; nd_map_x <= node_bounds.end_x; nd_map_x += SpatialHashGrid::NODE_SIZE) {
		for (int nd_map_y = node_bounds.start_y; nd_map_y <= node_bounds.end_y; nd_map_y += SpatialHashGrid::NODE_SIZE) {
			QTreeNode* nd = editor_->map.getLeaf(nd_map_x, nd_map_y);
			if (!nd) {
				if (live_client) {
					nd = editor_->map.createLeaf(nd_map_x, nd_map_y);
					nd->setVisible(false, false);
				} else {
					continue;
				}
			}

			const int node_draw_x = nd_map_x * TILE_SIZE + base_screen_x;
			const int node_draw_y = nd_map_y * TILE_SIZE + base_screen_y;
			if (!view.IsRectVisible(node_draw_x, node_draw_y, 4 * TILE_SIZE, 4 * TILE_SIZE, visibility_margin_pixels)) {
				continue;
			}

			if (live_client && !nd->isVisible(map_z > GROUND_LAYER)) {
				if (!nd->isRequested(map_z > GROUND_LAYER)) {
					editor_->QueryNode(nd_map_x, nd_map_y, map_z > GROUND_LAYER);
					nd->setRequested(map_z > GROUND_LAYER, true);
				}
				sprite_batch.drawRect(
					static_cast<float>(node_draw_x),
					static_cast<float>(node_draw_y),
					static_cast<float>(4 * TILE_SIZE),
					static_cast<float>(4 * TILE_SIZE),
					glm::vec4(1.0f, 0.0f, 1.0f, 0.5f),
					atlas
				);
				continue;
			}

			const bool fully_inside = view.IsRectFullyInside(node_draw_x, node_draw_y, 4 * TILE_SIZE, 4 * TILE_SIZE);
			Floor* floor = nd->getFloor(map_z);
			if (!floor) {
				continue;
			}

			TileLocation* location = floor->locs;
			int draw_x_base = node_draw_x;
			for (int map_x = 0; map_x < 4; ++map_x, draw_x_base += TILE_SIZE) {
				int draw_y = node_draw_y;
				for (int map_y = 0; map_y < 4; ++map_y, ++location, draw_y += TILE_SIZE) {
					if (!fully_inside && !view.IsPixelVisible(draw_x_base, draw_y, visibility_margin_pixels)) {
						continue;
					}

					tile_renderer_->DrawTile(
						sprite_batch,
						atlas,
						gfx,
						location,
						view,
						options,
						tooltip_drawer,
						draw_x_base,
						draw_y,
						draw_lights ? &light_buffer : nullptr
					);
				}
			}
		}
	}

	if (finish_layer) {
		tile_renderer_->FinishLayer(view, tooltip_drawer);
	}
}
