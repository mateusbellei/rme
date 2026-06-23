#ifndef RME_RENDERING_TILE_RENDERER_H_
#define RME_RENDERING_TILE_RENDERER_H_

#include "rendering/core/zone_finder.h"

#include <cstdint>
#include <sstream>
#include <unordered_map>
#include <vector>

class AtlasManager;
class Editor;
class GraphicManager;
class Item;
class SpriteBatch;
class Tile;
class TileLocation;
class TooltipDrawer;
struct DrawingOptions;
struct LightBuffer;
struct RenderView;

class TileRenderer {
public:
	explicit TileRenderer(Editor* editor);

	void BeginLayer();
	void DrawTile(
		SpriteBatch& sprite_batch,
		AtlasManager& atlas,
		GraphicManager& gfx,
		const TileLocation* location,
		const RenderView& view,
		const DrawingOptions& options,
		TooltipDrawer* tooltip_drawer,
		int draw_x = -1,
		int draw_y = -1,
		LightBuffer* light_buffer = nullptr
	);
	void FinishLayer(const RenderView& view, TooltipDrawer* tooltip_drawer);
	void DrawTileGhost(
		SpriteBatch& sprite_batch,
		AtlasManager& atlas,
		GraphicManager& gfx,
		const TileLocation* location,
		const RenderView& view,
		const DrawingOptions& options,
		int draw_x,
		int draw_y,
		uint8_t alpha
	);

private:
	void calculateTileColor(const Tile* tile, const TileLocation* location, const DrawingOptions& options, uint32_t current_house_id, uint8_t& r, uint8_t& g, uint8_t& b) const;
	void blitItem(
		SpriteBatch& sprite_batch,
		AtlasManager& atlas,
		GraphicManager& gfx,
		int& draw_x,
		int& draw_y,
		const Tile* tile,
		Item* item,
		const DrawingOptions& options,
		int red,
		int green,
		int blue,
		int alpha,
		LightBuffer* light_buffer,
		const RenderView& view
	);
	void blitSpriteType(SpriteBatch& sprite_batch, AtlasManager& atlas, GraphicManager& gfx, int screen_x, int screen_y, uint32_t sprite_id, int red, int green, int blue, int alpha);
	void drawRawBrush(SpriteBatch& sprite_batch, AtlasManager& atlas, GraphicManager& gfx, int screen_x, int screen_y, uint32_t sprite_id, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha);
	void drawSquare(SpriteBatch& sprite_batch, AtlasManager& atlas, int screen_x, int screen_y, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha, int size = 0);
	void collectZonePositions(const Tile* tile);
	void writeItemTooltip(Tile* tile, Item* item, TooltipDrawer* tooltip_drawer, bool is_house_tile, int map_z, const RenderView& view);

	Editor* editor_;
	std::unordered_map<uint16_t, std::vector<FinderPosition>> zone_tiles_;
};

#endif
