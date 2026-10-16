#include "world_catalog.h"

#include <algorithm>
#include <queue>
#include <set>

namespace WorldGen {
	namespace {
		bool Floor(char c) {
			return c == '.' || c == '+' || c == 'a' || c == 'b' || c == ',';
		}

		bool ValidateRows(const std::vector<std::string>& rows, bool upper, bool stairs, std::string& error) {
			if (rows.size() < 5 || rows.size() > 32 || rows.front().size() < 5 || rows.front().size() > 32) {
				error = "Blueprint dimensions must be 5..32 cells.";
				return false;
			}
			const int width = int(rows.front().size()), height = int(rows.size());
			int floors = 0, doors = 0, a = 0, b = 0, start = -1;
			for (int y = 0; y < height; ++y) {
				if (int(rows[y].size()) != width) {
					error = "Blueprint rows must have equal width.";
					return false;
				}
				for (int x = 0; x < width; ++x) {
					const char c = rows[y][x];
					if (c != '#' && c != ' ' && !Floor(c)) {
						error = "Unknown blueprint symbol.";
						return false;
					}
					if (Floor(c)) {
						++floors;
						if (start < 0) {
							start = y * width + x;
						}
					}
					if (c == '+') {
						++doors;
						start = y * width + x;
					}
					a += c == 'a';
					b += c == 'b';
					if ((x == 0 || y == 0 || x == width - 1 || y == height - 1) && Floor(c) && (upper || c != '+')) {
						error = "Blueprint outer edges must be walls or void, except the base entrance.";
						return false;
					}
					if (c == '+' && x != 0 && y != 0 && x != width - 1 && y != height - 1) {
						error = "Blueprint entrance must face an outer street edge.";
						return false;
					}
				}
			}
			if (floors == 0 || doors != (upper ? 0 : 1) || a > 1 || b > 1 || (stairs && (a != 1 || b != 1))) {
				error = "Blueprint needs connected floor, one base entrance, and distinct a/b landings for multiple floors.";
				return false;
			}
			std::vector<bool> seen(size_t(width) * height);
			std::queue<int> queue;
			queue.push(start);
			seen[start] = true;
			int reached = 0;
			while (!queue.empty()) {
				const int i = queue.front();
				queue.pop();
				++reached;
				for (const auto [dx, dy] : { std::pair { 1, 0 }, std::pair { -1, 0 }, std::pair { 0, 1 }, std::pair { 0, -1 } }) {
					const int x = i % width + dx, y = i / width + dy;
					if (x < 0 || y < 0 || x >= width || y >= height) {
						continue;
					}
					const int n = y * width + x;
					if (!seen[n] && Floor(rows[y][x])) {
						seen[n] = true;
						queue.push(n);
					}
				}
			}
			if (reached != floors) {
				error = "Blueprint has rooms disconnected from its entrance/landing.";
				return false;
			}
			for (char symbol : { 'a', 'b' }) {
				const auto [x, y] = FindBlueprintSymbol(rows, symbol);
				if (x < 0) {
					continue;
				}
				bool arrival = false;
				for (const auto [dx, dy] : { std::pair { 1, 0 }, std::pair { -1, 0 }, std::pair { 0, 1 }, std::pair { 0, -1 } }) {
					const int nx = x + dx, ny = y + dy;
					if (nx >= 0 && ny >= 0 && nx < width && ny < height && Floor(rows[ny][nx]) && rows[ny][nx] != 'a' && rows[ny][nx] != 'b') {
						arrival = true;
					}
				}
				if (!arrival) {
					error = "Blueprint landing has no safe neighboring arrival tile.";
					return false;
				}
			}
			return true;
		}

		std::vector<std::string> RotateRows(std::vector<std::string> rows, int turns) {
			if (rows.empty() || rows.size()>32 || rows.front().size()>32) return {};
			for (const auto& row:rows) if (row.size()!=rows.front().size()) return {};
			while (turns-- > 0) {
				const size_t h = rows.size(), w = rows.front().size();
				std::vector<std::string> result(w, std::string(h, ' '));
				for (size_t y = 0; y < h; ++y) {
					for (size_t x = 0; x < w; ++x) {
						result[x][h - y - 1] = rows[y][x];
					}
				}
				rows.swap(result);
			}
			return rows;
		}
	}

	std::pair<int, int> FindBlueprintSymbol(const std::vector<std::string>& rows, char symbol) {
		for (int y = 0; y < int(rows.size()); ++y) {
			for (int x = 0; x < int(rows[y].size()); ++x) {
				if (rows[y][x] == symbol) {
					return { x, y };
				}
			}
		}
		return { -1, -1 };
	}

	bool ValidateBlueprint(const Blueprint& b, std::string& error) {
		error.clear();
		if (b.id.empty() || b.id.size() > 64 || b.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos || b.weight < 1 || b.weight > 1000 || b.maxLevels < 1 || b.maxLevels > 4) {
			error = "Blueprint id, weight or maximum floor count is invalid.";
			return false;
		}
		if (!ValidateRows(b.rows, false, b.maxLevels > 1, error)) {
			return false;
		}
		if (!b.upperRows.empty()) {
			if (b.upperRows.size() != b.rows.size() || b.upperRows.front().size() != b.rows.front().size()) {
				error = "Upper blueprint floor dimensions must match the base.";
				return false;
			}
			if (!ValidateRows(b.upperRows, true, b.maxLevels > 1, error)) {
				return false;
			}
			for (char symbol : { 'a', 'b' }) {
				if (FindBlueprintSymbol(b.rows, symbol) != FindBlueprintSymbol(b.upperRows, symbol)) {
					error = "Blueprint landings must align between floors.";
					return false;
				}
			}
		}
		for (const auto& biome : b.biomes) {
			if (biome != "grass" && biome != "forest" && biome != "desert" && biome != "snow" && biome != "beach") {
				error = "Unknown allowed blueprint biome.";
				return false;
			}
		}
		return true;
	}

	bool ValidateCatalog(const BlueprintCatalog& catalog, std::string& error) {
		if (catalog.empty() || catalog.size() > 64) {
			error = "Construction catalog must contain 1..64 blueprints.";
			return false;
		}
		std::set<std::string> ids;
		for (const auto& b : catalog) {
			if (!ValidateBlueprint(b, error)) {
				return false;
			}
			if (!ids.insert(b.id).second) {
				error = "Duplicate blueprint id: " + b.id;
				return false;
			}
		}
		return true;
	}

	Blueprint RotateBlueprint(const Blueprint& blueprint, int quarterTurns) {
		Blueprint result = blueprint;
		const int turns = ((quarterTurns % 4) + 4) % 4;
		if (!result.rows.empty()) {
			result.rows = RotateRows(result.rows, turns);
		}
		if (!result.upperRows.empty()) {
			result.upperRows = RotateRows(result.upperRows, turns);
		}
		return result;
	}

	const BlueprintCatalog& DefaultBlueprints() {
		static const BlueprintCatalog catalog = {
			{ "hut", 4, 2, true, { "#####", "#a.b#", "#...#", "#...#", "##+##" }, {}, {} },
			{ "house", 4, 4, true, { "#######", "#a...b#", "#.....#", "#.....#", "#.....#", "###+###" }, {}, {} },
			{ "inn", 1, 3, true, { "#########", "#a.....b#", "#.......#", "#...#...#", "#.......#", "#.......#", "####+####" }, {}, {} },
			{ "courtyard", 1, 2, true, { "#########", "#a.....b#", "#.......#", "#..,,,..#", "#..,,,..#", "#..,,,..#", "#.......#", "#.......#", "####+####" }, { "#########", "#a.....b#", "#..###..#", "#..# #..#", "#..# #..#", "#..###..#", "#.......#", "#.......#", "#########" }, {} }
		};
		return catalog;
	}
}
