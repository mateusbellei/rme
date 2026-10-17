//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_TEXTURE_GARBAGE_COLLECTOR_H_
#define RME_TEXTURE_GARBAGE_COLLECTOR_H_

#include <deque>
#include <memory>
#include <time.h>
#include <vector>

class GameSprite;
class Sprite;

class TextureGarbageCollector {
public:
	TextureGarbageCollector();
	~TextureGarbageCollector();

	void GarbageCollect(std::vector<GameSprite*>& resident_game_sprites, std::vector<void*>& resident_images, time_t current_time);
	void AddSpriteToCleanup(GameSprite* spr);
	void CleanSoftwareSprites(std::vector<std::unique_ptr<Sprite>>& sprite_space);
	void Clear();

	void NotifyTextureLoaded();
	void NotifyTextureUnloaded();

	int GetLoadedTexturesCount() const {
		return loaded_textures;
	}

private:
	int loaded_textures;
	time_t lastclean;
	std::deque<GameSprite*> cleanup_list;
};

#endif
