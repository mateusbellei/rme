#include "rendering/rendering_gl_first.h"

#include "brush.h"
#include "editor.h"
#include "graphics.h"
#include "gui.h"
#include "house_brush.h"
#include "house_exit_brush.h"
#include "map.h"
#include "map_display.h"
#include "rendering/core/light_buffer.h"
#include "rendering/core/modern_sprite_bridge.h"
#include "rendering/core/render_view.h"
#include "rendering/core/sprite_batch.h"
#include "rendering/drawers/map_layer_drawer.h"
#include "rendering/drawers/map_overlay_drawer.h"
#include "rendering/drawers/map_preview_drawer.h"
#include "rendering/drawers/tile_renderer.h"
#include "rendering/modern_map_drawer.h"
#include "rendering/spatial_hash_grid.h"
#include "rendering/utilities/modern_light_drawer.h"
#include "rendering/utilities/tooltip_drawer.h"

#include <cmath>

struct ModernMapDrawer::Impl {
	Editor& editor;
	RenderView view;
	LightBuffer light_buffer;
	std::unique_ptr<SpriteBatch> sprite_batch;
	std::unique_ptr<TileRenderer> tile_renderer;
	std::unique_ptr<MapLayerDrawer> map_layer_drawer;
	std::unique_ptr<ModernLightDrawer> light_drawer;
	std::unique_ptr<TooltipDrawer> tooltip_drawer;
	bool renderers_initialized = false;

	explicit Impl(MapCanvas* canvas) :
		editor(canvas->editor),
		sprite_batch(std::make_unique<SpriteBatch>()),
		tile_renderer(std::make_unique<TileRenderer>(&editor)),
		map_layer_drawer(std::make_unique<MapLayerDrawer>(tile_renderer.get(), &editor)),
		light_drawer(std::make_unique<ModernLightDrawer>()),
		tooltip_drawer(std::make_unique<TooltipDrawer>()) {
	}
};

ModernMapDrawer::ModernMapDrawer(MapCanvas* canvas) :
	canvas_(canvas),
	impl_(std::make_unique<Impl>(canvas)) {
}

ModernMapDrawer::~ModernMapDrawer() {
	Release();
}

void ModernMapDrawer::SetupVars() {
	options_.current_house_id = 0;
	if (Brush* brush = g_gui.GetCurrentBrush()) {
		if (brush->isHouse()) {
			options_.current_house_id = brush->asHouse()->getHouseID();
		} else if (brush->isHouseExit()) {
			options_.current_house_id = brush->asHouseExit()->getHouseID();
		}
	}

	const double now = wxGetLocalTimeMillis().ToDouble();
	const double speed = 0.005;
	options_.highlight_pulse = static_cast<float>((std::sin(now * speed) + 1.0) / 2.0);

	impl_->view.Setup(canvas_, options_);
}

void ModernMapDrawer::SetupGL() {
	impl_->view.SetupGL();
	if (!impl_->renderers_initialized) {
		impl_->sprite_batch->initialize();
		impl_->renderers_initialized = true;
	}
}

void ModernMapDrawer::Release() {
	if (!impl_) {
		return;
	}
	impl_->tooltip_drawer->clear();
	impl_->view.ReleaseGL();
}

void ModernMapDrawer::Draw() {
	impl_->light_buffer.Clear();
	impl_->tooltip_drawer->clear();

	if (!ModernSpriteBridge::get().ensureAtlasManager(g_gui.gfx)) {
		return;
	}

	AtlasManager* atlas = ModernSpriteBridge::get().getAtlasManager(g_gui.gfx);
	if (!atlas) {
		return;
	}

	if (options_.isDrawLight()) {
		impl_->light_buffer.Prepare(impl_->view);
	}

	DrawBackground();

	impl_->sprite_batch->begin(impl_->view.projectionMatrix, *atlas);
	DrawMap();
	DrawHigherFloors(*atlas);
	MapPreviewDrawer::DrawDraggingShadow(*impl_->sprite_batch, *atlas, g_gui.gfx, *impl_->tile_renderer, canvas_, impl_->editor, impl_->view, options_);
	MapOverlayDrawer::Draw(*impl_->sprite_batch, *atlas, g_gui.gfx, *impl_->tile_renderer, canvas_, impl_->editor, impl_->view, options_);
	impl_->sprite_batch->end(*atlas);

	if (options_.isDrawLight()) {
		DrawLight();
	}
}

void ModernMapDrawer::DrawBackground() {
	impl_->view.Clear();
}

void ModernMapDrawer::DrawMap() {
	const bool live_client = impl_->editor.IsLiveClient();
	AtlasManager* atlas = ModernSpriteBridge::get().getAtlasManager(g_gui.gfx);
	if (!atlas) {
		return;
	}

	RenderView layer_view = impl_->view;
	bool begin_layer = true;

	for (int map_z = layer_view.start_z; map_z >= layer_view.superend_z; --map_z) {
		if (map_z >= layer_view.end_z) {
			impl_->map_layer_drawer->Draw(
				*impl_->sprite_batch,
				*atlas,
				g_gui.gfx,
				map_z,
				live_client,
				layer_view,
				options_,
				impl_->light_buffer,
				impl_->tooltip_drawer.get(),
				begin_layer,
				false
			);
			begin_layer = false;

			MapPreviewDrawer::DrawSecondaryMapLayer(
				*impl_->sprite_batch,
				*atlas,
				g_gui.gfx,
				*impl_->tile_renderer,
				canvas_,
				impl_->editor,
				map_z,
				layer_view,
				options_
			);
		}

		--layer_view.start_x;
		--layer_view.start_y;
		++layer_view.end_x;
		++layer_view.end_y;
	}

	impl_->tile_renderer->FinishLayer(impl_->view, impl_->tooltip_drawer.get());
}

void ModernMapDrawer::DrawHigherFloors(AtlasManager& atlas) {
	if (!options_.shouldDrawTransparentHigherFloors(impl_->view.zoom)) {
		return;
	}

	const RenderView& view = impl_->view;
	if (view.floor == 8 || view.floor == 0) {
		return;
	}

	const int map_z = view.floor - 1;
	const int offset = TILE_SIZE * (view.floor - map_z);
	const int base_screen_x = -view.view_scroll_x - offset;
	const int base_screen_y = -view.view_scroll_y - offset;
	const SpatialHashGrid::NodeBounds node_bounds = SpatialHashGrid::computeNodeBounds(view.start_x, view.start_y, view.end_x, view.end_y);

	for (int nd_map_x = node_bounds.start_x; nd_map_x <= node_bounds.end_x; nd_map_x += SpatialHashGrid::NODE_SIZE) {
		for (int nd_map_y = node_bounds.start_y; nd_map_y <= node_bounds.end_y; nd_map_y += SpatialHashGrid::NODE_SIZE) {
			QTreeNode* node = impl_->editor.map.getLeaf(nd_map_x, nd_map_y);
			if (!node) {
				continue;
			}

			Floor* floor = node->getFloor(map_z);
			if (!floor) {
				continue;
			}

			const int node_draw_x = nd_map_x * TILE_SIZE + base_screen_x;
			const int node_draw_y = nd_map_y * TILE_SIZE + base_screen_y;
			TileLocation* location = floor->locs;
			int draw_x_base = node_draw_x;

			for (int map_x = 0; map_x < 4; ++map_x, draw_x_base += TILE_SIZE) {
				int draw_y = node_draw_y;
				for (int map_y = 0; map_y < 4; ++map_y, ++location, draw_y += TILE_SIZE) {
					if (!location->get()) {
						continue;
					}

					const int absolute_x = nd_map_x + map_x;
					const int absolute_y = nd_map_y + map_y;
					int draw_x = 0;
					int draw_y_visible = 0;
					if (!view.IsTileVisible(absolute_x, absolute_y, map_z, draw_x, draw_y_visible)) {
						continue;
					}

					impl_->tile_renderer->DrawTileGhost(
						*impl_->sprite_batch,
						atlas,
						g_gui.gfx,
						location,
						view,
						options_,
						draw_x,
						draw_y_visible,
						96
					);
				}
			}
		}
	}
}

void ModernMapDrawer::TakeScreenshot(uint8_t* screenshot_buffer) {
	if (!screenshot_buffer) {
		return;
	}

	glFinish();
	glPixelStorei(GL_PACK_ALIGNMENT, 1);

	for (int i = 0; i < impl_->view.screensize_y; ++i) {
		glReadPixels(
			0,
			impl_->view.screensize_y - i,
			impl_->view.screensize_x,
			1,
			GL_RGB,
			GL_UNSIGNED_BYTE,
			screenshot_buffer + 3 * impl_->view.screensize_x * i
		);
	}
}

void ModernMapDrawer::DrawLight() {
	impl_->light_drawer->draw(impl_->view, impl_->light_buffer, options_);
}

void ModernMapDrawer::DrawTooltips(wxDC& dc) {
	impl_->tooltip_drawer->draw(dc, impl_->view);
}
