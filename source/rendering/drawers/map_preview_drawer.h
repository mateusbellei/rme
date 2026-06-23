#ifndef RME_RENDERING_MAP_PREVIEW_DRAWER_H_
#define RME_RENDERING_MAP_PREVIEW_DRAWER_H_

class AtlasManager;
class Editor;
class GraphicManager;
class MapCanvas;
class SpriteBatch;
class TileRenderer;
struct DrawingOptions;
struct RenderView;

class MapPreviewDrawer {
public:
	static void DrawSecondaryMapLayer(
		SpriteBatch& sprite_batch,
		AtlasManager& atlas,
		GraphicManager& gfx,
		TileRenderer& tile_renderer,
		MapCanvas* canvas,
		Editor& editor,
		int map_z,
		const RenderView& view,
		const DrawingOptions& options
	);

	static void DrawDraggingShadow(
		SpriteBatch& sprite_batch,
		AtlasManager& atlas,
		GraphicManager& gfx,
		TileRenderer& tile_renderer,
		MapCanvas* canvas,
		Editor& editor,
		const RenderView& view,
		const DrawingOptions& options
	);
};

#endif
