#include "world_plan.h"

#include <iostream>
#include <limits>
#include <set>
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

static void TestPlanMetadata() {
	Settings settings;
	settings.settlementCount = 1;
	settings.buildingsPerSettlement = 4;
	settings.buildingLevels = 3;
	settings.caveDepth = 1;
	settings.riverCount = 0;
	GeographyInput input;
	input.elevation.assign(256 * 256, 6000);
	input.biomes.assign(256 * 256, Terrain::Grass);
	Plan plan;
	std::string error;
	Check(Build(settings, plan, error, nullptr, &input) && Validate(plan).valid && plan.buildings.size() == 4, "Metadata fixture must contain a complete valid town");
	auto reject = [&](auto change, const char* message) {
		auto invalid = plan;
		change(invalid);
		Check(!Validate(invalid).valid, message);
	};
	reject([](Plan& p) { p.settlements[0].center = { -1, -1 }; }, "Out-of-bounds town centers must fail safely");
	reject([](Plan& p) { p.settlements[0].biome = Terrain::Mountain; }, "Town source biomes must be supported construction habitats");
	reject([](Plan& p) { p.settings.settlementCount = 2; auto town = p.settlements[0]; town.buildings.clear(); p.settlements.push_back(town); }, "Duplicate town centers must fail");
	reject([](Plan& p) { p.settlements[0].buildings[0] = std::numeric_limits<int>::max(); }, "Invalid construction indices must fail without indexing outside the plan");
	reject([](Plan& p) { p.settlements[0].buildings[1] = p.settlements[0].buildings[0]; }, "Duplicate ownership references must fail");
	reject([](Plan& p) { p.settlements[0].buildings.clear(); }, "Unlisted constructions must fail");
	reject([](Plan& p) { p.buildings[0].settlement = 1; }, "Construction ownership must match the town list");
	reject([](Plan& p) { p.buildings[0].width = std::numeric_limits<int>::max(); }, "Excessive footprint dimensions must fail before coordinate addition can overflow");
	reject([](Plan& p) { --p.buildings[0].width; }, "Footprint dimensions must match the rotated template");
	reject([](Plan& p) { p.buildings[0].rotation = 1; p.catalog[p.buildings[0].blueprint].allowRotation = false; }, "Nonrotating templates cannot declare a rotated construction");
	reject([](Plan& p) { p.catalog[p.buildings[0].blueprint].maxLevels = 1; }, "Constructed height cannot exceed its template limit");
	reject([](Plan& p) { p.catalog[p.buildings[0].blueprint].biomes = { "snow" }; }, "Templates cannot be assigned to a disallowed town biome");
	reject([](Plan& p) { p.buildings[1] = p.buildings[0]; }, "Overlapping footprints must fail independently of valid surface navigation");
	reject([](Plan& p) { const auto& link = p.links[0]; p.FindLayer(link.upperOffsetZ)->cells[p.Index(link.upper)].flags &= uint8_t(~Connection); }, "Travel endpoints need connection markers");
	reject([](Plan& p) { p.FindLayer(0)->cells[0].flags |= Connection; }, "Orphaned connection markers must fail");
}

static void TestSettingsMatrix() {
	for (bool diverse : { false, true }) {
		int accepted = 0, multipleTowns = 0;
		for (uint32_t seed = 0; seed < 150; ++seed) {
			Settings s;
			s.diverseSettlements = diverse;
			s.width = 32 + int(seed % 4) * 24;
			s.height = 32 + int((seed / 4) % 4) * 24;
			s.seed = seed;
			s.seaLevel = 2000 + int(seed % 10) * 500;
			s.shape = seed % 2 ? LandShape::Continent : LandShape::Archipelago;
			s.caveDepth = int(seed % 4);
			s.caveEntrances = 1 + int(seed % 8);
			s.buildingLevels = 1 + int(seed % 4);
			s.settlementCount = int(seed % 6);
			s.buildingsPerSettlement = int(seed % 10);
			s.caveFill = 20 + int(seed % 46);
			s.roadWidth = 1 + int(seed % 4);
			s.coastRoughness = int(seed % 101);
			s.relief = seed % 3 ? ReliefStyle::Ridges : ReliefStyle::Smooth;
			s.biomeDetail = int(seed % 101);
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
			++accepted;
			multipleTowns += plan.settlements.size() >= 2;
			if (!report.valid) {
				std::string message = "Settings matrix seed=" + std::to_string(seed) + " diverse=" + std::to_string(diverse) + ": ";
				for (const auto& issue : report.errors) {
					message += issue + "; ";
				}
				throw std::runtime_error(message);
			}
		}
		Check(accepted >= 25 && multipleTowns >= 5, "Each settlement distribution mode must exercise successful terrain and multi-town worlds across the settings matrix");
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

static void TestCaveEntrances() {
	Settings settings;
	settings.width = 128;
	settings.height = 96;
	settings.settlementCount = 0;
	settings.caveDepth = 3;
	settings.caveEntrances = 6;
	GeographyInput input;
	input.elevation.assign(128 * 96, 6000);
	input.biomes.assign(128 * 96, Terrain::Grass);
	Plan plan, repeat;
	std::string error;
	Check(Build(settings, plan, error, nullptr, &input) && Validate(plan).valid, "Multiple surface entrances must connect to the complete cave stack");
	int surfaceLinks = 0;
	for (const auto& link : plan.links) {
		if (link.upperOffsetZ == 0 && link.lowerOffsetZ == 1) {
			++surfaceLinks;
			Check(IsWalkable(plan.FindLayer(0)->cells[plan.Index(link.upperArrival)]) && IsWalkable(plan.FindLayer(1)->cells[plan.Index(link.lowerArrival)]), "Every cave entrance must retain walkable adjacent arrivals");
		}
	}
	Check(surfaceLinks == 6, "A spacious dry region must fit the requested separated entrances");
	for (int z = 1; z <= 3; ++z) Check(CountConnected(*plan.FindLayer(z), settings.width, settings.height) == 1, "Every added cave arrival must join its cave's walkable component");
	Check(Build(settings, repeat, error, nullptr, &input) && Fingerprint(repeat) == Fingerprint(plan), "Distributed cave sites must be deterministic");
	auto invalid = plan;
	invalid.settings.caveEntrances = 1;
	Check(!Validate(invalid).valid, "Plan validation must reject cave entrance counts above the requested cap");
	settings.caveEntrances = 17;
	Check(!Build(settings, repeat, error), "Cave entrance limit must be checked before allocation");
}

static void TestReliefAndBiomeDetail() {
	Settings settings;
	settings.width = settings.height = 256;
	settings.settlementCount = 0;
	settings.caveDepth = 1;
	Plan smooth, ridged, repeat;
	std::string error;
	Check(Build(settings, smooth, error), "Smooth relief fixture must build");
	settings.relief = ReliefStyle::Ridges;
	settings.biomeDetail = 75;
	Check(Build(settings, ridged, error) && Validate(ridged).valid, "Ridged terrain, drainage and caves must remain valid");
	Check(Build(settings, repeat, error) && Fingerprint(repeat) == Fingerprint(ridged), "Ridged terrain and basin breaching must be deterministic");
	auto mountains = [](const Plan& plan) { size_t count = 0; for (const auto& cell : plan.FindLayer(0)->cells) count += cell.terrain == Terrain::Mountain; return count; };
	Check(mountains(ridged) > 0 && mountains(ridged) < mountains(smooth), "The public seed must retain mountains while opening more valleys than smooth central highlands");
	GeographyInput holes;
	holes.elevation.assign(256 * 256, 65535);
	Check(Build(settings, repeat, error, nullptr, &holes) && Fingerprint(repeat) == Fingerprint(ridged), "Fully transparent height input must preserve procedural ridge erosion");
	GeographyInput authored;
	authored.elevation.assign(256 * 256, 6000);
	Check(Build(settings, ridged, error, nullptr, &authored), "Authored heightmaps must retain control in ridged mode");
	for (int y = 1; y < 255; ++y) for (int x = 1; x < 255; ++x) Check(ridged.FindLayer(0)->cells[size_t(y) * 256 + x].elevation == 6000, "Basin breaching must not alter authored height pixels");
	settings.biomeDetail = 0;
	Check(Build(settings, smooth, error, nullptr, &authored), "Undetailed biome fixture must build");
	size_t changed = 0;
	for (size_t i = 0; i < smooth.FindLayer(0)->cells.size(); ++i) changed += smooth.FindLayer(0)->cells[i].moisture != ridged.FindLayer(0)->cells[i].moisture;
	Check(changed > 10000, "Biome detail must change local climate fields even with fixed elevation");
	settings.biomeDetail = 101;
	Check(!Build(settings, repeat, error), "Out-of-range local biome variation must fail");
	settings.biomeDetail = 0;
	settings.relief = static_cast<ReliefStyle>(99);
	Check(!Build(settings, repeat, error), "Unknown relief modes must fail");
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

static void TestDiverseSettlements() {
	Settings settings;
	settings.settlementCount = 4;
	settings.buildingsPerSettlement = 0;
	settings.riverCount = 0;
	settings.diverseSettlements = true;
	GeographyInput input;
	input.elevation.assign(256 * 256, 6000);
	input.biomes.resize(256 * 256);
	const Terrain habitats[] = { Terrain::Grass, Terrain::Forest, Terrain::Desert, Terrain::Snow };
	for (int y = 0; y < 256; ++y) {
		for (int x = 0; x < 256; ++x) input.biomes[size_t(y) * 256 + x] = habitats[(y >= 128 ? 2 : 0) + (x >= 128 ? 1 : 0)];
	}
	Plan plan, repeat;
	std::string error;
	for (uint32_t seed : { 0u, 42u, 1337u }) {
		settings.seed = seed;
		Check(Build(settings, plan, error, nullptr, &input) && Validate(plan).valid, "Diverse settlement placement must remain valid");
		std::set<Terrain> selected;
		for (const auto& town : plan.settlements) selected.insert(town.biome);
		Check(selected.size() == 4, "Separated suitable habitats must each receive a settlement before repeats");
		Check(Build(settings, repeat, error, nullptr, &input) && Fingerprint(repeat) == Fingerprint(plan), "Diverse site selection must be deterministic");
	}
	settings.settlementCount = 6;
	input.biomes.assign(256 * 256, Terrain::Grass);
	Check(Build(settings, plan, error, nullptr, &input) && plan.settlements.size() == 6 && Validate(plan).valid, "A single available biome must still fill the requested settlement count");
	settings.diverseSettlements = false;
	Check(Build(settings, repeat, error, nullptr, &input) && repeat.settlements.size() == plan.settlements.size(), "Fitness mode must remain available");
	for (size_t i = 0; i < plan.settlements.size(); ++i) Check(plan.settlements[i].center == repeat.settlements[i].center, "Single-habitat fallback must retain the original fitness order");
	settings = Settings();
	settings.width = settings.height = 128;
	settings.settlementLayout = SettlementLayout::Blocks;
	settings.buildingLevels = settings.caveDepth = 2;
	Check(Build(settings, plan, error) && Fingerprint(plan) == 3278022085088056009ull, "Default-off diversity must preserve the pre-feature algorithm fingerprint");
}

static void TestLayeredBuildings() {
	Blueprint blueprint;
	blueprint.id = "layered";
	blueprint.maxLevels = 3;
	blueprint.allowRotation = false;
	blueprint.rows = { "#########", "#.......#", "#.a...b.#", "#.......#", "#.......#", "#.......#", "#.......#", "#.......#", "####+####" };
	auto middle = blueprint.rows;
	middle.back() = "#########";
	middle[4] = "#..#.#..#";
	blueprint.upperFloors = { middle, { "         ", " ####### ", " #a...b# ", " #.....# ", " #.....# ", " #.....# ", " #.....# ", " ####### ", "         " } };
	Settings settings;
	settings.width = settings.height = 128;
	settings.settlementCount = 1;
	settings.buildingsPerSettlement = 1;
	settings.buildingLevels = 3;
	settings.riverCount = 0;
	GeographyInput input;
	input.elevation.assign(128 * 128, 6000);
	input.biomes.assign(128 * 128, Terrain::Grass);
	std::string error;
	for (int rotation = 0; rotation < 4; ++rotation) {
		BlueprintCatalog catalog { RotateBlueprint(blueprint, rotation) };
		Plan plan;
		Check(Build(settings, plan, error, &catalog, &input) && Validate(plan).valid && plan.buildings.size() == 1, "Each rotated floor-specific construction must remain connected");
		Check(plan.links.size() == 2, "Three-floor buildings must retain both vertical routes");
		const auto& building = plan.buildings.front();
		for (int level = 1; level <= 2; ++level) {
			size_t occupied = 0;
			for (const auto& cell : plan.FindLayer(-level)->cells) occupied += cell.terrain != Terrain::Void;
			Check(occupied == size_t(level == 1 ? 81 : 49), "Upper floors must use their own stepped footprint rather than a repeated lower floor");
			for (int y = 0; y < building.height; ++y) {
				for (int x = 0; x < building.width; ++x) {
					const char symbol = BlueprintRowsForFloor(catalog[0], level)[y][x];
					const auto& cell = plan.FindLayer(-level)->cells[plan.Index({ building.origin.x + x, building.origin.y + y })];
					Check((symbol == ' ') == (cell.terrain == Terrain::Void), "Explicit voids must survive plan stamping at every rotation");
				}
			}
		}
		auto changed = plan;
		changed.catalog[0].upperFloors[0][1][1] = '#';
		Check(Fingerprint(changed) != Fingerprint(plan), "Fingerprint must cover every explicit upper floor");
		const auto restored = RotateBlueprint(catalog[0], 4 - rotation);
		Check(restored.upperFloors == blueprint.upperFloors, "Four rotations must restore all explicit upper floors");
	}
	auto invalid = blueprint;
	invalid.upperFloors.pop_back();
	Check(!ValidateBlueprint(invalid, error), "Incomplete explicit floor lists must fail");
	invalid = blueprint;
	invalid.upperRows = middle;
	Check(!ValidateBlueprint(invalid, error), "Repeated and explicit upper definitions cannot be mixed");
	invalid = blueprint;
	invalid.upperFloors[1][2] = " #.a.b.# ";
	Check(!ValidateBlueprint(invalid, error), "Landings must align in every explicitly defined floor");
	invalid = blueprint;
	invalid.objects.push_back({ 1, 1, 1, { "chair" }, true });
	Check(ValidateBlueprint(invalid, error), "Furniture anchors must use the selected floor layout");
	invalid.objects.back().floor = 2;
	Check(!ValidateBlueprint(invalid, error), "Furniture cannot float in another floor's void or wall");
}

static void TestConstructionMinimums() {
	Settings settings;
	settings.width = settings.height = 256;
	settings.settlementCount = 2;
	settings.buildingsPerSettlement = 8;
	settings.riverCount = 0;
	settings.settlementLayout = SettlementLayout::Blocks;
	GeographyInput input;
	input.elevation.assign(256 * 256, 6000);
	input.biomes.assign(256 * 256, Terrain::Grass);
	auto catalog = DefaultBlueprints();
	catalog.resize(3);
	for (auto& blueprint : catalog) blueprint.weight = 1000;
	catalog[1].weight = 1;
	catalog[1].minPerSettlement = catalog[1].maxPerSettlement = 1;
	catalog[2].weight = 1;
	catalog[2].minPerSettlement = catalog[2].maxPerSettlement = 2;
	Plan plan;
	std::string error;
	for (uint32_t seed : { 0u, 1337u, 0xffffffffu }) {
		settings.seed = seed;
		Check(Build(settings, plan, error, &catalog, &input) && Validate(plan).valid, "Required low-weight templates must precede optional construction");
		Check(plan.settlements.size() == 2, "Minimum fixture must contain two towns");
		for (int t = 0; t < 2; ++t) {
			int first = 0, second = 0;
			for (const auto& building : plan.buildings) {
				if (building.settlement == t) {
					first += building.blueprint == 1;
					second += building.blueprint == 2;
				}
			}
			Check(first == 1 && second == 2, "Min/max requirements must reset and hold independently in each settlement");
		}
	}
	auto altered = plan;
	altered.catalog[0].minPerSettlement = 8;
	Check(!Validate(altered).valid && Fingerprint(altered) != Fingerprint(plan), "Validation and fingerprint must independently reflect missing required templates");
	settings.buildingsPerSettlement = 2;
	Check(!Build(settings, plan, error, &catalog, &input) && plan.layers.empty() && error.find("budget") != std::string::npos, "Oversubscribed construction requirements must reject and discard a partial plan");
	catalog[1].biomes = { "desert" };
	catalog[2].biomes = { "desert" };
	Check(Build(settings, plan, error, &catalog, &input) && Validate(plan).valid, "Minimums for other biomes must not apply to grass settlements");
	catalog.resize(1);
	catalog[0].minPerSettlement = 64;
	settings.buildingsPerSettlement = 64;
	settings.width = settings.height = 64;
	settings.settlementCount = 1;
	input.elevation.assign(64 * 64, 6000);
	input.biomes.assign(64 * 64, Terrain::Grass);
	Check(!Build(settings, plan, error, &catalog, &input) && plan.layers.empty() && error.find("cannot fit") != std::string::npos, "Terrain failure must report the unmet construction ID and discard all partial tiles");
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
	altered.catalog[0].floorMaterials.push_back({ "wood", "stone" });
	Check(Fingerprint(altered) != Fingerprint(observed), "Fingerprint must include per-floor material bindings");
	altered = observed;
	altered.catalog[0].role = "civic";
	Check(Fingerprint(altered) != Fingerprint(observed), "Fingerprint must include construction roles");
	altered = observed;
	altered.catalog[0].maxPerSettlement = 1;
	Check(Fingerprint(altered) != Fingerprint(observed), "Fingerprint must include construction quotas");
}

static void TestIndependentCavesAndShape() {
	Settings settings;
	settings.width = settings.height = 64;
	settings.settlementCount = settings.riverCount = 0;
	settings.caveDepth = 2;
	Plan original, changed, replay;
	std::string error;
	Check(Build(settings, original, error), "Independent cave fixture must build");
	settings.caveSeed = 98765;
	Check(Build(settings, changed, error) && Build(settings, replay, error), "Independent caves must build deterministically");
	Check(Fingerprint(changed) == Fingerprint(replay) && Fingerprint(original) != Fingerprint(changed), "Cave seed must change a reproducible world");
	bool different = false;
	for (size_t i = 0; i < original.FindLayer(0)->cells.size(); ++i) {
		Check(original.FindLayer(0)->cells[i].elevation == changed.FindLayer(0)->cells[i].elevation, "Cave seed must preserve surface geography");
		different |= original.FindLayer(1)->cells[i].terrain != changed.FindLayer(1)->cells[i].terrain;
	}
	Check(different && Validate(changed).valid, "Independent caves must vary actual tiles and preserve connectivity");
	settings.caveDepth = 0;
	GeographyInput input;
	input.landMask.assign(64 * 64, 0);
	for (int y = 16; y < 48; ++y) for (int x = 16; x < 48; ++x) input.landMask[y * 64 + x] = 1;
	Check(Build(settings, changed, error, nullptr, &input), "Painted island must build");
	for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) {
		const auto& cell = changed.FindLayer(0)->cells[y * 64 + x];
		Check(input.landMask[y * 64 + x] ? cell.elevation > settings.seaLevel : cell.terrain == Terrain::Ocean, "Black mask pixels must stay ocean and white pixels must start above sea level");
	}
	input.elevation.assign(64 * 64, 0);
	Check(Build(settings, changed, error, nullptr, &input), "Mask must raise authored ocean into land");
	Check(changed.FindLayer(0)->cells[16 * 64 + 32].terrain == Terrain::Beach, "Newly painted shores must retain natural beach classification");
	input.landMask[100] = 2;
	Check(!Build(settings, changed, error, nullptr, &input) && changed.layers.empty(), "Invalid land mask values must discard the plan");
}

int main() {
	try {
		TestIndependentCavesAndShape();
		TestClimateAndCoast();
		TestReliefAndBiomeDetail();
		TestCityBlocks();
		TestConstructionMinimums();
		TestLayeredBuildings();
		TestDiverseSettlements();
		TestCancellation();
		TestCaves();
		TestCaveEntrances();
		TestWorlds();
		TestValidation();
		TestPlanMetadata();
		TestSettingsMatrix();
		std::cout << "PASS: world geography, drainage, cave topology, vertical landings, settings budgets and deterministic plans\n";
		return 0;
	} catch (const std::exception& e) {
		std::cerr << "FAIL: " << e.what() << '\n';
		return 1;
	}
}
