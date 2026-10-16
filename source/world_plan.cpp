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
		struct Cancelled { };
		struct Control {
			const ProgressCallback& callback;
			const char* stage = "Preparing world";
			int start = 0, end = 0, percent = 0;
			std::vector<int> roadCost, roadParent;
			std::vector<uint32_t> roadVisited;
			uint32_t roadSearch = 0;
			void Poll() const {
				if (callback && !callback(stage, percent)) {
					throw Cancelled {};
				}
			}
			void Stage(const char* name, int from, int to) {
				stage = name;
				start = from;
				end = to;
				percent = from;
				Poll();
			}
			void Row(int completed, int total) {
				percent = std::max(percent, start + (end - start) * completed / std::max(1, total));
				Poll();
			}
		};
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

		void BuildGeography(Plan& plan, const GeographyInput* input, Control& control) {
			const auto& s = plan.settings;
			Layer surface;
			surface.cells.resize(size_t(s.width) * s.height);
			const uint32_t terrainSeed = Hash(s.seed, 1), climateSeed = Hash(s.seed, 2);
			std::array<Point, 5> islandCenters;
			for (uint32_t i = 0; i < islandCenters.size(); ++i) {
				islandCenters[i] = { int(s.width * (20 + Hash(terrainSeed, i, 1) % 60) / 100), int(s.height * (20 + Hash(terrainSeed, i, 2) % 60) / 100) };
			}
			for (int y = 0; y < s.height; ++y) {
				if ((y & 15) == 0) {
					control.Row(y, s.height);
				}
				for (int x = 0; x < s.width; ++x) {
					Cell& cell = surface.cells[size_t(y) * s.width + x];
					const int broad = Noise(x, y, s.width, s.height, 3, terrainSeed);
					const int medium = Noise(x, y, s.width, s.height, 8, Hash(terrainSeed, 1));
					const int fine = Noise(x, y, s.width, s.height, 24, Hash(terrainSeed, 2));
					const int noise = (broad * 5 + medium * 2 + fine) / 8 - 5000;
					const int warpFrequency = s.shape == LandShape::Continent ? 4 : 5;
					const int warpScale = s.shape == LandShape::Continent ? 400 : 700;
					const int warpX = s.coastRoughness ? (Noise(x, y, s.width, s.height, warpFrequency, Hash(terrainSeed, 71)) - 5000) * s.coastRoughness / warpScale : 0;
					const int warpY = s.coastRoughness ? (Noise(x, y, s.width, s.height, warpFrequency, Hash(terrainSeed, 72)) - 5000) * s.coastRoughness / warpScale : 0;
					int64_t falloff = 0;
					if (s.shape == LandShape::Continent) {
						const int64_t nx = int64_t(x) * 10000 / (s.width - 1) - 5000 + warpX;
						const int64_t ny = int64_t(y) * 10000 / (s.height - 1) - 5000 + warpY;
						falloff = (nx * nx + ny * ny) * 6500 / 25000000;
					} else {
						falloff = 20000;
						for (Point center : islandCenters) {
							const int64_t nx = int64_t(x - center.x) * 10000 / s.width + warpX;
							const int64_t ny = int64_t(y - center.y) * 10000 / s.height + warpY;
							falloff = std::min(falloff, (nx * nx + ny * ny) * 6500 / 3500000);
						}
					}
					cell.elevation = uint16_t(Clamp(7800 - int(falloff) + noise * 65 / 100));
					if (input && !input->elevation.empty() && input->elevation[size_t(y) * s.width + x] != 65535) {
						cell.elevation = input->elevation[size_t(y) * s.width + x];
					}
					if (x == 0 || y == 0 || x == s.width - 1 || y == s.height - 1) {
						cell.elevation = 0;
					}
					const int latitude = std::abs(y * 10000 / (s.height - 1) - 5000) * 2;
					cell.temperature = uint16_t(Clamp(10500 - latitude * 8 / 10 - std::max(0, int(cell.elevation) - s.seaLevel) * 7 / 10 + (Noise(x, y, s.width, s.height, 4, climateSeed) - 5000) / 5 + s.temperatureBias));
					cell.moisture = uint16_t(Clamp(Noise(x, y, s.width, s.height, 5, Hash(climateSeed, 1)) + s.moistureBias));
					cell.terrain = cell.elevation <= s.seaLevel ? Terrain::Ocean : Terrain::Grass;
				}
			}
			plan.layers.push_back(std::move(surface));
		}

		void BuildDrainage(Plan& plan, Control& control) {
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
				if ((order.size() & 4095) == 0) {
					control.Row(int(order.size()), int(cells.size()));
				}
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
			std::vector<int> riverCenters;
			uint32_t maximumFlow = 1;
			for (int i = 0; i < int(cells.size()); ++i) {
				if (cells[i].terrain == Terrain::River) {
					riverCenters.push_back(i);
					maximumFlow = std::max(maximumFlow, plan.flow[i]);
				}
			}
			for (int i : riverCenters) {
				const int radius = std::min((s.riverWidth - 1) / 2, int(uint64_t(s.riverWidth - 1) * plan.flow[i] / maximumFlow));
				const int x = i % s.width, y = i / s.width;
				for (int dy = -radius; dy <= radius; ++dy) {
					for (int dx = -radius; dx <= radius; ++dx) {
						if (std::abs(dx) + std::abs(dy) > radius || !Interior({ x + dx, y + dy }, s.width, s.height)) {
							continue;
						}
						auto& cell = cells[size_t(y + dy) * s.width + x + dx];
						if (cell.terrain != Terrain::Ocean && cell.terrain != Terrain::Lake) {
							cell.terrain = Terrain::River;
						}
					}
				}
			}
			if (int(sources.size()) < s.riverCount) {
				plan.warnings.push_back("Fewer river sources fit the terrain than requested.");
			}
		}

		void ClassifyBiomes(Plan& plan, Control& control) {
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
				if ((q & 4095) == 0) {
					control.Poll();
				}
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
			return !IsWater(cell.terrain) && cell.terrain != Terrain::Mountain && !(cell.flags & (Reserved | Wall));
		}

		// Weighted shortest paths avoid mountains and water; rivers may be bridged.
		bool Road(Plan& plan, Point from, Point to, Control& control) {
			const auto& s = plan.settings;
			auto& cells = plan.layers[0].cells;
			const int start = int(plan.Index(from)), end = int(plan.Index(to));
			if (control.roadCost.size() != cells.size()) {
				control.roadCost.resize(cells.size());
				control.roadParent.resize(cells.size());
				control.roadVisited.assign(cells.size(), 0);
			}
			if (++control.roadSearch == 0) {
				std::fill(control.roadVisited.begin(), control.roadVisited.end(), 0);
				++control.roadSearch;
			}
			auto& cost = control.roadCost;
			auto& parent = control.roadParent;
			auto& visited = control.roadVisited;
			const uint32_t search = control.roadSearch;
			auto heuristic = [&](int index) { return (std::abs(index % s.width - to.x) + std::abs(index / s.width - to.y)) * 2; };
			using Node = std::tuple<int, int, int>;
			std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
			cost[start] = 0;
			parent[start] = -1;
			visited[start] = search;
			open.push({ heuristic(start), 0, start });
			size_t expanded = 0;
			while (!open.empty()) {
				if ((expanded++ & 4095) == 0) {
					control.Poll();
				}
				const auto [priority, value, current] = open.top();
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
					if (visited[next] != search || candidate < cost[next]) {
						visited[next] = search;
						cost[next] = candidate;
						parent[next] = current;
						open.push({ candidate + heuristic(next), candidate, next });
					}
				}
			}
			if (visited[end] != search) {
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

		struct LotSlot {
			Point center, frontage, junction;
			uint32_t serial = 0;
		};

		std::vector<LotSlot> SettlementLots(const Plan& plan, const Settlement& town) {
			std::vector<LotSlot> lots;
			const auto& s = plan.settings;
			if (s.settlementLayout == SettlementLayout::Organic) {
				static const Point directions[] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 1, 1 }, { -1, 1 }, { 1, -1 }, { -1, -1 } };
				for (int ring = 1; ring <= std::max(4, (s.buildingsPerSettlement + 3) / 4); ++ring) {
					for (int side = 0; side < 8; ++side) {
						lots.push_back({ { town.center.x + directions[side].x * ring * 10, town.center.y + directions[side].y * ring * 10 }, town.center, town.center, uint32_t(ring * 8 + side) });
					}
				}
				return lots;
			}
			int largest = 5;
			for (const auto& b : plan.catalog) {
				if (b.biomes.empty() || std::find(b.biomes.begin(), b.biomes.end(), TerrainName(town.biome)) != b.biomes.end()) {
					largest = std::max(largest, std::max(int(b.rows.size()), int(b.rows.front().size())));
				}
			}
			const int pitch = largest + s.roadWidth + 4;
			int reach = 2;
			while ((reach * 2) * (reach * 2) < s.buildingsPerSettlement * 3) {
				++reach;
			}
			for (int gy = -reach; gy < reach; ++gy) {
				for (int gx = -reach; gx < reach; ++gx) {
					const Point center { town.center.x + gx * pitch + pitch / 2, town.center.y + gy * pitch + pitch / 2 };
					const int streetX = town.center.x + (gx >= 0 ? gx : gx + 1) * pitch;
					const int streetY = town.center.y + (gy >= 0 ? gy : gy + 1) * pitch;
					const bool vertical = std::abs(center.x - town.center.x) >= std::abs(center.y - town.center.y);
					lots.push_back({ center, vertical ? Point { streetX, center.y } : Point { center.x, streetY }, vertical ? Point { streetX, town.center.y } : Point { town.center.x, streetY }, uint32_t((gy + reach) * reach * 2 + gx + reach + 1000) });
				}
			}
			std::sort(lots.begin(), lots.end(), [&](const LotSlot& a, const LotSlot& b) {const int da=std::abs(a.center.x-town.center.x)+std::abs(a.center.y-town.center.y),db=std::abs(b.center.x-town.center.x)+std::abs(b.center.y-town.center.y);return da!=db?da<db:a.serial<b.serial; });
			return lots;
		}

		void BuildSettlements(Plan& plan, Control& control) {
			const auto& s = plan.settings;
			auto& cells = plan.layers[0].cells;
			const int margin = std::max(8, std::min(s.width, s.height) / 14);
			std::vector<int> dryArea;
			int cityRadius = 0;
			if (s.settlementLayout == SettlementLayout::Blocks && s.buildingsPerSettlement > 0) {
				int largest = 5, across = 1;
				for (const auto& b : plan.catalog) {
					largest = std::max(largest, std::max(int(b.rows.size()), int(b.rows.front().size())));
				}
				while (across * across < s.buildingsPerSettlement) {
					++across;
				}
				cityRadius = (across + 1) * (largest + s.roadWidth + 4) / 2;
				dryArea.assign(size_t(s.width + 1) * (s.height + 1), 0);
				for (int y = 0; y < s.height; ++y) {
					if ((y & 15) == 0) {
						control.Poll();
					}
					int row = 0;
					for (int x = 0; x < s.width; ++x) {
						row += DryBuildable(cells[size_t(y) * s.width + x]);
						dryArea[size_t(y + 1) * (s.width + 1) + x + 1] = dryArea[size_t(y) * (s.width + 1) + x + 1] + row;
					}
				}
			}
			auto cityFitness = [&](int x, int y) {
				if (dryArea.empty()) {
					return 0;
				}
				const int x0 = std::max(0, x - cityRadius), y0 = std::max(0, y - cityRadius), x1 = std::min(s.width, x + cityRadius + 1), y1 = std::min(s.height, y + cityRadius + 1);
				const int count = dryArea[size_t(y1) * (s.width + 1) + x1] - dryArea[size_t(y0) * (s.width + 1) + x1] - dryArea[size_t(y1) * (s.width + 1) + x0] + dryArea[size_t(y0) * (s.width + 1) + x0];
				return int(int64_t(count) * 15000 / ((cityRadius * 2 + 1) * (cityRadius * 2 + 1)));
			};
			struct Candidate {
				int score, index;
			};
			std::vector<Candidate> candidates;
			for (int y = margin; y < s.height - margin; y += 3) {
				control.Row(y, s.height);
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
					const int score = 10000 - (high - low) * 3 + cells[index].moisture / 4 + int(Hash(s.seed, index, 19) % 500) + cityFitness(x, y);
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
				town.biome = cells[candidate.index].terrain;
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
				if (!Road(plan, plan.settlements[bestA].center, plan.settlements[bestB].center, control)) {
					plan.warnings.push_back("Settlements on separate land components cannot be connected without a sea crossing.");
				}
				joined[bestB] = true;
			}
			for (int t = 0; t < int(plan.settlements.size()); ++t) {
				control.Poll();
				auto& town = plan.settlements[t];
				// Organic radial lots or compact street blocks, always connected to the plaza.
				for (const auto& slot : SettlementLots(plan, town)) {
					if (int(town.buildings.size()) >= s.buildingsPerSettlement) {
						break;
					}
					control.Poll();
					if (s.settlementLayout == SettlementLayout::Blocks) {
						bool belongs = true;
						const int own = std::abs(slot.center.x - town.center.x) + std::abs(slot.center.y - town.center.y);
						for (int other = 0; other < int(plan.settlements.size()); ++other) {
							if (other != t) {
								const auto p = plan.settlements[other].center;
								const int distance = std::abs(slot.center.x - p.x) + std::abs(slot.center.y - p.y);
								if (distance < own || (distance == own && other < t)) {
									belongs = false;
								}
							}
						}
						if (!belongs) {
							continue;
						}
					}

					std::vector<int> eligible;
					int totalWeight = 0;
					for (int i = 0; i < int(plan.catalog.size()); ++i) {
						const auto& candidate = plan.catalog[i];
						if (candidate.biomes.empty() || std::find(candidate.biomes.begin(), candidate.biomes.end(), TerrainName(town.biome)) != candidate.biomes.end()) {
							eligible.push_back(i);
							totalWeight += candidate.weight;
						}
					}
					if (eligible.empty()) {
						continue;
					}
					int roll = int(Hash(s.seed, uint32_t(t) * 997u + slot.serial, 31) % uint32_t(totalWeight));
					int templateIndex = eligible.back();
					for (int i : eligible) {
						if (roll < plan.catalog[i].weight) {
							templateIndex = i;
							break;
						}
						roll -= plan.catalog[i].weight;
					}
					int rotation = 0, bestDistance = std::numeric_limits<int>::max();
					// Determine orientation from the actual template entrance, including custom north/east doors.
					for (int turn = 0; turn < (plan.catalog[templateIndex].allowRotation ? 4 : 1); ++turn) {
						const auto candidate = RotateBlueprint(plan.catalog[templateIndex], turn);
						const auto [doorX, doorY] = FindBlueprintSymbol(candidate.rows, '+');
						const int w = int(candidate.rows.front().size()), h = int(candidate.rows.size());
						const int x = slot.center.x - w / 2 + doorX, y = slot.center.y - h / 2 + doorY;
						const int distance = std::abs(x - slot.frontage.x) + std::abs(y - slot.frontage.y);
						if (distance < bestDistance) {
							bestDistance = distance;
							rotation = turn;
						}
					}
					const auto blueprint = RotateBlueprint(plan.catalog[templateIndex], rotation);
					const int w = int(blueprint.rows.front().size()), h = int(blueprint.rows.size());
					const Point origin { slot.center.x - w / 2, slot.center.y - h / 2 };
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
					const auto [doorX, doorY] = FindBlueprintSymbol(blueprint.rows, '+');
					const Point entrance { origin.x + doorX, origin.y + doorY };
					Point approach = entrance;
					if (doorX == 0) {
						--approach.x;
					} else if (doorX == w - 1) {
						++approach.x;
					} else if (doorY == 0) {
						--approach.y;
					} else {
						++approach.y;
					}
					// Reserve the footprint temporarily so the access road routes outside it.
					for (int y = origin.y; y < origin.y + h; ++y) {
						for (int x = origin.x; x < origin.x + w; ++x) {
							cells[size_t(y) * s.width + x].flags |= Wall;
						}
					}
					bool access = false;
					if (s.settlementLayout == SettlementLayout::Blocks && plan.Contains(slot.frontage) && plan.Contains(slot.junction)) {
						access = Road(plan, slot.junction, town.center, control) && Road(plan, slot.frontage, slot.junction, control) && Road(plan, approach, slot.frontage, control);
					} else if (s.settlementLayout == SettlementLayout::Organic) {
						access = Road(plan, approach, town.center, control);
					}
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
					building.levels = std::min(s.buildingLevels, blueprint.maxLevels);
					building.blueprint = templateIndex;
					building.rotation = rotation;
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

					for (int y = 0; y < h; ++y) {
						for (int x = 0; x < w; ++x) {
							const char symbol = blueprint.rows[y][x];
							if (symbol == ' ') {
								continue;
							}
							Cell& c = cells[size_t(origin.y + y) * s.width + origin.x + x];
							c.terrain = symbol == ',' ? Terrain::Grass : Terrain::BuildingFloor;
							c.flags = uint8_t(Reserved | (symbol == '#' ? Wall : 0) | (symbol == '+' ? Entrance : 0));
						}
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

		Layer MakeCave(const Settings& s, int depth, Control& control) {
			Layer layer;
			layer.offsetZ = depth;
			layer.cells.resize(size_t(s.width) * s.height);
			for (int y = 0; y < s.height; ++y) {
				if ((y & 15) == 0) {
					control.Poll();
				}
				for (int x = 0; x < s.width; ++x) {
					auto& c = layer.cells[size_t(y) * s.width + x];
					c.terrain = Interior({ x, y }, s.width, s.height) && int(Hash(s.seed, x + depth * 7919, y) % 100) >= s.caveFill ? Terrain::CaveFloor : Terrain::CaveWall;
				}
			}
			for (int iteration = 0; iteration < 5; ++iteration) {
				auto next = layer.cells;
				for (int y = 1; y < s.height - 1; ++y) {
					if ((y & 15) == 0) {
						control.Poll();
					}
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

		bool BuildVertical(Plan& plan, std::string& error, Control& control) {
			const auto& s = plan.settings;
			// Upper floors occupy only building footprints; the remaining layer is void.
			for (int level = 1; level < s.buildingLevels; ++level) {
				control.Poll();
				Layer upper;
				upper.offsetZ = -level;
				upper.cells.resize(size_t(s.width) * s.height);

				for (const Building& b : plan.buildings) {
					if (level >= b.levels) {
						continue;
					}
					const auto blueprint = RotateBlueprint(plan.catalog[b.blueprint], b.rotation);
					auto rows = blueprint.upperRows.empty() ? blueprint.rows : blueprint.upperRows;
					for (int y = 0; y < b.height; ++y) {
						for (int x = 0; x < b.width; ++x) {
							char symbol = rows[y][x];
							if (symbol == '+') {
								symbol = '#';
							}
							if (symbol == ',') {
								symbol = ' ';
							}
							if (symbol == ' ') {
								continue;
							}
							Cell& c = upper.cells[size_t(b.origin.y + y) * s.width + b.origin.x + x];
							c.terrain = Terrain::BuildingFloor;
							c.flags = uint8_t(Reserved | (symbol == '#' ? Wall : 0));
						}
					}
				}
				plan.layers.push_back(std::move(upper));
			}
			for (const Building& b : plan.buildings) {
				for (int level = 1; level < b.levels; ++level) {
					const auto blueprint = RotateBlueprint(plan.catalog[b.blueprint], b.rotation);
					const auto [landingX, landingY] = FindBlueprintSymbol(blueprint.rows, level % 2 ? 'a' : 'b');
					Point p { b.origin.x + landingX, b.origin.y + landingY };
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
					control.Row(depth - 1, s.caveDepth);
					Layer cave = MakeCave(s, depth, control);
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
		if ((s.shape != LandShape::Continent && s.shape != LandShape::Archipelago) || (s.settlementLayout != SettlementLayout::Organic && s.settlementLayout != SettlementLayout::Blocks) || s.coastRoughness < 0 || s.coastRoughness > 100 || s.temperatureBias < -5000 || s.temperatureBias > 5000 || s.moistureBias < -5000 || s.moistureBias > 5000 || s.riverWidth < 1 || s.riverWidth > 7 || s.riverWidth % 2 == 0 || s.seaLevel < 2000 || s.seaLevel > 6500 || s.riverCount < 0 || s.riverCount > 64 || s.settlementCount < 0 || s.settlementCount > 32 || s.buildingsPerSettlement < 0 || s.buildingsPerSettlement > 64 || s.buildingLevels < 1 || s.buildingLevels > 4 || s.caveDepth < 0 || s.caveDepth > 8 || s.caveFill < 20 || s.caveFill > 65 || s.roadWidth < 1 || s.roadWidth > 4) {
			error = "World settings are outside supported ranges.";
			return false;
		}
		if (int64_t(s.width) * s.height * (s.buildingLevels + s.caveDepth) > int64_t(kMaxCells) * 2) {
			error = "World layers exceed the 8,388,608-cell planning budget. Reduce dimensions or floors.";
			return false;
		}
		return true;
	}

	bool Build(const Settings& settings, Plan& plan, std::string& error, const BlueprintCatalog* catalog, const GeographyInput* input, const ProgressCallback& progress) {
		plan = Plan();
		if (!ValidateSettings(settings, error)) {
			return false;
		}
		if (input) {
			const size_t count = size_t(settings.width) * settings.height;
			if ((!input->elevation.empty() && input->elevation.size() != count) || (!input->biomes.empty() && input->biomes.size() != count)) {
				error = "World input dimensions do not match the target region.";
				return false;
			}
			for (uint16_t value : input->elevation) {
				if (value > 10000 && value != 65535) {
					error = "World height input is outside 0..10000.";
					return false;
				}
			}
			for (Terrain biome : input->biomes) {
				if (biome != Terrain::Void && (biome < Terrain::Beach || biome > Terrain::Mountain)) {
					error = "Biome hints must be dry natural terrain categories.";
					return false;
				}
			}
		}
		plan.catalog = catalog ? *catalog : DefaultBlueprints();
		if (!ValidateCatalog(plan.catalog, error)) {
			return false;
		}
		plan.settings = settings;
		Control control { progress };
		try {
			control.Stage("Shaping continents", 0, 15);
			BuildGeography(plan, input, control);
			control.Stage("Computing drainage and rivers", 15, 35);
			BuildDrainage(plan, control);
			control.Stage("Classifying biomes", 35, 45);
			ClassifyBiomes(plan, control);
			if (input && !input->biomes.empty()) {
				auto& cells = plan.layers.front().cells;
				for (size_t i = 0; i < cells.size(); ++i) {
					if (input->biomes[i] != Terrain::Void && !IsWater(cells[i].terrain)) {
						cells[i].terrain = input->biomes[i];
					}
				}
			}
			control.Stage("Planning settlements and access roads", 45, 70);
			BuildSettlements(plan, control);
			control.Stage("Building upper floors and connected caves", 70, 99);
			if (!BuildVertical(plan, error, control)) {
				return false;
			}
			control.Stage("World planned", 100, 100);
			return true;
		} catch (const Cancelled&) {
			plan = Plan();
			error = "World generation cancelled.";
			return false;
		}
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
		if (!ValidateCatalog(plan.catalog, error)) {
			fail(error);
			return report;
		}

		std::vector<int> offsets;
		struct Topology {
			int floor, base, count;
			std::vector<int> labels;
		};
		std::vector<Topology> topology;
		int totalComponents = 0;
		for (const auto& layer : plan.layers) {
			if (layer.cells.size() != count) {
				fail("Layer dimensions do not match world dimensions.");
				continue;
			}
			if (std::find(offsets.begin(), offsets.end(), layer.offsetZ) != offsets.end()) {
				fail("Duplicate floor offset.");
			}
			offsets.push_back(layer.offsetZ);
			if (layer.offsetZ < 1 - plan.settings.buildingLevels || layer.offsetZ > plan.settings.caveDepth) {
				fail("Floor offset is outside the requested world layers.");
				continue;
			}
			bool invalidCell = false;
			for (const auto& cell : layer.cells) {
				invalidCell = invalidCell || static_cast<unsigned>(cell.terrain) > static_cast<unsigned>(Terrain::Void) || cell.elevation > 10000 || cell.moisture > 10000 || cell.temperature > 10000 || (cell.flags & ~(Reserved | Wall | Entrance | Connection)) || (layer.offsetZ >= 0 && cell.terrain == Terrain::Void);
			}
			if (invalidCell) {
				fail("Layer has an invalid terrain, flag or normalized value.");
				continue;
			}
			int components = 0;
			auto labels = Components(layer.cells, plan.settings.width, plan.settings.height, components);
			topology.push_back({ layer.offsetZ, totalComponents, components, std::move(labels) });
			totalComponents += components;
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
		for (int z = 1 - plan.settings.buildingLevels; z <= plan.settings.caveDepth; ++z) {
			if (!plan.FindLayer(z)) {
				fail("Requested world floor is missing.");
			}
		}
		if (!report.valid) {
			return report;
		}
		std::vector<int> roots(totalComponents);
		std::iota(roots.begin(), roots.end(), 0);
		auto root = [&](int value) { while (roots[value] != value) { roots[value]=roots[roots[value]];value=roots[value]; } return value; };
		auto component = [&](int floor, Point p) {
			for (const auto& graph : topology) {
				if (graph.floor == floor) {
					const int label = graph.labels[plan.Index(p)];
					return label < 0 ? -1 : graph.base + label;
				}
			}
			return -1;
		};
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
				continue;
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
			const int a = component(link.upperOffsetZ, link.upper), b = component(link.lowerOffsetZ, link.lower);
			if (a >= 0 && b >= 0) {
				roots[root(a)] = root(b);
			}
		}
		std::set<int> surfaceRoots;
		for (const auto& graph : topology) {
			if (graph.floor == 0) {
				for (int i = 0; i < graph.count; ++i) {
					surfaceRoots.insert(root(graph.base + i));
				}
			}
		}
		for (const auto& graph : topology) {
			if (graph.floor != 0) {
				for (int i = 0; i < graph.count; ++i) {
					if (!surfaceRoots.count(root(graph.base + i))) {
						fail("An upper/underground walkable component has no vertical route to the surface.");
						break;
					}
				}
			}
		}
		for (const auto& building : plan.buildings) {
			if (building.blueprint < 0 || building.blueprint >= int(plan.catalog.size()) || building.rotation < 0 || building.rotation > 3 || building.levels < 1 || building.levels > plan.settings.buildingLevels) {
				fail("Invalid building template, rotation or levels.");
				continue;
			}
			if (!plan.Contains(building.origin) || building.width < 3 || building.height < 3 || !plan.Contains({ building.origin.x + building.width - 1, building.origin.y + building.height - 1 }) || !plan.Contains(building.entrance)) {
				fail("Invalid building footprint.");
				continue;
			}
			if (!IsWalkable(surface->cells[plan.Index(building.entrance)]) || !(surface->cells[plan.Index(building.entrance)].flags & Entrance)) {
				fail("Building entrance is blocked.");
			}
			if (building.settlement < 0 || building.settlement >= int(plan.settlements.size()) || !plan.Contains(plan.settlements[building.settlement].center)) {
				fail("Building has an invalid settlement reference.");
				continue;
			}
			const auto center = plan.settlements[building.settlement].center;
			if (component(0, building.entrance) < 0 || component(0, building.entrance) != component(0, center)) {
				fail("Building entrance is disconnected from its settlement plaza.");
			}
		}
		return report;
	}

	uint64_t Fingerprint(const Plan& plan) {
		uint64_t hash = 1469598103934665603ull;
		auto add = [&](uint32_t value) {for(int byte=0;byte<4;++byte){hash^=(value>>(byte*8))&255;hash*=1099511628211ull;} };
		auto string = [&](const std::string& value) {add(uint32_t(value.size()));for(unsigned char byte:value){hash^=byte;hash*=1099511628211ull;} };
		add(AlgorithmVersion);
		add(uint32_t(plan.settings.shape));
		add(plan.settings.seaLevel);
		add(plan.settings.coastRoughness);
		add(plan.settings.temperatureBias);
		add(plan.settings.moistureBias);
		add(plan.settings.riverWidth);
		add(plan.settings.riverCount);
		add(plan.settings.settlementCount);
		add(plan.settings.buildingsPerSettlement);
		add(plan.settings.buildingLevels);
		add(plan.settings.caveDepth);
		add(plan.settings.caveFill);
		add(plan.settings.roadWidth);
		add(uint32_t(plan.settings.settlementLayout));
		add(uint32_t(plan.catalog.size()));
		for (const auto& blueprint : plan.catalog) {
			string(blueprint.id);
			add(blueprint.weight);
			add(blueprint.maxLevels);
			add(blueprint.allowRotation);
			for (const auto* rows : { &blueprint.rows, &blueprint.upperRows, &blueprint.biomes }) {
				add(uint32_t(rows->size()));
				for (const auto& row : *rows) {
					string(row);
				}
			}
			add(uint32_t(blueprint.objects.size()));
			for (const auto& object : blueprint.objects) {
				add(object.x);
				add(object.y);
				add(object.floor);
				add(object.required);
				add(uint32_t(object.brushes.size()));
				for (const auto& name : object.brushes) {
					string(name);
				}
			}
		}
		add(uint32_t(plan.buildings.size()));
		for (const auto& building : plan.buildings) {
			add(building.origin.x);
			add(building.origin.y);
			add(building.width);
			add(building.height);
			add(building.levels);
			add(building.entrance.x);
			add(building.entrance.y);
			add(building.settlement);
			add(building.blueprint);
			add(building.rotation);
		}
		add(uint32_t(plan.settlements.size()));
		for (const auto& town : plan.settlements) {
			add(town.center.x);
			add(town.center.y);
			add(town.radius);
			add(uint32_t(town.biome));
			add(uint32_t(town.buildings.size()));
			for (int index : town.buildings) {
				add(index);
			}
		}
		add(uint32_t(plan.drainage.size()));
		for (int downstream : plan.drainage) {
			add(uint32_t(downstream));
		}
		add(uint32_t(plan.flow.size()));
		for (uint32_t flow : plan.flow) {
			add(flow);
		}
		add(uint32_t(plan.layers.size()));
		add(uint32_t(plan.links.size()));
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
