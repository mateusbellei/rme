//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "rendering/rendering_gl_first.h"
#include "rendering/core/texture_garbage_collector.h"
#include "graphics.h"
#include "settings.h"
#include <algorithm>

TextureGarbageCollector::TextureGarbageCollector() :
	loaded_textures(0),
	lastclean(0) {
}

TextureGarbageCollector::~TextureGarbageCollector() {
}

void TextureGarbageCollector::Clear() {
	loaded_textures = 0;
	lastclean = time(nullptr);
	cleanup_list.clear();
}

void TextureGarbageCollector::NotifyTextureLoaded() {
	loaded_textures++;
}

void TextureGarbageCollector::NotifyTextureUnloaded() {
	loaded_textures--;
}

const int MIN_CLEAN_THRESHOLD = 100;

void TextureGarbageCollector::AddSpriteToCleanup(GameSprite* spr) {
	cleanup_list.push_back(spr);
	const size_t clean_threshold = static_cast<size_t>(std::max(MIN_CLEAN_THRESHOLD, g_settings.getInteger(Config::SOFTWARE_CLEAN_THRESHOLD)));
	if (cleanup_list.size() > clean_threshold) {
		const auto software_clean_size = std::max(0, g_settings.getInteger(Config::SOFTWARE_CLEAN_SIZE));
		const auto cleanup_count = std::min(cleanup_list.size(), static_cast<size_t>(software_clean_size));

		for (size_t i = 0; i < cleanup_count; ++i) {
			cleanup_list.front()->unloadDC();
			cleanup_list.pop_front();
		}
	}
}

void TextureGarbageCollector::GarbageCollect(std::vector<GameSprite*>& resident_game_sprites, std::vector<void*>& resident_images, time_t current_time) {
	if (g_settings.getInteger(Config::TEXTURE_MANAGEMENT)) {
		if (loaded_textures > g_settings.getInteger(Config::TEXTURE_CLEAN_THRESHOLD) && current_time - lastclean > g_settings.getInteger(Config::TEXTURE_CLEAN_PULSE)) {

			int longevity = g_settings.getInteger(Config::TEXTURE_LONGEVITY);
			for (size_t i = resident_images.size(); i > 0; --i) {
				GameSprite::Image* img = static_cast<GameSprite::Image*>(resident_images[i - 1]);
				img->clean(current_time);

				if (!img->isGLLoaded) {
					if (i - 1 < resident_images.size() - 1) {
						resident_images[i - 1] = resident_images.back();
					}
					resident_images.pop_back();
				}
			}

			for (size_t i = resident_game_sprites.size(); i > 0; --i) {
				GameSprite* gs = resident_game_sprites[i - 1];
				gs->clean(current_time);
			}

			lastclean = current_time;
		}
	}
}

void TextureGarbageCollector::CleanSoftwareSprites(std::vector<std::unique_ptr<Sprite>>& sprite_space) {
	for (auto& sprite_ptr : sprite_space) {
		if (sprite_ptr) {
			sprite_ptr->unloadDC();
		}
	}
}
