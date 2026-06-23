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
#include "rendering/drawers/tile_renderer.h"
#include "rendering/modern_map_drawer.h"
#include "rendering/utilities/modern_light_drawer.h"
#include "rendering/utilities/post_process.h"
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
	MapOverlayDrawer::Draw(*impl_->sprite_batch, *atlas, canvas_, impl_->editor, impl_->view, options_);
	impl_->sprite_batch->end(*atlas);

	if (options_.isDrawLight()) {
		DrawLight();
	}

	PostProcessPass::Apply(impl_->view, impl_->view.screensize_x, impl_->view.screensize_y);
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
				impl_->tooltip_drawer.get()
			);
		}

		--layer_view.start_x;
		--layer_view.start_y;
		++layer_view.end_x;
		++layer_view.end_y;
	}
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
	for (int map_x = view.start_x; map_x <= view.end_x; ++map_x) {
		for (int map_y = view.start_y; map_y <= view.end_y; ++map_y) {
			QTreeNode* node = impl_->editor.map.getLeaf(map_x, map_y);
			if (!node) {
				continue;
			}

			TileLocation* location = node->getTile(map_x, map_y, map_z);
			if (!location || !location->get()) {
				continue;
			}

			int draw_x = 0;
			int draw_y = 0;
			if (!view.IsTileVisible(map_x, map_y, map_z, draw_x, draw_y)) {
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
				draw_y,
				96
			);
		}
	}
}

void ModernMapDrawer::DrawLight() {
	impl_->light_drawer->draw(impl_->view, impl_->light_buffer, options_);
}

void ModernMapDrawer::DrawTooltips(wxDC& dc) {
	impl_->tooltip_drawer->draw(dc, impl_->view);
}
