#ifndef RME_RENDERING_CORE_MODERN_SPRITE_BRIDGE_H_
#define RME_RENDERING_CORE_MODERN_SPRITE_BRIDGE_H_

#include "rendering/core/atlas_manager.h"
#include "rendering/core/texture_garbage_collector.h"
#include <cstdint>
#include <memory>
#include <functional>

class GraphicManager;
class GameSprite;

/**
 * Bridges the modern texture-atlas rendering core to RME's legacy GraphicManager.
 * Owns AtlasManager and texture GC state until GraphicManager is fully migrated.
 */
class ModernSpriteBridge {
public:
	[[nodiscard]] static ModernSpriteBridge& get();

	ModernSpriteBridge(const ModernSpriteBridge&) = delete;
	ModernSpriteBridge& operator=(const ModernSpriteBridge&) = delete;

	bool ensureAtlasManager(GraphicManager& gfx);
	AtlasManager* getAtlasManager(GraphicManager& gfx);
	bool hasAtlasManager(const GraphicManager& gfx) const;

	const AtlasRegion* ensureAtlasSprite(
		GraphicManager& gfx,
		uint32_t sprite_id,
		std::function<uint8_t*()> rgba_provider = nullptr
	);

	const AtlasRegion* ensureAtlasSpritePart(
		GraphicManager& gfx,
		GameSprite& spr,
		int cx,
		int cy,
		int cf,
		int subtype,
		int pattern_x,
		int pattern_y,
		int pattern_z,
		int frame
	);

	std::unique_ptr<uint8_t[]> loadCompressedDump(GraphicManager& gfx, uint32_t sprite_id, uint16_t& size);
	std::unique_ptr<uint8_t[]> decompressToRGBA(const uint8_t* dump, uint16_t size, bool has_transparency);

	void clear(GraphicManager& gfx);
	TextureGarbageCollector& collector() {
		return collector_;
	}
	const TextureGarbageCollector& collector() const {
		return collector_;
	}

private:
	ModernSpriteBridge() = default;

	std::unique_ptr<AtlasManager> atlas_manager_;
	TextureGarbageCollector collector_;
};

#endif
