#ifndef RME_RENDERING_MAP_OVERLAY_DRAWER_H_
#define RME_RENDERING_MAP_OVERLAY_DRAWER_H_

class AtlasManager;
class Editor;
class MapCanvas;
class SpriteBatch;
struct DrawingOptions;
struct RenderView;

class MapOverlayDrawer {
public:
	static void Draw(
		SpriteBatch& sprite_batch,
		AtlasManager& atlas,
		MapCanvas* canvas,
		Editor& editor,
		const RenderView& view,
		const DrawingOptions& options
	);

private:
	static void DrawGrid(SpriteBatch& batch, AtlasManager& atlas, const RenderView& view);
	static void DrawSelectionBox(SpriteBatch& batch, AtlasManager& atlas, MapCanvas* canvas, const RenderView& view);
	static void DrawLiveCursors(SpriteBatch& batch, AtlasManager& atlas, MapCanvas* canvas, Editor& editor, const RenderView& view);
	static void DrawIngameBox(SpriteBatch& batch, AtlasManager& atlas, const RenderView& view);
	static void DrawBrush(
		SpriteBatch& batch,
		AtlasManager& atlas,
		MapCanvas* canvas,
		Editor& editor,
		const RenderView& view,
		const DrawingOptions& options
	);
};

#endif
