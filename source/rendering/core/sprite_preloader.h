//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_RENDERING_CORE_SPRITE_PRELOADER_H_
#define RME_RENDERING_CORE_SPRITE_PRELOADER_H_

#include "graphics.h"
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_set>
#include <vector>

class SpritePreloader {
public:
	[[nodiscard]] static SpritePreloader& get();

	SpritePreloader(const SpritePreloader&) = delete;
	SpritePreloader& operator=(const SpritePreloader&) = delete;

	void preload(GameSprite* spr, int pattern_x, int pattern_y, int pattern_z, int frame);
	void update();
	void clear();
	void shutdown();

private:
	SpritePreloader();
	~SpritePreloader();

	struct PendingSpriteKey {
		uint32_t id = 0;
		uint64_t epoch = 0;

		bool operator==(const PendingSpriteKey& other) const = default;
	};

	struct PendingSpriteKeyHash {
		size_t operator()(const PendingSpriteKey& key) const noexcept {
			size_t seed = std::hash<uint32_t> {}(key.id);
			seed ^= std::hash<uint64_t> {}(key.epoch) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
			return seed;
		}
	};

	struct Task {
		PendingSpriteKey pending;
		GameSprite::NormalImage* image = nullptr;
	};

	struct Result {
		PendingSpriteKey pending;
		std::unique_ptr<uint8_t[]> data;
		GameSprite::NormalImage* image = nullptr;
	};

	void workerLoop(std::stop_token stop_token);

	static constexpr unsigned int MIN_WORKER_THREADS = 2u;
	static constexpr unsigned int MAX_WORKER_THREADS = 8u;
	static constexpr size_t MAX_QUEUE_SIZE = 50000;

	std::mutex queue_mutex;
	std::condition_variable cv;
	bool stopping = false;
	std::vector<std::jthread> workers;

	std::queue<Task> task_queue;
	std::queue<Result> result_queue;
	std::unordered_set<PendingSpriteKey, PendingSpriteKeyHash> pending_ids;
	uint64_t active_epoch = 0;
};

namespace rme {
	void collectTileSprites(GameSprite* spr, int pattern_x, int pattern_y, int pattern_z, int frame);
}

#endif
