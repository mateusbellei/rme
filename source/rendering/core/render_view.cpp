#include "rendering/rendering_gl_first.h"

#include "map_display.h"
#include "rendering/core/drawing_options.h"
#include "rendering/core/render_view.h"
#include "ui_theme.h"

#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

void RenderView::Setup(MapCanvas* canvas, const DrawingOptions& options) {
	canvas->MouseToMap(&mouse_map_x, &mouse_map_y);
	canvas->GetViewBox(&view_scroll_x, &view_scroll_y, &screensize_x, &screensize_y);
	viewport_x = 0;
	viewport_y = 0;

	zoom = static_cast<float>(canvas->GetZoom());
	tile_size = std::max(1, static_cast<int>(TILE_SIZE / zoom));
	floor = canvas->GetFloor();

	if (options.show_all_floors) {
		if (floor <= GROUND_LAYER) {
			start_z = GROUND_LAYER;
		} else {
			start_z = std::min(MAP_MAX_LAYER, floor + 2);
		}
	} else {
		start_z = floor;
	}

	end_z = floor;
	superend_z = (floor > GROUND_LAYER ? 8 : 0);

	start_x = view_scroll_x / TILE_SIZE;
	start_y = view_scroll_y / TILE_SIZE;

	if (floor > GROUND_LAYER) {
		start_x -= 2;
		start_y -= 2;
	}

	end_x = start_x + screensize_x / tile_size + 2;
	end_y = start_y + screensize_y / tile_size + 2;

	logical_width = screensize_x * zoom;
	logical_height = screensize_y * zoom;
}

int RenderView::getFloorAdjustment() const {
	if (floor > GROUND_LAYER) {
		return 0;
	}
	return TILE_SIZE * (GROUND_LAYER - floor);
}

bool RenderView::IsTileVisible(int map_x, int map_y, int map_z, int& out_x, int& out_y) const {
	const int offset = (map_z <= GROUND_LAYER)
		? (GROUND_LAYER - map_z) * TILE_SIZE
		: TILE_SIZE * (floor - map_z);
	out_x = (map_x * TILE_SIZE) - view_scroll_x - offset;
	out_y = (map_y * TILE_SIZE) - view_scroll_y - offset;
	const int margin = PAINTERS_ALGORITHM_SAFETY_MARGIN_PIXELS;

	if (out_x < -margin || out_x > logical_width + margin || out_y < -margin || out_y > logical_height + margin) {
		return false;
	}
	return true;
}

bool RenderView::IsPixelVisible(int draw_x, int draw_y, int margin) const {
	if (draw_x + TILE_SIZE + margin < 0 || draw_x - margin > logical_width || draw_y + TILE_SIZE + margin < 0 || draw_y - margin > logical_height) {
		return false;
	}
	return true;
}

bool RenderView::IsRectVisible(int draw_x, int draw_y, int width, int height, int margin) const {
	if (draw_x + width + margin < 0 || draw_x - margin > logical_width || draw_y + height + margin < 0 || draw_y - margin > logical_height) {
		return false;
	}
	return true;
}

bool RenderView::IsRectFullyInside(int draw_x, int draw_y, int width, int height) const {
	return draw_x >= 0 && draw_x + width <= logical_width && draw_y >= 0 && draw_y + height <= logical_height;
}

void RenderView::getScreenPosition(int map_x, int map_y, int map_z, int& out_x, int& out_y) const {
	const int offset = (map_z <= GROUND_LAYER)
		? (GROUND_LAYER - map_z) * TILE_SIZE
		: TILE_SIZE * (floor - map_z);
	out_x = (map_x * TILE_SIZE) - view_scroll_x - offset;
	out_y = (map_y * TILE_SIZE) - view_scroll_y - offset;
}

void RenderView::SetupGL() {
	glViewport(viewport_x, viewport_y, screensize_x, screensize_y);
	projectionMatrix = glm::ortho(0.0f, screensize_x * zoom, screensize_y * zoom, 0.0f, -1.0f, 1.0f);
	viewMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(0.375f, 0.375f, 0.0f));
}

void RenderView::ReleaseGL() {
}

void RenderView::Clear() {
	const glm::vec4 clear_color = UiTheme::GetMapClearColor();
	glClearColor(clear_color.r, clear_color.g, clear_color.b, clear_color.a);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}
