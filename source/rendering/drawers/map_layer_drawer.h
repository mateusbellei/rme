#ifndef RME_RENDERING_MAP_LAYER_DRAWER_H_
#define RME_RENDERING_MAP_LAYER_DRAWER_H_

class AtlasManager;
class Editor;
class GraphicManager;
class TileRenderer;
class TooltipDrawer;
class SpriteBatch;
struct DrawingOptions;
struct LightBuffer;
struct RenderView;

class MapLayerDrawer {
public:
	MapLayerDrawer(TileRenderer* tile_renderer, Editor* editor);

	void Draw(
		SpriteBatch& sprite_batch,
		AtlasManager& atlas,
		GraphicManager& gfx,
		int map_z,
		bool live_client,
		const RenderView& view,
		const DrawingOptions& options,
		LightBuffer& light_buffer,
		TooltipDrawer* tooltip_drawer,
		bool begin_layer = true,
		bool finish_layer = true
	);

private:
	TileRenderer* tile_renderer_;
	Editor* editor_;
};

#endif
