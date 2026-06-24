#include "rendering/core/modern_sprite_bridge.h"
#include "graphics.h"
#include "filehandle.h"
#include "settings.h"
#include <cstring>

namespace {
	bool detectExtendedSprites(const wxFileName& sprites_file) {
		FileReadHandle fh(nstr(sprites_file.GetFullPath()));
		if (!fh.isOk()) {
			return false;
		}
		uint32_t signature = 0;
		if (!fh.getU32(signature)) {
			return false;
		}
		uint32_t count32 = 0;
		if (fh.getU32(count32) && count32 > 0 && count32 < 1000000) {
			return true;
		}
		return false;
	}

	bool readSpriteDump(const wxString& sprite_path, bool is_extended, uint32_t sprite_id, uint8_t*& target, uint16_t& size) {
		if (sprite_id == 0) {
			size = 0;
			target = nullptr;
			return true;
		}

		FileReadHandle fh(nstr(sprite_path));
		if (!fh.isOk()) {
			return false;
		}

		if (!fh.seek((is_extended ? 4 : 2) + sprite_id * sizeof(uint32_t))) {
			return false;
		}

		uint32_t to_seek = 0;
		if (!fh.getU32(to_seek)) {
			return false;
		}
		fh.seek(to_seek + 3);
		uint16_t sprite_size = 0;
		if (!fh.getU16(sprite_size)) {
			return false;
		}

		target = new uint8_t[sprite_size];
		if (!fh.getRAW(target, sprite_size)) {
			delete[] target;
			target = nullptr;
			return false;
		}
		size = sprite_size;
		return true;
	}
}

ModernSpriteBridge& ModernSpriteBridge::get() {
	static ModernSpriteBridge instance;
	return instance;
}

bool ModernSpriteBridge::ensureAtlasManager(GraphicManager& /*gfx*/) {
	if (!atlas_manager_) {
		atlas_manager_ = std::make_unique<AtlasManager>();
	}
	return atlas_manager_->ensureInitialized();
}

AtlasManager* ModernSpriteBridge::getAtlasManager(GraphicManager& /*gfx*/) {
	return atlas_manager_.get();
}

bool ModernSpriteBridge::hasAtlasManager(const GraphicManager& /*gfx*/) const {
	return atlas_manager_ != nullptr && atlas_manager_->isValid();
}

std::unique_ptr<uint8_t[]> ModernSpriteBridge::loadCompressedDump(GraphicManager& gfx, uint32_t sprite_id, uint16_t& size) {
	size = 0;
	if (g_settings.getInteger(Config::USE_MEMCACHED_SPRITES)) {
		return nullptr;
	}

	const wxFileName sprites_file = gfx.getSpritesFileName();
	if (!sprites_file.FileExists()) {
		return nullptr;
	}

	const bool is_extended = detectExtendedSprites(sprites_file);
	uint8_t* dump = nullptr;
	if (!readSpriteDump(sprites_file.GetFullPath(), is_extended, sprite_id, dump, size)) {
		return nullptr;
	}
	return std::unique_ptr<uint8_t[]>(dump);
}

std::unique_ptr<uint8_t[]> ModernSpriteBridge::decompressToRGBA(const uint8_t* dump, uint16_t size, bool has_transparency) {
	if (!dump) {
		return nullptr;
	}

	const int pixels_data_size = SPRITE_PIXELS_SIZE * 4;
	auto data = std::make_unique<uint8_t[]>(pixels_data_size);
	const uint8_t bpp = has_transparency ? 4 : 3;
	int write = 0;
	int read = 0;

	while (read < size && write < pixels_data_size) {
		const int transparent = dump[read] | dump[read + 1] << 8;
		if (has_transparency && transparent >= SPRITE_PIXELS_SIZE) {
			break;
		}
		read += 2;
		for (int i = 0; i < transparent && write < pixels_data_size; ++i) {
			data[write + 0] = 0x00;
			data[write + 1] = 0x00;
			data[write + 2] = 0x00;
			data[write + 3] = 0x00;
			write += 4;
		}

		if (read + 1 >= size) {
			break;
		}

		const int colored = dump[read] | dump[read + 1] << 8;
		read += 2;
		for (int i = 0; i < colored && write < pixels_data_size; ++i) {
			if (read + bpp > size) {
				break;
			}
			data[write + 0] = dump[read + 0];
			data[write + 1] = dump[read + 1];
			data[write + 2] = dump[read + 2];
			data[write + 3] = has_transparency ? dump[read + 3] : 0xFF;
			write += 4;
			read += bpp;
		}
	}

	while (write < pixels_data_size) {
		data[write + 0] = 0x00;
		data[write + 1] = 0x00;
		data[write + 2] = 0x00;
		data[write + 3] = 0x00;
		write += 4;
	}

	return data;
}

const AtlasRegion* ModernSpriteBridge::ensureAtlasSprite(
	GraphicManager& gfx,
	uint32_t sprite_id,
	std::function<uint8_t*()> rgba_provider
) {
	if (!ensureAtlasManager(gfx)) {
		return nullptr;
	}

	AtlasManager* atlas_mgr = atlas_manager_.get();

	const AtlasRegion* region = atlas_mgr->getRegion(sprite_id);
	if (region) {
		if (region->debug_sprite_id == AtlasRegion::INVALID_SENTINEL ||
			(region->debug_sprite_id != 0 && region->debug_sprite_id != sprite_id)) {
			atlas_mgr->clearMapping(sprite_id);
			region = nullptr;
		} else {
			return region;
		}
	}

	uint8_t* rgba = nullptr;
	std::unique_ptr<uint8_t[]> owned_rgba;
	if (rgba_provider) {
		rgba = rgba_provider();
		owned_rgba.reset(rgba);
	} else {
		uint16_t dump_size = 0;
		auto dump = loadCompressedDump(gfx, sprite_id, dump_size);
		if (dump) {
			owned_rgba = decompressToRGBA(dump.get(), dump_size, gfx.hasTransparency());
			rgba = owned_rgba.get();
		}
	}

	if (!rgba) {
		constexpr int SPRITE_DIMENSION = 32;
		constexpr int RGBA_COMPONENTS = 4;
		owned_rgba = std::make_unique<uint8_t[]>(SPRITE_DIMENSION * SPRITE_DIMENSION * RGBA_COMPONENTS);
		for (int i = 0; i < SPRITE_DIMENSION * SPRITE_DIMENSION; ++i) {
			owned_rgba[i * RGBA_COMPONENTS + 0] = 255;
			owned_rgba[i * RGBA_COMPONENTS + 1] = 0;
			owned_rgba[i * RGBA_COMPONENTS + 2] = 255;
			owned_rgba[i * RGBA_COMPONENTS + 3] = 255;
		}
		rgba = owned_rgba.get();
	}

	region = atlas_mgr->addSprite(sprite_id, rgba);
	if (region) {
		collector_.NotifyTextureLoaded();
	}
	return region;
}

const AtlasRegion* ModernSpriteBridge::ensureAtlasSpritePart(
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
) {
	const int part_index = spr.getIndex(cx, cy, cf, pattern_x, pattern_y, pattern_z, frame);
	if (part_index < 0 || part_index >= spr.numsprites) {
		return nullptr;
	}

	auto* image = static_cast<GameSprite::NormalImage*>(spr.spriteList[part_index]);
	if (!image) {
		return nullptr;
	}

	const uint32_t dump_id = static_cast<uint32_t>(image->id);
	return ensureAtlasSprite(gfx, dump_id, [image]() -> uint8_t* {
		return image->getRGBAData();
	});
}

void ModernSpriteBridge::clear(GraphicManager& /*gfx*/) {
	if (atlas_manager_) {
		atlas_manager_->clear();
	}
	collector_.Clear();
}
