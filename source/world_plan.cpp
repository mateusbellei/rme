#include "world_plan.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <numeric>
#include <queue>
#include <set>
#include <tuple>
#include <utility>

namespace WorldGen {
	namespace {
		constexpr int kMaxCells = 4 * 1024 * 1024;
		constexpr std::array<Point, 4> kDirections = { { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } } };

		uint32_t Hash(uint32_t seed, uint32_t x, uint32_t y = 0) {
			uint32_t value = seed ^ (x * 0x9e3779b9u) ^ (y * 0x85ebca6bu);
			value ^= value >> 16;
			value *= 0x7feb352du;
			value ^= value >> 15;
			value *= 0x846ca68bu;
			return value ^ (value >> 16);
		}

		int Clamp(int value, int low = 0, int high = 10000) {
			return std::max(low, std::min(high, value));
		}

		int Noise(int x, int y, int width, int height, int frequency, uint32_t seed) {
			const int64_t px = int64_t(x) * frequency * 1024 / width;
			const int64_t py = int64_t(y) * frequency * 1024 / height;
			const int gx = int(px / 1024), gy = int(py / 1024);
			auto smooth = [](int64_t t) { return t * t * (3072 - 2 * t) / (1024 * 1024); };
			const int64_t tx = smooth(px % 1024), ty = smooth(py % 1024);
			auto sample = [&](int dx, int dy) { return int64_t(Hash(seed, gx + dx, gy + dy) % 10001); };
			const int64_t a = (sample(0, 0) * (1024 - tx) + sample(1, 0) * tx) / 1024;
			const int64_t b = (sample(0, 1) * (1024 - tx) + sample(1, 1) * tx) / 1024;
			return int((a * (1024 - ty) + b * ty) / 1024);
		}

		bool Inside(int x, int y, int width, int height) {
			return x >= 0 && y >= 0 && x < width && y < height;
		}
		bool Interior(Point p, int width, int height) {
			return p.x > 0 && p.y > 0 && p.x < width - 1 && p.y < height - 1;
		}

		std::vector<int> Components(const std::vector<Cell>& cells, int width, int height, int& count) {
			std::vector<int> labels(cells.size(), -1), queue;
			count = 0;
			for (int i = 0; i < int(cells.size()); ++i) {
				if (labels[i] >= 0 || !IsWalkable(cells[i])) {
					continue;
				}
				queue.clear();
				queue.push_back(i);
				labels[i] = count;
				for (size_t q = 0; q < queue.size(); ++q) {
					const int current = queue[q], x = current % width, y = current / width;
					for (Point direction : kDirections) {
						const int nx = x + direction.x, ny = y + direction.y;
						if (!Inside(nx, ny, width, height)) {
							continue;
						}
						const int next = ny * width + nx;
						if (labels[next] < 0 && IsWalkable(cells[next])) {
							labels[next] = count;
							queue.push_back(next);
						}
					}
				}
				++count;
			}
			return labels;
		}

		void BuildGeography(Plan& plan) {
			const auto& s = plan.settings;
			Layer surface;
			surface.cells.resize(size_t(s.width) * s.height);
			const uint32_t terrainSeed = Hash(s.seed, 1), climateSeed = Hash(s.seed, 2);
			std::array<Point, 5> islandCenters;
			for (uint32_t i = 0; i < islandCenters.size(); ++i) {
				islandCenters[i] = { int(s.width * (20 + Hash(terrainSeed, i, 1) % 60) / 100), int(s.height * (20 + Hash(terrainSeed, i, 2) % 60) / 100) };
			}
			for (int y = 0; y < s.height; ++y) {
				for (int x = 0; x < s.width; ++x) {
					Cell& cell = surface.cells[size_t(y) * s.width + x];
					const int broad = Noise(x, y, s.width, s.height, 3, terrainSeed);
					const int medium = Noise(x, y, s.width, s.height, 8, Hash(terrainSeed, 1));
					const int fine = Noise(x, y, s.width, s.height, 24, Hash(terrainSeed, 2));
					const int noise = (broad * 5 + medium * 2 + fine) / 8 - 5000;
					int64_t falloff = 0;
					if (s.shape == LandShape::Continent) {
						const int64_t nx = int64_t(x) * 10000 / (s.width - 1) - 5000;
						const int64_t ny = int64_t(y) * 10000 / (s.height - 1) - 5000;
						falloff = (nx * nx + ny * ny) * 6500 / 25000000;
					} else {
						falloff = 20000;
						for (Point center : islandCenters) {
							const int64_t nx = int64_t(x - center.x) * 10000 / s.width;
							const int64_t ny = int64_t(y - center.y) * 10000 / s.height;
							falloff = std::min(falloff, (nx * nx + ny * ny) * 6500 / 3500000);
						}
					}
					cell.elevation = uint16_t(Clamp(7800 - int(falloff) + noise * 65 / 100));
					if (x == 0 || y == 0 || x == s.width - 1 || y == s.height - 1) {
						cell.elevation = 0;
					}
					const int latitude = std::abs(y * 10000 / (s.height - 1) - 5000) * 2;
					cell.temperature = uint16_t(Clamp(10500 - latitude * 8 / 10 - std::max(0, int(cell.elevation) - s.seaLevel) * 7 / 10 + (Noise(x, y, s.width, s.height, 4, climateSeed) - 5000) / 5));
					cell.moisture = uint16_t(Noise(x, y, s.width, s.height, 5, Hash(climateSeed, 1)));
					cell.terrain = cell.elevation <= s.seaLevel ? Terrain::Ocean : Terrain::Grass;
				}
			}
			plan.layers.push_back(std::move(surface));
		}

		void BuildDrainage(Plan& plan) {
			const auto& s = plan.settings;
			auto& cells = plan.layers[0].cells;
			using Node = std::pair<int, int>;
			std::priority_queue<Node, std::vector<Node>, std::greater<Node>> frontier;
			std::vector<int> filled(cells.size(), -1), order;
			plan.drainage.assign(cells.size(), -1);
			plan.flow.assign(cells.size(), 1);
			for (int i = 0; i < int(cells.size()); ++i) {
				if (cells[i].terrain == Terrain::Ocean) {
					filled[i] = s.seaLevel;
					frontier.push({ s.seaLevel, i });
				}
			}
			while (!frontier.empty()) {
				const auto [height, current] = frontier.top();
				frontier.pop();
				order.push_back(current);
				const int x = current % s.width, y = current / s.width;
				for (Point dir : kDirections) {
					const int nx = x + dir.x, ny = y + dir.y;
					if (!Inside(nx, ny, s.width, s.height)) {
						continue;
					}
					const int next = ny * s.width + nx;
					if (filled[next] >= 0) {
						continue;
					}
					filled[next] = std::max(int(cells[next].elevation), height + 1);
					plan.drainage[next] = current;
					frontier.push({ filled[next], next });
				}
			}
			for (auto it = order.rbegin(); it != order.rend(); ++it) {
				const int parent = plan.drainage[*it];
				if (parent >= 0) {
					plan.flow[parent] += plan.flow[*it];
				}
			}
			// Depressions use the same spill graph as rivers, so there are no cycles.
			for (int i = 0; i < int(cells.size()); ++i) {
				if (cells[i].terrain != Terrain::Ocean && filled[i] - cells[i].elevation > 220 && plan.flow[i] > 8) {
					cells[i].terrain = Terrain::Lake;
				}
			}
			std::vector<int> candidates;
			for (int i = 0; i < int(cells.size()); ++i) {
				if (!IsWater(cells[i].terrain) && cells[i].elevation > s.seaLevel + 900 && plan.flow[i] >= uint32_t(std::max(8, s.width * s.height / 4096))) {
					candidates.push_back(i);
				}
			}
			std::sort(candidates.begin(), candidates.end(), [&](int a, int b) {
				const int64_t scoreA = int64_t(plan.flow[a]) * (cells[a].elevation - s.seaLevel);
				const int64_t scoreB = int64_t(plan.flow[b]) * (cells[b].elevation - s.seaLevel);
				return scoreA != scoreB ? scoreA > scoreB : a < b;
			});
			std::vector<Point> sources;
			const int separation = std::max(6, std::min(s.width, s.height) / 10);
			for (int candidate : candidates) {
				if (int(sources.size()) >= s.riverCount) {
					break;
				}
				if (IsWater(cells[candidate].terrain)) {
					continue;
				}
				Point source { candidate % s.width, candidate / s.width };
				bool nearby = false;
				for (Point p : sources) {
					if (std::abs(p.x - source.x) + std::abs(p.y - source.y) < separation) {
						nearby = true;
					}
				}
				if (nearby) {
					continue;
				}
				sources.push_back(source);
				for (int current = candidate; current >= 0; current = plan.drainage[current]) {
					if (cells[current].terrain == Terrain::Ocean || cells[current].terrain == Terrain::River) {
						break;
					}
					if (cells[current].terrain != Terrain::Lake) {
						cells[current].terrain = Terrain::River;
					}
				}
			}
			if (int(sources.size()) < s.riverCount) {
				plan.warnings.push_back("Fewer river sources fit the terrain than requested.");
			}
		}

		void ClassifyBiomes(Plan& plan) {
			const auto& s = plan.settings;
			auto& cells = plan.layers[0].cells;
			std::vector<int> distance(cells.size(), -1), queue;
			for (int i = 0; i < int(cells.size()); ++i) {
				if (IsWater(cells[i].terrain)) {
					distance[i] = 0;
					queue.push_back(i);
				}
			}
			for (size_t q = 0; q < queue.size(); ++q) {
				const int current = queue[q], x = current % s.width, y = current / s.width;
				for (Point d : kDirections) {
					const int nx = x + d.x, ny = y + d.y;
					if (!Inside(nx, ny, s.width, s.height)) {
						continue;
					}
					const int next = ny * s.width + nx;
					if (distance[next] < 0) {
						distance[next] = distance[current] + 1;
						queue.push_back(next);
					}
				}
			}
			for (int i = 0; i < int(cells.size()); ++i) {
				Cell& cell = cells[i];
				if (IsWater(cell.terrain)) {
					continue;
				}
				cell.moisture = uint16_t(Clamp(int(cell.moisture) + std::max(0, 1600 - distance[i] * 100)));
				if (distance[i] <= 2 && cell.elevation < s.seaLevel + 600) {
					cell.terrain = Terrain::Beach;
				} else if (cell.elevation > 7600) {
					cell.terrain = Terrain::Mountain;
				} else if (cell.temperature < 3000) {
					cell.terrain = Terrain::Snow;
				} else if (cell.temperature > 6500 && cell.moisture < 4200) {
					cell.terrain = Terrain::Desert;
				} else if (cell.moisture > 5400) {
					cell.terrain = Terrain::Forest;
				} else {
					cell.terrain = Terrain::Grass;
				}
			}
		}

		bool DryBuildable(const Cell& cell) {
			return !IsWater(cell.terrain) && cell.terrain != Terrain::Mountain && !(cell.flags & Reserved);
		}

		// Weighted shortest paths avoid mountains and water; rivers may be bridged.
		bool Road(Plan& plan, Point from, Point to) {
			const auto& s = plan.settings;
			auto& cells = plan.layers[0].cells;
			const int start = int(plan.Index(from)), end = int(plan.Index(to));
			std::vector<int> cost(cells.size(), std::numeric_limits<int>::max()), parent(cells.size(), -1);
			using Node = std::pair<int, int>;
			std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
			cost[start] = 0;
			open.push({ 0, start });
			while (!open.empty()) {
				const auto [value, current] = open.top();
				open.pop();
				if (value != cost[current]) {
					continue;
				}
				if (current == end) {
					break;
				}
				const int x = current % s.width, y = current / s.width;
				for (Point d : kDirections) {
					const int nx = x + d.x, ny = y + d.y;
					if (!Inside(nx, ny, s.width, s.height)) {
						continue;
					}
					const int next = ny * s.width + nx;
					const Cell& c = cells[next];
					if (c.terrain == Terrain::Ocean || c.terrain == Terrain::Lake || c.terrain == Terrain::Mountain || (c.flags & Wall)) {
						continue;
					}
					const int step = c.terrain == Terrain::River ? 50 : (c.terrain == Terrain::Road || c.terrain == Terrain::Plaza ? 2 : 10);
					const int candidate = value + step + std::abs(int(c.elevation) - int(cells[current].elevation)) / 100;
					if (candidate < cost[next]) {
						cost[next] = candidate;
						parent[next] = current;
						open.push({ candidate, next });
					}
				}
			}
			if (cost[end] == std::numeric_limits<int>::max()) {
				return false;
			}
			for (int current = end; current >= 0; current = parent[current]) {
				const int x = current % s.width, y = current / s.width;
				for (int dy = 0; dy < s.roadWidth; ++dy) {
					for (int dx = 0; dx < s.roadWidth; ++dx) {
						const int nx = x + dx, ny = y + dy;
						if (!Inside(nx, ny, s.width, s.height)) {
							continue;
						}
						Cell& c = cells[size_t(ny) * s.width + nx];
						if (c.terrain == Terrain::River) {
							c.terrain = Terrain::Bridge;
						} else if (DryBuildable(c) && c.terrain != Terrain::Plaza) {
							c.terrain = Terrain::Road;
						}
						if (c.terrain == Terrain::Road || c.terrain == Terrain::Bridge) {
							c.flags |= Reserved;
						}
					}
				}
			}
			return true;
		}

		void BuildSettlements(Plan& plan) {
			const auto& s = plan.settings;
			auto& cells = plan.layers[0].cells;
			const int margin = std::max(8, std::min(s.width, s.height) / 14);
			struct Candidate {
				int score, index;
			};
			std::vector<Candidate> candidates;
			for (int y = margin; y < s.height - margin; y += 3) {
				for (int x = margin; x < s.width - margin; x += 3) {
					const int index = y * s.width + x;
					if (!DryBuildable(cells[index])) {
						continue;
					}
					int low = 10000, high = 0, clear = 0;
					for (int dy = -4; dy <= 4; ++dy) {
						for (int dx = -4; dx <= 4; ++dx) {
							const Cell& c = cells[size_t(y + dy) * s.width + x + dx];
							clear += DryBuildable(c);
							low = std::min(low, int(c.elevation));
							high = std::max(high, int(c.elevation));
						}
					}
					if (clear < 81 || high - low > 1300) {
						continue;
					}
					const int score = 10000 - (high - low) * 3 + cells[index].moisture / 4 + int(Hash(s.seed, index, 19) % 500);
					candidates.push_back({ score, index });
				}
			}
			std::sort(candidates.begin(), candidates.end(), [](Candidate a, Candidate b) { return a.score != b.score ? a.score > b.score : a.index < b.index; });
			const int spacing = std::max(20, std::min(s.width, s.height) / 4);
			for (Candidate candidate : candidates) {
				if (int(plan.settlements.size()) >= s.settlementCount) {
					break;
				}
				const Point center { candidate.index % s.width, candidate.index / s.width };
				bool near = false;
				for (const auto& town : plan.settlements) {
					if (std::abs(center.x - town.center.x) + std::abs(center.y - town.center.y) < spacing) {
						near = true;
					}
				}
				if (near) {
					continue;
				}
				Settlement town;
				town.center = center;
				town.radius = std::max(10, std::min(28, std::min(s.width, s.height) / 8));
				for (int dy = -2; dy <= 2; ++dy) {
					for (int dx = -2; dx <= 2; ++dx) {
						Cell& c = cells[size_t(center.y + dy) * s.width + center.x + dx];
						c.terrain = Terrain::Plaza;
						c.flags |= Reserved;
					}
				}
				plan.settlements.push_back(town);
			}
			// A minimum-distance tree avoids redundant roads and connects every reachable town.
			std::vector<bool> joined(plan.settlements.size(), false);
			if (!joined.empty()) {
				joined[0] = true;
			}
			for (size_t pass = 1; pass < joined.size(); ++pass) {
				int bestA = -1, bestB = -1, best = std::numeric_limits<int>::max();
				for (int a = 0; a < int(joined.size()); ++a) {
					if (joined[a]) {
						for (int b = 0; b < int(joined.size()); ++b) {
							if (!joined[b]) {
								const auto p = plan.settlements[a].center, q = plan.settlements[b].center;
								const int distance = std::abs(p.x - q.x) + std::abs(p.y - q.y);
								if (distance < best) {
									best = distance;
									bestA = a;
									bestB = b;
								}
							}
						}
					}
				}
				if (bestB < 0) {
					break;
				}
				if (!Road(plan, plan.settlements[bestA].center, plan.settlements[bestB].center)) {
					plan.warnings.push_back("Settlements on separate land components cannot be connected without a sea crossing.");
				}
				joined[bestB] = true;
			}
			for (int t = 0; t < int(plan.settlements.size()); ++t) {
				auto& town = plan.settlements[t];
				// Lots grow around the central plaza, with setbacks and a path to each door.
				for (int ring = 1; ring <= 4 && int(town.buildings.size()) < s.buildingsPerSettlement; ++ring) {
					for (int side = 0; side < 8 && int(town.buildings.size()) < s.buildingsPerSettlement; ++side) {
						static const Point slots[] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 1, 1 }, { -1, 1 }, { 1, -1 }, { -1, -1 } };
						const int w = 5 + int(Hash(s.seed, t * 97 + ring * 8 + side, 31) % 3), h = 5 + int(Hash(s.seed, t * 97 + ring * 8 + side, 32) % 3);
						const Point origin { town.center.x + slots[side].x * ring * 10 - w / 2, town.center.y + slots[side].y * ring * 10 - h / 2 };
						if (origin.x < 2 || origin.y < 2 || origin.x + w >= s.width - 2 || origin.y + h >= s.height - 2) {
							continue;
						}
						bool clear = true;
						int low = 10000, high = 0;
						for (int y = origin.y - 1; y <= origin.y + h; ++y) {
							for (int x = origin.x - 1; x <= origin.x + w; ++x) {
								const Cell& c = cells[size_t(y) * s.width + x];
								clear = clear && DryBuildable(c);
								low = std::min(low, int(c.elevation));
								high = std::max(high, int(c.elevation));
							}
						}
						if (!clear || high - low > 1300) {
							continue;
						}
						const Point entrance { origin.x + w / 2, origin.y + h - 1 };
						// Reserve the footprint temporarily so the access road routes outside it.
						for (int y = origin.y; y < origin.y + h; ++y) {
							for (int x = origin.x; x < origin.x + w; ++x) {
								cells[size_t(y) * s.width + x].flags |= Wall;
							}
						}
						const bool access = Road(plan, { entrance.x, entrance.y + 1 }, town.center);
						for (int y = origin.y; y < origin.y + h; ++y) {
							for (int x = origin.x; x < origin.x + w; ++x) {
								cells[size_t(y) * s.width + x].flags &= uint8_t(~Wall);
							}
						}
						if (!access) {
							continue;
						}
						Building building;
						building.origin = origin;
						building.width = w;
						building.height = h;
						building.levels = s.buildingLevels;
						building.entrance = entrance;
						building.settlement = t;
						const int index = int(plan.buildings.size());
						plan.buildings.push_back(building);
						town.buildings.push_back(index);
						for (int y = origin.y - 1; y <= origin.y + h; ++y) {
							for (int x = origin.x - 1; x <= origin.x + w; ++x) {
								cells[size_t(y) * s.width + x].flags |= Reserved;
							}
						}
						for (int y = origin.y; y < origin.y + h; ++y) {
							for (int x = origin.x; x < origin.x + w; ++x) {
								Cell& c = cells[size_t(y) * s.width + x];
								c.terrain = Terrain::BuildingFloor;
								if (x == origin.x || y == origin.y || x == origin.x + w - 1 || y == origin.y + h - 1) {
									c.flags |= Wall;
								}
							}
						}
						cells[plan.Index(entrance)].flags = (cells[plan.Index(entrance)].flags & uint8_t(~Wall)) | Entrance;
					}
				}
				if (int(town.buildings.size()) < s.buildingsPerSettlement) {
					plan.warnings.push_back("A settlement has fewer valid building lots than requested.");
				}
			}
			if (int(plan.settlements.size()) < s.settlementCount) {
				plan.warnings.push_back("Fewer dry, sufficiently flat settlement sites fit than requested.");
			}
		}

		Layer MakeCave(const Settings& s, int depth) {
			Layer layer;
			layer.offsetZ = depth;
			layer.cells.resize(size_t(s.width) * s.height);
			for (int y = 0; y < s.height; ++y) {
				for (int x = 0; x < s.width; ++x) {
					auto& c = layer.cells[size_t(y) * s.width + x];
					c.terrain = Interior({ x, y }, s.width, s.height) && int(Hash(s.seed, x + depth * 7919, y) % 100) >= s.caveFill ? Terrain::CaveFloor : Terrain::CaveWall;
				}
			}
			for (int iteration = 0; iteration < 5; ++iteration) {
				auto next = layer.cells;
				for (int y = 1; y < s.height - 1; ++y) {
					for (int x = 1; x < s.width - 1; ++x) {
						int walls = 0;
						for (int dy = -1; dy <= 1; ++dy) {
							for (int dx = -1; dx <= 1; ++dx) {
								if (dx || dy) {
									walls += layer.cells[size_t(y + dy) * s.width + x + dx].terrain == Terrain::CaveWall;
								}
							}
						}
						next[size_t(y) * s.width + x].terrain = walls >= 5 ? Terrain::CaveWall : Terrain::CaveFloor;
					}
				}
				layer.cells.swap(next);
			}
			return layer;
		}

		void ClearLanding(Plan& plan, Layer& layer, Point center, Terrain terrain) {
			const auto& s = plan.settings;
			for (int dy = -1; dy <= 1; ++dy) {
				for (int dx = -1; dx <= 1; ++dx) {
					Point p { center.x + dx, center.y + dy };
					if (!Interior(p, s.width, s.height)) {
						continue;
					}
					auto& c = layer.cells[plan.Index(p)];
					c.terrain = terrain;
					c.flags |= Reserved;
				}
			}
			layer.cells[plan.Index(center)].flags |= Connection;
		}

		bool BuildVertical(Plan& plan, std::string& error) {
			const auto& s = plan.settings;
			// Upper floors occupy only building footprints; the remaining layer is void.
			for (int level = 1; level < s.buildingLevels; ++level) {
				Layer upper;
				upper.offsetZ = -level;
				upper.cells.resize(size_t(s.width) * s.height);
				for (const Building& b : plan.buildings) {
					for (int y = b.origin.y; y < b.origin.y + b.height; ++y) {
						for (int x = b.origin.x; x < b.origin.x + b.width; ++x) {
							Cell& c = upper.cells[size_t(y) * s.width + x];
							c.terrain = Terrain::BuildingFloor;
							c.flags = Reserved;
							if (x == b.origin.x || y == b.origin.y || x == b.origin.x + b.width - 1 || y == b.origin.y + b.height - 1) {
								c.flags |= Wall;
							}
						}
					}
				}
				plan.layers.push_back(std::move(upper));
			}
			for (const Building& b : plan.buildings) {
				for (int level = 1; level < b.levels; ++level) {
					Point p { b.origin.x + (level % 2 ? 1 : 3), b.origin.y + 2 };
					plan.links.push_back({ p, p, -level, -level + 1 });
					plan.FindLayer(-level)->cells[plan.Index(p)].flags |= Connection;
					plan.FindLayer(-level + 1)->cells[plan.Index(p)].flags |= Connection;
				}
			}
			if (s.caveDepth > 0) {
				Point entrance { s.width / 2, s.height / 2 };
				if (!plan.settlements.empty()) {
					entrance = plan.settlements.front().center;
				} else {
					int best = std::numeric_limits<int>::max();
					for (int y = 2; y < s.height - 2; ++y) {
						for (int x = 2; x < s.width - 6; ++x) {
							const Cell& c = plan.layers[0].cells[size_t(y) * s.width + x];
							if (!DryBuildable(c)) {
								continue;
							}
							int distance = std::abs(x - s.width / 2) + std::abs(y - s.height / 2);
							if (distance < best) {
								best = distance;
								entrance = { x, y };
							}
						}
					}
					if (best == std::numeric_limits<int>::max()) {
						error = "No dry surface landing is available for a cave entrance.";
						return false;
					}
				}
				ClearLanding(plan, plan.layers[0], entrance, Terrain::Plaza);
				for (int depth = 1; depth <= s.caveDepth; ++depth) {
					Layer cave = MakeCave(s, depth);
					const Point incoming { entrance.x + ((depth - 1) % 2) * 4, entrance.y };
					ClearLanding(plan, cave, incoming, Terrain::CaveFloor);
					if (depth < s.caveDepth) {
						ClearLanding(plan, cave, { entrance.x + (depth % 2) * 4, entrance.y }, Terrain::CaveFloor);
					}
					ConnectCave(cave.cells, s.width, s.height);
					plan.layers.push_back(std::move(cave));
					plan.links.push_back({ incoming, incoming, depth - 1, depth });
				}
			}
			for (auto& link : plan.links) {
				auto arrival = [&](int floor, Point portal, Point& target) {
					Layer* layer = plan.FindLayer(floor);
					for (Point d : kDirections) {
						Point p { portal.x + d.x, portal.y + d.y };
						if (!plan.Contains(p)) {
							continue;
						}
						Cell& c = layer->cells[plan.Index(p)];
						if (IsWalkable(c) && !(c.flags & Connection)) {
							target = p;
							c.flags |= Reserved;
							return true;
						}
					}
					return false;
				};
				if (!arrival(link.upperOffsetZ, link.upper, link.upperArrival) || !arrival(link.lowerOffsetZ, link.lower, link.lowerArrival)) {
					error = "A vertical connection has no safe arrival tile beside its portal.";
					return false;
				}
			}
			return true;
		}
	}

	Layer* Plan::FindLayer(int offsetZ) {
		for (auto& layer : layers) {
			if (layer.offsetZ == offsetZ) {
				return &layer;
			}
		}
		return nullptr;
	}
	const Layer* Plan::FindLayer(int offsetZ) const {
		for (const auto& layer : layers) {
			if (layer.offsetZ == offsetZ) {
				return &layer;
			}
		}
		return nullptr;
	}
	bool Plan::Contains(Point p) const {
		return Inside(p.x, p.y, settings.width, settings.height);
	}
	size_t Plan::Index(Point p) const {
		return size_t(p.y) * settings.width + p.x;
	}

	bool IsWater(Terrain t) {
		return t == Terrain::Ocean || t == Terrain::Lake || t == Terrain::River;
	}
	bool IsWalkable(const Cell& c) {
		return c.terrain != Terrain::Void && !IsWater(c.terrain) && c.terrain != Terrain::Mountain && c.terrain != Terrain::CaveWall && !(c.flags & Wall);
	}
	const char* TerrainName(Terrain t) {
		static const char* names[] = { "ocean", "lake", "river", "beach", "grass", "forest", "desert", "snow", "mountain", "road", "bridge", "plaza", "building", "cave_wall", "cave_floor", "void" };
		const auto i = static_cast<size_t>(t);
		return i < std::size(names) ? names[i] : "unknown";
	}
	uint32_t TerrainColor(Terrain t) {
		static const uint32_t colors[] = { 0x0000FF, 0x2060D0, 0x4080FF, 0xFFD700, 0x00FF00, 0x008000, 0xE8C060, 0xF0F8FF, 0x654321, 0xA0522D, 0xB08050, 0xD0C0A0, 0xDEB887, 0x808080, 0xC0C0C0, 0x000000 };
		const auto i = static_cast<size_t>(t);
		return i < std::size(colors) ? colors[i] : 0;
	}

	bool ValidateSettings(const Settings& s, std::string& error) {
		error.clear();
		if (s.width < 32 || s.height < 32 || s.width > 4096 || s.height > 4096 || int64_t(s.width) * s.height > kMaxCells) {
			error = "World dimensions must be 32..4096 with at most 4,194,304 surface cells.";
			return false;
		}
		if (s.seaLevel < 2000 || s.seaLevel > 6500 || s.riverCount < 0 || s.riverCount > 64 || s.settlementCount < 0 || s.settlementCount > 32 || s.buildingsPerSettlement < 0 || s.buildingsPerSettlement > 64 || s.buildingLevels < 1 || s.buildingLevels > 4 || s.caveDepth < 0 || s.caveDepth > 8 || s.caveFill < 20 || s.caveFill > 65 || s.roadWidth < 1 || s.roadWidth > 4) {
			error = "World settings are outside supported ranges.";
			return false;
		}
		if (int64_t(s.width) * s.height * (s.buildingLevels + s.caveDepth) > int64_t(kMaxCells) * 2) {
			error = "World layers exceed the 8,388,608-cell planning budget. Reduce dimensions or floors.";
			return false;
		}
		return true;
	}

	bool Build(const Settings& settings, Plan& plan, std::string& error) {
		plan = Plan();
		if (!ValidateSettings(settings, error)) {
			return false;
		}
		plan.settings = settings;
		BuildGeography(plan);
		BuildDrainage(plan);
		ClassifyBiomes(plan);
		BuildSettlements(plan);
		return BuildVertical(plan, error);
	}

	int ConnectCave(std::vector<Cell>& cells, int width, int height) {
		if (width < 3 || height < 3 || cells.size() != size_t(width) * height) {
			return 0;
		}
		int count = 0;
		auto owner = Components(cells, width, height, count);
		if (count <= 1) {
			return count;
		}
		std::vector<int> roots(count), parent(cells.size(), -1), queue;
		std::iota(roots.begin(), roots.end(), 0);
		auto root = [&](int i) {while(roots[i]!=i){roots[i]=roots[roots[i]];i=roots[i];}return i; };
		for (int i = 0; i < int(cells.size()); ++i) {
			if (owner[i] >= 0) {
				queue.push_back(i);
			}
		}
		int remaining = count;
		auto carve = [&](int i) {while(i>=0){cells[i].terrain=Terrain::CaveFloor;i=parent[i];} };
		for (size_t q = 0; q < queue.size() && remaining > 1; ++q) {
			const int current = queue[q];
			Point p { current % width, current / width };
			for (Point d : kDirections) {
				Point n { p.x + d.x, p.y + d.y };
				if (!Interior(n, width, height)) {
					continue;
				}
				const int next = n.y * width + n.x;
				if (owner[next] < 0) {
					owner[next] = owner[current];
					parent[next] = current;
					queue.push_back(next);
				} else if (root(owner[current]) != root(owner[next])) {
					carve(current);
					carve(next);
					roots[root(owner[next])] = root(owner[current]);
					--remaining;
				}
			}
		}
		return remaining;
	}

	Report Validate(const Plan& plan) {
		Report report;
		std::string error;
		auto fail = [&](const std::string& message) {report.valid=false;report.errors.push_back(message); };
		if (!ValidateSettings(plan.settings, error)) {
			fail(error);
			return report;
		}
		const auto* surface = plan.FindLayer(0);
		const size_t count = size_t(plan.settings.width) * plan.settings.height;
		if (!surface || surface->cells.size() != count) {
			fail("Missing or invalid surface layer.");
			return report;
		}
		std::vector<int> offsets;
		for (const auto& layer : plan.layers) {
			if (layer.cells.size() != count) {
				fail("Layer dimensions do not match world dimensions.");
				continue;
			}
			if (std::find(offsets.begin(), offsets.end(), layer.offsetZ) != offsets.end()) {
				fail("Duplicate floor offset.");
			}
			offsets.push_back(layer.offsetZ);
			int components = 0;
			Components(layer.cells, plan.settings.width, plan.settings.height, components);
			if (layer.offsetZ == 0) {
				report.surfaceComponents = components;
				for (const auto& c : layer.cells) {
					if (IsWater(c.terrain)) {
						++report.waterTiles;
					} else {
						++report.landTiles;
					}
					if (c.terrain == Terrain::Road || c.terrain == Terrain::Bridge) {
						++report.roadTiles;
					}
				}
			}
			if (layer.offsetZ > 0) {
				report.caveComponents += components;
				if (components != 1) {
					fail("Cave floor has disconnected walkable components.");
				}
			}
		}
		std::set<std::tuple<int, int, int>> endpoints;
		for (const auto& link : plan.links) {
			const auto* upper = plan.FindLayer(link.upperOffsetZ);
			const auto* lower = plan.FindLayer(link.lowerOffsetZ);
			if (!plan.Contains(link.upper) || !plan.Contains(link.lower) || !upper || !lower || upper->cells.size() != count || lower->cells.size() != count) {
				fail("Vertical link endpoint is outside the plan.");
				continue;
			}
			if (link.lowerOffsetZ != link.upperOffsetZ + 1 || !IsWalkable(upper->cells[plan.Index(link.upper)]) || !IsWalkable(lower->cells[plan.Index(link.lower)])) {
				fail("Vertical link needs adjacent floors and walkable landings.");
			}
			if (!endpoints.insert({ link.upperOffsetZ, link.upper.x, link.upper.y }).second || !endpoints.insert({ link.lowerOffsetZ, link.lower.x, link.lower.y }).second) {
				fail("Vertical travel endpoints overlap on the same tile.");
			}
			for (const auto [layer, arrival, portal] : { std::tuple { upper, link.upperArrival, link.upper }, std::tuple { lower, link.lowerArrival, link.lower } }) {
				if (!plan.Contains(arrival) || std::abs(arrival.x - portal.x) + std::abs(arrival.y - portal.y) != 1) {
					fail("Vertical connection arrival must be adjacent to its portal.");
					continue;
				}
				const Cell& c = layer->cells[plan.Index(arrival)];
				if (!IsWalkable(c) || (c.flags & Connection)) {
					fail("Vertical travel arrival is blocked or creates a portal loop.");
				}
			}
		}
		for (const auto& building : plan.buildings) {
			if (!plan.Contains(building.origin) || building.width < 3 || building.height < 3 || !plan.Contains({ building.origin.x + building.width - 1, building.origin.y + building.height - 1 }) || !plan.Contains(building.entrance)) {
				fail("Invalid building footprint.");
				continue;
			}
			if (!IsWalkable(surface->cells[plan.Index(building.entrance)]) || !(surface->cells[plan.Index(building.entrance)].flags & Entrance)) {
				fail("Building entrance is blocked.");
			}
		}
		return report;
	}

	uint64_t Fingerprint(const Plan& plan) {
		uint64_t hash = 1469598103934665603ull;
		auto add = [&](uint32_t value) {for(int byte=0;byte<4;++byte){hash^=(value>>(byte*8))&255;hash*=1099511628211ull;} };
		add(plan.settings.width);
		add(plan.settings.height);
		add(plan.settings.seed);
		for (const auto& layer : plan.layers) {
			add(uint32_t(layer.offsetZ));
			for (const auto& c : layer.cells) {
				add(uint32_t(c.terrain));
				add(c.flags);
				add(c.elevation);
				add(c.moisture);
				add(c.temperature);
			}
		}
		for (const auto& link : plan.links) {
			add(link.upper.x);
			add(link.upper.y);
			add(link.lower.x);
			add(link.lower.y);
			add(uint32_t(link.upperOffsetZ));
			add(uint32_t(link.lowerOffsetZ));
			add(link.upperArrival.x);
			add(link.upperArrival.y);
			add(link.lowerArrival.x);
			add(link.lowerArrival.y);
		}
		return hash;
	}
}
