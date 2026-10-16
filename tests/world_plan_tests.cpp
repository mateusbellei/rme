#include "world_plan.h"

#include <iostream>
#include <queue>
#include <stdexcept>
#include <unordered_set>

using namespace WorldGen;
static void Check(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static int CountConnected(const Layer& layer, int width, int height) {
	std::vector<bool> seen(layer.cells.size());
	int components = 0;
	for (int i = 0; i < int(layer.cells.size()); ++i) {
		if (seen[i] || !IsWalkable(layer.cells[i])) {
			continue;
		}
		++components;
		std::queue<int> queue;
		queue.push(i);
		seen[i] = true;
		while (!queue.empty()) {
			const int p = queue.front();
			queue.pop();
			for (const auto [dx, dy] : { std::pair { 1, 0 }, std::pair { -1, 0 }, std::pair { 0, 1 }, std::pair { 0, -1 } }) {
				int x = p % width + dx, y = p / width + dy;
				if (x < 0 || y < 0 || x >= width || y >= height) {
					continue;
				}
				int n = y * width + x;
				if (!seen[n] && IsWalkable(layer.cells[n])) {
					seen[n] = true;
					queue.push(n);
				}
			}
		}
	}
	return components;
}

static void TestCaves() {
	const int w = 33, h = 25;
	Layer layer;
	layer.cells.resize(w * h);
	for (auto& c : layer.cells) {
		c.terrain = Terrain::CaveWall;
	}
	for (int y = 2; y < h - 2; y += 5) {
		for (int x = 2; x < w - 2; x += 5) {
			layer.cells[y * w + x].terrain = Terrain::CaveFloor;
		}
	}
	Check(CountConnected(layer, w, h) > 10, "Fixture must have disconnected rooms");
	Check(ConnectCave(layer.cells, w, h) == 1, "Cave connector must join all rooms");
	Check(CountConnected(layer, w, h) == 1, "Independent flood fill must reach all carved rooms");
	for (int x = 0; x < w; ++x) {
		Check(layer.cells[x].terrain == Terrain::CaveWall && layer.cells[(h - 1) * w + x].terrain == Terrain::CaveWall, "Cave boundary must remain sealed");
	}
	for (int y = 0; y < h; ++y) {
		Check(layer.cells[y * w].terrain == Terrain::CaveWall && layer.cells[y * w + w - 1].terrain == Terrain::CaveWall, "Cave boundary must remain sealed");
	}
	Check(ConnectCave(layer.cells, w, h) == 1, "Connecting a connected cave must be stable");
}

static void TestWorlds() {
	for (uint32_t seed : { 1u, 42u, 1337u, 0xffffffffu }) {
		for (LandShape shape : { LandShape::Continent, LandShape::Archipelago }) {
			Settings s;
			s.width = 96;
			s.height = 80;
			s.seed = seed;
			s.shape = shape;
			s.caveDepth = 2;
			s.buildingLevels = 3;
			Plan plan, repeat;
			std::string error;
			Check(Build(s, plan, error), "World build must succeed");
			const Report report = Validate(plan);
			if (!report.valid) {
				for (const auto& message : report.errors) {
					std::cerr << message << '\n';
				}
			}
			Check(report.valid, "Generated world must pass semantic validation");
			Check(report.landTiles > 0 && report.waterTiles > 0, "World must have land and ocean");
			Check(plan.layers.size() == 5, "Requested building/cave floors must exist");
			for (int z = 1; z <= 2; ++z) {
				Check(CountConnected(*plan.FindLayer(z), s.width, s.height) == 1, "Every cave floor must be connected");
			}
			for (int i = 0; i < int(plan.drainage.size()); ++i) {
				int steps = 0;
				for (int p = i; p >= 0; p = plan.drainage[p]) {
					Check(++steps <= int(plan.drainage.size()), "Drainage must be acyclic and reach an outlet");
				}
			}
			Check(Build(s, repeat, error) && Fingerprint(plan) == Fingerprint(repeat), "Same seed/settings must reproduce all semantic layers");
			s.seed ^= 17;
			Check(Build(s, repeat, error) && Fingerprint(plan) != Fingerprint(repeat), "Changing seed must change world");
		}
	}
}

static void TestValidation() {
	Settings s;
	std::string error;
	Plan plan;
	for (int invalid : { 0, -1, 31, 4097 }) {
		s.width = invalid;
		Check(!Build(s, plan, error) && !error.empty(), "Invalid dimensions must fail before allocation");
	}
	s = Settings();
	s.width = 4096;
	s.height = 4096;
	Check(!Build(s, plan, error), "Surface budget must be enforced");
	s = Settings();
	s.width = 2048;
	s.height = 2048;
	s.caveDepth = 8;
	Check(!Build(s, plan, error), "Layer budget must be enforced");
	s = Settings();
	s.caveDepth = 9;
	Check(!Build(s, plan, error), "Invalid depth must fail");
	s = Settings();
	s.width = 64;
	s.height = 64;
	s.caveDepth = 1;
	Check(Build(s, plan, error), "Fixture build");
	plan.links[0].upper.x = -1;
	Check(!Validate(plan).valid, "Out-of-bounds vertical links must be rejected");
	Check(Build(s, plan, error), "Fixture rebuild");
	plan.layers[1].cells.clear();
	Check(!Validate(plan).valid, "Malformed layer must fail safely");
	Check(Build(s, plan, error), "Fixture rebuild");
	plan.layers[0].cells[0].terrain = static_cast<Terrain>(255);
	Check(!Validate(plan).valid, "Unknown terrain values must fail before material indexing");
	Check(Build(s, plan, error), "Fixture rebuild");
	plan.links.clear();
	Check(!Validate(plan).valid, "Underground floors without a route to the surface must fail");
}

static void TestSettingsMatrix() {
	for (uint32_t seed = 0; seed < 150; ++seed) {
		Settings s;
		s.width = 32 + int(seed % 4) * 24;
		s.height = 32 + int((seed / 4) % 4) * 24;
		s.seed = seed;
		s.seaLevel = 2000 + int(seed % 10) * 500;
		s.shape = seed % 2 ? LandShape::Continent : LandShape::Archipelago;
		s.caveDepth = int(seed % 4);
		s.buildingLevels = 1 + int(seed % 4);
		s.settlementCount = int(seed % 6);
		s.buildingsPerSettlement = int(seed % 10);
		s.caveFill = 20 + int(seed % 46);
		s.roadWidth = 1 + int(seed % 4);
		s.coastRoughness = int(seed % 101);
		s.temperatureBias = int(seed % 11) * 1000 - 5000;
		s.moistureBias = int(seed % 9) * 1000 - 4000;
		s.riverWidth = 1 + int(seed % 4) * 2;
		s.settlementLayout = seed % 3 ? SettlementLayout::Organic : SettlementLayout::Blocks;
		Plan plan;
		std::string error;
		if (!Build(s, plan, error)) {
			Check(!error.empty(), "Rejected settings/landing must explain the failure");
			continue;
		}
		const auto report = Validate(plan);
		if (!report.valid) {
			std::string message = "Settings matrix seed=" + std::to_string(seed) + ": ";
			for (const auto& issue : report.errors) {
				message += issue + "; ";
			}
			throw std::runtime_error(message);
		}
	}
}

static void TestClimateAndCoast() {
	Settings settings;
	settings.width = settings.height = 128;
	settings.settlementCount = 0;
	settings.riverCount = 0;
	GeographyInput input;
	input.elevation.assign(128 * 128, 6000);
	std::string error;
	Plan cold, hot, dry, wet, calm, rugged;
	settings.temperatureBias = -5000;
	Check(Build(settings, cold, error, nullptr, &input), "Cold climate must build");
	settings.temperatureBias = 5000;
	Check(Build(settings, hot, error, nullptr, &input), "Hot climate must build");
	auto count = [](const Plan& plan, Terrain terrain) {size_t result=0;for(const auto& cell:plan.FindLayer(0)->cells){result+=cell.terrain==terrain;
}return result; };
	Check(count(cold, Terrain::Snow) > count(hot, Terrain::Snow), "Temperature adjustment must change snow coverage");
	settings.temperatureBias = 0;
	settings.moistureBias = -5000;
	Check(Build(settings, dry, error, nullptr, &input), "Dry climate must build");
	settings.moistureBias = 5000;
	Check(Build(settings, wet, error, nullptr, &input), "Wet climate must build");
	Check(count(wet, Terrain::Forest) > count(dry, Terrain::Forest), "Moisture adjustment must change forest coverage");
	settings.moistureBias = 0;
	settings.coastRoughness = 0;
	Check(Build(settings, calm, error), "Calm coast must build");
	settings.coastRoughness = 100;
	Check(Build(settings, rugged, error), "Rugged coast must build");
	Check(Fingerprint(calm) != Fingerprint(rugged) && Validate(rugged).valid, "Domain-warped coast must alter geography while preserving invariants");
	settings.riverWidth = 2;
	Check(!Build(settings, wet, error), "River width must be an odd supported diameter");
	settings = Settings();
	settings.width = settings.height = 256;
	settings.settlementCount = 0;
	settings.riverWidth = 1;
	Plan narrow, wide;
	Check(Build(settings, narrow, error), "Narrow rivers must build");
	settings.riverWidth = 7;
	Check(Build(settings, wide, error), "Wide rivers must build");
	Check(count(wide, Terrain::River) > count(narrow, Terrain::River) && Validate(wide).valid, "Runoff-dependent width must expand river channels without invalidating drainage");
}

static void TestCityBlocks() {
	Settings settings;
	settings.width = settings.height = 256;
	settings.riverCount = 0;
	settings.settlementCount = 1;
	settings.buildingsPerSettlement = 32;
	settings.buildingLevels = 3;
	settings.settlementLayout = SettlementLayout::Blocks;
	GeographyInput input;
	input.elevation.assign(256 * 256, 6000);
	input.biomes.assign(256 * 256, Terrain::Grass);
	Plan plan;
	std::string error;
	Check(Build(settings, plan, error, nullptr, &input), "City blocks on a dry plain must build");
	Check(Validate(plan).valid, "City blocks must preserve building/plaza and vertical connectivity");
	Check(plan.buildings.size() >= 24, "Street blocks must support a substantial city beyond the eight village directions");
	for (size_t a = 0; a < plan.buildings.size(); ++a) {
		for (size_t b = a + 1; b < plan.buildings.size(); ++b) {
			const auto& p = plan.buildings[a];
			const auto& q = plan.buildings[b];
			Check(p.origin.x + p.width < q.origin.x || q.origin.x + q.width < p.origin.x || p.origin.y + p.height < q.origin.y || q.origin.y + q.height < p.origin.y, "City lots must preserve setbacks");
		}
	}
	Plan repeat;
	Check(Build(settings, repeat, error, nullptr, &input) && Fingerprint(repeat) == Fingerprint(plan), "City street blocks must be deterministic");
	// Custom templates may put their original entrance on any exterior side.
	auto catalog = DefaultBlueprints();
	for (auto& b : catalog) {
		b = RotateBlueprint(b, 2);
	}
	Check(Build(settings, plan, error, &catalog, &input) && Validate(plan).valid, "North-facing source doors must rotate toward their actual street access");
	catalog = DefaultBlueprints();
	for (size_t i = 0; i < catalog.size(); ++i) {
		catalog[i].maxPerSettlement = i == 1 ? 0 : 1;
		catalog[i].weight = i == 1 ? 1 : 1000;
		catalog[i].role = i == 1 ? "residential" : "civic";
	}
	Check(Build(settings, plan, error, &catalog, &input) && Validate(plan).valid, "Template caps must preserve a valid connected city");
	std::vector<int> counts(catalog.size(), 0);
	for (const auto& building : plan.buildings) ++counts[building.blueprint];
	Check(plan.buildings.size() >= 24 && counts[0] <= 1 && counts[2] <= 1 && counts[3] <= 1 && counts[1] >= 21, "Capped civic templates must leave remaining city lots for unbounded residences");
	auto invalidLimit = plan;
	invalidLimit.catalog[1].maxPerSettlement = 1;
	Check(!Validate(invalidLimit).valid, "Validation must independently reject plans exceeding template caps");
	catalog.resize(1);
	Check(Build(settings, plan, error, &catalog, &input) && plan.buildings.size() == 1 && !plan.warnings.empty(), "An exhausted catalog must report an underfilled settlement without exceeding caps");
}

static void TestCancellation() {
	Settings settings;
	settings.width = settings.height = 96;
	settings.caveDepth = 2;
	Plan expected;
	std::string error;
	Check(Build(settings, expected, error), "Cancellation fixture must build");
	for (const char* stage : { "Shaping continents", "Computing drainage and rivers", "Classifying biomes", "Planning settlements and access roads", "Building upper floors and connected caves", "World planned" }) {
		Plan cancelled;
		bool visited = false;
		Check(!Build(settings, cancelled, error, nullptr, nullptr, [&](const char* current, int percent) {Check(percent>=0 && percent<=100,"Progress must be normalized");if(std::string(current)==stage){visited=true;return false;}return true; }), "Cancellation must stop planning");
		Check(visited && cancelled.layers.empty() && error == "World generation cancelled.", "Cancelled partial plans must be discarded with an explicit outcome");
	}
	int last = -1;
	Plan observed;
	Check(Build(settings, observed, error, nullptr, nullptr, [&](const char*, int percent) {Check(percent>=last,"Planning progress must be monotonic");last=percent;return true; }), "Progress observation must allow planning");
	Check(last == 100 && Fingerprint(observed) == Fingerprint(expected), "Progress must not affect the generated plan");
	auto altered = observed;
	altered.catalog[0].id = "different-template";
	Check(Fingerprint(altered) != Fingerprint(observed), "Fingerprint must include template identity even if terrain cells match");
	altered = observed;
	altered.catalog[0].objects.push_back({ 2, 2, 0, { "table" }, false });
	Check(Fingerprint(altered) != Fingerprint(observed), "Fingerprint must include authored furniture and client brush bindings");
	altered = observed;
	altered.catalog[0].floorBrush = "custom floor";
	Check(Fingerprint(altered) != Fingerprint(observed), "Fingerprint must include exact construction material bindings");
	altered = observed;
	altered.catalog[0].role = "civic";
	Check(Fingerprint(altered) != Fingerprint(observed), "Fingerprint must include construction roles");
	altered = observed;
	altered.catalog[0].maxPerSettlement = 1;
	Check(Fingerprint(altered) != Fingerprint(observed), "Fingerprint must include construction quotas");
}

int main() {
	try {
		TestClimateAndCoast();
		TestCityBlocks();
		TestCancellation();
		TestCaves();
		TestWorlds();
		TestValidation();
		TestSettingsMatrix();
		std::cout << "PASS: world geography, drainage, cave topology, vertical landings, settings budgets and deterministic plans\n";
		return 0;
	} catch (const std::exception& e) {
		std::cerr << "FAIL: " << e.what() << '\n';
		return 1;
	}
}
