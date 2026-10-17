#ifndef RME_RENDERING_CORE_RENDER_VIEW_H_
#define RME_RENDERING_CORE_RENDER_VIEW_H_

#include "definitions.h"
#include "position.h"
#include "rendering/core/render_constants.h"

#include <glm/glm.hpp>

class MapCanvas;
struct DrawingOptions;

struct ViewBounds {
	int start_x = 0;
	int start_y = 0;
	int end_x = 0;
	int end_y = 0;
};

struct RenderView {
	float zoom = 1.0f;
	int tile_size = TILE_SIZE;
	int floor = GROUND_LAYER;

	int start_x = 0;
	int start_y = 0;
	int start_z = GROUND_LAYER;
	int end_x = 0;
	int end_y = 0;
	int end_z = GROUND_LAYER;
	int superend_z = 0;
	int view_scroll_x = 0;
	int view_scroll_y = 0;
	int screensize_x = 0;
	int screensize_y = 0;
	int viewport_x = 0;
	int viewport_y = 0;

	int mouse_map_x = 0;
	int mouse_map_y = 0;

	float logical_width = 0.0f;
	float logical_height = 0.0f;

	glm::mat4 projectionMatrix { 1.0f };
	glm::mat4 viewMatrix { 1.0f };

	void Setup(MapCanvas* canvas, const DrawingOptions& options);
	void SetupGL();
	void ReleaseGL();
	void Clear();

	int getFloorAdjustment() const;
	bool IsTileVisible(int map_x, int map_y, int map_z, int& out_x, int& out_y) const;
	bool IsPixelVisible(int draw_x, int draw_y, int margin = PAINTERS_ALGORITHM_SAFETY_MARGIN_PIXELS) const;
	bool IsRectVisible(int draw_x, int draw_y, int width, int height, int margin = PAINTERS_ALGORITHM_SAFETY_MARGIN_PIXELS) const;
	bool IsRectFullyInside(int draw_x, int draw_y, int width, int height) const;
	void getScreenPosition(int map_x, int map_y, int map_z, int& out_x, int& out_y) const;
};

#endif
