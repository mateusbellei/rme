#include "world_decoration.h"

#include <algorithm>
#include <queue>

namespace WorldGen {

	bool CanPlaceDecoration(const DecorationGrid& grid, Point anchor, const std::vector<DecorationCell>& footprint, std::string* reason) {
		auto fail = [&](const char* message) {if(reason){*reason=message;
}return false; };
		if (reason) {
			reason->clear();
		}
		if (grid.width <= 0 || grid.height <= 0 || int64_t(grid.width) * grid.height > 8 * 1024 * 1024 || grid.walkable.size() != size_t(grid.width) * grid.height || grid.reserved.size() != grid.walkable.size() || grid.occupied.size() != grid.walkable.size() || grid.habitat.size() != grid.walkable.size()) {
			return fail("Invalid decoration occupancy grid.");
		}
		if (anchor.x < 0 || anchor.y < 0 || anchor.x >= grid.width || anchor.y >= grid.height || footprint.empty() || footprint.size() > 256) {
			return fail("Invalid decoration anchor or footprint size.");
		}
		int minX = 0, maxX = 0, minY = 0, maxY = 0;
		bool blocking = false;
		for (const auto& cell : footprint) {
			if (cell.offset.x < -32 || cell.offset.x > 32 || cell.offset.y < -32 || cell.offset.y > 32) {
				return fail("Decoration offset exceeds supported footprint bounds.");
			}
			minX = std::min(minX, cell.offset.x);
			maxX = std::max(maxX, cell.offset.x);
			minY = std::min(minY, cell.offset.y);
			maxY = std::max(maxY, cell.offset.y);
			blocking = blocking || cell.blocking;
		}
		if (maxX - minX >= 32 || maxY - minY >= 32) {
			return fail("Decoration footprint must fit within 32 by 32 cells.");
		}
		const int left = anchor.x + minX - 1, top = anchor.y + minY - 1, right = anchor.x + maxX + 1, bottom = anchor.y + maxY + 1;
		if (left < 0 || top < 0 || right >= grid.width || bottom >= grid.height) {
			return fail("Decoration clearance extends outside the region.");
		}
		const uint8_t habitat = grid.habitat[size_t(anchor.y) * grid.width + anchor.x];
		if (!habitat) {
			return fail("Anchor has no decoration habitat.");
		}
		for (int y = top; y <= bottom; ++y) {
			for (int x = left; x <= right; ++x) {
				const size_t index = size_t(y) * grid.width + x;
				if (!grid.walkable[index] || grid.reserved[index] || grid.occupied[index] || grid.habitat[index] != habitat) {
					return fail("Decoration overlaps terrain, a route, a reservation or an occupied footprint.");
				}
			}
		}
		if (!blocking) {
			return true;
		}
		const int width = right - left + 1, height = bottom - top + 1;
		std::vector<uint8_t> blocked(size_t(width) * height), seen(blocked.size());
		for (const auto& cell : footprint) {
			if (cell.blocking) {
				blocked[size_t(anchor.y + cell.offset.y - top) * width + anchor.x + cell.offset.x - left] = 1;
			}
		}
		std::queue<int> open;
		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				if (x == 0 || y == 0 || x == width - 1 || y == height - 1) {
					const int index = y * width + x;
					open.push(index);
					seen[index] = 1;
				}
			}
		}
		while (!open.empty()) {
			const int current = open.front();
			open.pop();
			for (const auto [dx, dy] : { std::pair { 1, 0 }, std::pair { -1, 0 }, std::pair { 0, 1 }, std::pair { 0, -1 } }) {
				const int x = current % width + dx, y = current / width + dy;
				if (x < 0 || y < 0 || x >= width || y >= height) {
					continue;
				}
				const int next = y * width + x;
				if (!blocked[next] && !seen[next]) {
					seen[next] = 1;
					open.push(next);
				}
			}
		}
		for (size_t i = 0; i < blocked.size(); ++i) {
			if (!blocked[i] && !seen[i]) {
				return fail("Decoration would enclose an unreachable walkable pocket.");
			}
		}
		return true;
	}

	void MarkDecoration(DecorationGrid& grid, Point anchor, const std::vector<DecorationCell>& footprint) {
		if (grid.width <= 0 || grid.height <= 0 || grid.occupied.size() != size_t(grid.width) * grid.height || grid.walkable.size() != grid.occupied.size()) {
			return;
		}
		for (const auto& cell : footprint) {
			const int64_t x = int64_t(anchor.x) + cell.offset.x, y = int64_t(anchor.y) + cell.offset.y;
			if (x < 0 || y < 0 || x >= grid.width || y >= grid.height) {
				continue;
			}
			const size_t index = size_t(y) * grid.width + size_t(x);
			grid.occupied[index] = 1;
			if (cell.blocking) {
				grid.walkable[index] = 0;
			}
		}
	}
}
