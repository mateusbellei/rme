//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "rendering/rendering_gl_first.h"
#include "rendering/core/sprite_preloader.h"
#include "rendering/core/modern_sprite_bridge.h"
#include "gui.h"

#include <algorithm>
#include <cassert>

SpritePreloader& SpritePreloader::get() {
	static SpritePreloader instance;
	return instance;
}

SpritePreloader::SpritePreloader() : stopping(false) {
	const unsigned int num_threads = std::clamp(std::thread::hardware_concurrency(), MIN_WORKER_THREADS, MAX_WORKER_THREADS);
	workers.reserve(num_threads);
	for (unsigned int i = 0; i < num_threads; ++i) {
		workers.emplace_back([this](std::stop_token stop_token) {
			this->workerLoop(stop_token);
		});
	}
}

SpritePreloader::~SpritePreloader() {
	shutdown();
}

void SpritePreloader::shutdown() {
	{
		std::lock_guard<std::mutex> lock(queue_mutex);
		if (stopping) {
			return;
		}
		stopping = true;
	}
	for (auto& worker : workers) {
		worker.request_stop();
	}
	cv.notify_all();
}

void SpritePreloader::clear() {
	std::lock_guard<std::mutex> lock(queue_mutex);
	++active_epoch;
	task_queue = std::queue<Task>();
	result_queue = std::queue<Result>();
	pending_ids.clear();
}

void SpritePreloader::preload(GameSprite* spr, int pattern_x, int pattern_y, int pattern_z, int frame) {
	if (!spr || g_gui.gfx.isUnloaded()) {
		return;
	}

	struct PendingTask {
		GameSprite::NormalImage* image = nullptr;
		uint32_t id = 0;
	};

	static thread_local std::vector<PendingTask> ids_to_enqueue;
	ids_to_enqueue.clear();
	if (ids_to_enqueue.capacity() < 64) {
		ids_to_enqueue.reserve(64);
	}

	for (int cx = 0; cx < spr->width; ++cx) {
		for (int cy = 0; cy < spr->height; ++cy) {
			for (int cf = 0; cf < spr->layers; ++cf) {
				int idx = spr->getIndex(cx, cy, cf, pattern_x, pattern_y, pattern_z, frame);

				if (idx >= static_cast<int>(spr->numsprites)) {
					if (spr->numsprites == 1) {
						idx = 0;
					} else {
						idx %= spr->numsprites;
					}
				}

				if (idx < 0 || static_cast<size_t>(idx) >= spr->spriteList.size()) {
					continue;
				}

				GameSprite::NormalImage* img = spr->spriteList[idx];
				if (!img) {
					continue;
				}

				auto& bridge = ModernSpriteBridge::get();
				if (bridge.hasAtlasManager(g_gui.gfx) && bridge.getAtlasManager(g_gui.gfx)->hasSprite(img->id)) {
					continue;
				}

				ids_to_enqueue.push_back({ img, img->id });
			}
		}
	}

	if (ids_to_enqueue.empty()) {
		return;
	}

	std::lock_guard<std::mutex> lock(queue_mutex);
	if (task_queue.size() > MAX_QUEUE_SIZE) {
		return;
	}

	for (const auto& pending : ids_to_enqueue) {
		const PendingSpriteKey pending_key { pending.id, active_epoch };
		if (pending_ids.insert(pending_key).second) {
			task_queue.push({ pending_key, pending.image });
		}
	}
	cv.notify_all();
}

void SpritePreloader::workerLoop(std::stop_token stop_token) {
	auto& bridge = ModernSpriteBridge::get();

	while (!stop_token.stop_requested()) {
		Task task;
		{
			std::unique_lock<std::mutex> lock(queue_mutex);
			cv.wait(lock, [this, &stop_token] { return stop_token.stop_requested() || !task_queue.empty(); });
			if (stop_token.stop_requested()) {
				break;
			}
			task = std::move(task_queue.front());
			task_queue.pop();
		}

		std::unique_ptr<uint8_t[]> rgba;
		uint16_t dump_size = 0;
		auto dump = bridge.loadCompressedDump(g_gui.gfx, task.pending.id, dump_size);
		if (dump) {
			rgba = bridge.decompressToRGBA(dump.get(), dump_size, g_gui.gfx.hasTransparency());
		}

		{
			std::lock_guard<std::mutex> lock(queue_mutex);
			if (rgba) {
				result_queue.push({ task.pending, std::move(rgba), task.image });
			} else {
				pending_ids.erase(task.pending);
			}
		}
	}
}

void SpritePreloader::update() {
	assert(wxIsMainThread());

	std::queue<Result> results;
	uint64_t current_epoch = 0;
	{
		std::lock_guard<std::mutex> lock(queue_mutex);
		if (result_queue.empty()) {
			return;
		}
		results = std::move(result_queue);
		current_epoch = active_epoch;
	}

	thread_local std::vector<PendingSpriteKey> keys_processed;
	keys_processed.clear();
	keys_processed.reserve(results.size());

	auto& bridge = ModernSpriteBridge::get();

	while (!results.empty()) {
		Result res = std::move(results.front());
		results.pop();

		keys_processed.push_back(res.pending);

		if (res.pending.epoch != current_epoch || g_gui.gfx.isUnloaded()) {
			continue;
		}

		if (res.image && res.image->id == res.pending.id) {
			bridge.ensureAtlasSprite(
				g_gui.gfx,
				res.pending.id,
				[&res]() { return res.data.release(); }
			);
		}
	}

	if (!keys_processed.empty()) {
		std::lock_guard<std::mutex> lock(queue_mutex);
		for (const auto& pending : keys_processed) {
			pending_ids.erase(pending);
		}
	}
}

namespace rme {
	void collectTileSprites(GameSprite* spr, int pattern_x, int pattern_y, int pattern_z, int frame) {
		SpritePreloader::get().preload(spr, pattern_x, pattern_y, pattern_z, frame);
	}
}
