#include "procedural_recipe.h"
#include "mt_rand.h"
#include "world_catalog.h"
#include "world_inputs.h"
#include "world_export.h"
#include "json.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

static void Check(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

static void TestRecipes() {
	const std::filesystem::path path = std::filesystem::temp_directory_path() / "rme-world-recipe-test.json";
	const wxString file = wxString::FromUTF8(path.string());
	GenerationSpec original;
	original.source = GenerationSource::WorldPlan;
	original.seed = 0xFFFFFFFFu;
	original.region.originX = 200;
	original.region.originY = 300;
	original.region.width = 128;
	original.region.height = 96;
	original.world.settings.shape = WorldGen::LandShape::Archipelago;
	original.world.settings.seaLevel = 4000;
	original.world.settings.coastRoughness = 80;
	original.world.settings.relief = WorldGen::ReliefStyle::Ridges;
	original.world.settings.biomeDetail = 67;
	original.world.settings.temperatureBias = -3200;
	original.world.settings.moistureBias = 2100;
	original.world.settings.riverWidth = 5;
	original.world.settings.riverCount = 6;
	original.world.settings.settlementCount = 4;
	original.world.settings.diverseSettlements = true;
	original.world.settings.buildingsPerSettlement = 5;
	original.world.settings.buildingLevels = 3;
	original.world.settings.caveDepth = 2;
	original.world.settings.caveEntrances = 5;
	original.world.settings.caveFill = 40;
	original.world.settings.roadWidth = 3;
	original.world.settings.settlementLayout = WorldGen::SettlementLayout::Blocks;
	original.world.connectionItemId = 1387;
	original.world.wallBrushName = "Stone walls";
	original.world.catalogPath = "custom-buildings.json";
	original.world.heightmapPath = "height.png";
	original.world.biomeHintsPath = "biomes.png";
	original.world.protectExisting = false;
	original.imageMask.legendPath = "custom-world.json";
	wxString error;
	Check(ProceduralRecipe::Save(file, original, error), "World recipe must save");
	GenerationSpec loaded;
	Check(ProceduralRecipe::Load(file, loaded, error), "World recipe must load");
	Check(loaded.source == original.source && loaded.seed == original.seed, "Full uint32 seed and world source must round-trip");
	Check(loaded.region.originX == 200 && loaded.region.originY == 300 && loaded.region.width == 128 && loaded.region.height == 96, "Target origin and dimensions must round-trip");
	Check(loaded.world.settings.shape == original.world.settings.shape && loaded.world.settings.seaLevel == 4000 && loaded.world.settings.riverCount == 6, "World geography must round-trip");
	Check(loaded.world.settings.coastRoughness == 80 && loaded.world.settings.temperatureBias == -3200 && loaded.world.settings.moistureBias == 2100 && loaded.world.settings.riverWidth == 5, "Coast, climate and river width must round-trip");
	Check(loaded.world.settings.settlementCount == 4 && loaded.world.settings.buildingsPerSettlement == 5 && loaded.world.settings.buildingLevels == 3, "World settlement settings must round-trip");
	Check(loaded.world.settings.diverseSettlements, "Settlement habitat preference must round-trip");
	Check(loaded.world.settings.caveDepth == 2 && loaded.world.settings.caveFill == 40 && loaded.world.settings.roadWidth == 3, "Caves and road settings must round-trip");
	Check(loaded.world.settings.settlementLayout == WorldGen::SettlementLayout::Blocks, "City street layout must round-trip");
	Check(loaded.world.settings.caveEntrances == 5, "Surface cave entrance count must round-trip");
	Check(loaded.world.settings.relief == WorldGen::ReliefStyle::Ridges && loaded.world.settings.biomeDetail == 67, "Relief and local biome variation must round-trip");
	Check(loaded.world.connectionItemId == 1387 && loaded.world.wallBrushName == "Stone walls" && !loaded.world.protectExisting, "Client/protection settings must round-trip");
	Check(loaded.world.catalogPath == original.world.catalogPath, "Construction catalog path must round-trip");
	Check(loaded.world.heightmapPath == original.world.heightmapPath && loaded.world.biomeHintsPath == original.world.biomeHintsPath, "World raster paths must round-trip");
	{
		std::ifstream input(path, std::ios::binary);
		const std::string saved((std::istreambuf_iterator<char>(input)), {});
		input.close();
		std::ofstream(path, std::ios::binary) << "\xef\xbb\xbf" << saved;
		Check(ProceduralRecipe::Load(file, loaded, error) && loaded.seed == original.seed, "UTF-8 BOM recipes must preserve their settings");
		auto invalidWall = original;
		invalidWall.world.wallBrushName = wxString::FromUTF8(std::string("stone\0other", 11));
		Check(!ProceduralRecipe::Save(file, invalidWall, error), "Saving a NUL wall binding must fail before truncating its name");
		Check(ProceduralRecipe::Load(file, loaded, error) && loaded.world.wallBrushName == original.world.wallBrushName, "Invalid wall binding must preserve the existing recipe file");
		std::ofstream(path, std::ios::binary) << "{\"legend\":\"" << char(255) << "\"}";
		Check(!ProceduralRecipe::Load(file, loaded, error), "Invalid UTF-8 paths must not silently become blank defaults");
	}
	const auto unicodePath = std::filesystem::temp_directory_path() / std::filesystem::path(u8"rme-world-\u00E7-\u6D4B\u8BD5.json");
	const auto encodedPath = unicodePath.u8string();
	const wxString unicodeFile = wxString::FromUTF8(std::string(encodedPath.begin(), encodedPath.end()));
	Check(ProceduralRecipe::Save(unicodeFile, original, error) && ProceduralRecipe::Load(unicodeFile, loaded, error), "Recipe paths must support Unicode on Windows and Linux");
	std::filesystem::remove(unicodePath);
	for (const char* malformed : {
			 R"({"version":99})", R"({"source":"world","world":{"caveEntrances":17}})", R"({"source":"world","world":{"relief":"unknown"}})", R"({"source":"world","world":{"biomeDetail":101}})", R"({"world":{"wallBrush":"stone\u0000other"}})", "{} trailing", "{} {}", R"({"pathBase":"unknown"})", R"({"legend":"bad\u0000path"})", R"({"world":{"heightmap":"bad\u0000path"}})", R"({"source":false})", R"({"preset":"typo"})", R"({"region":[]})", R"({"seed":1,"useSelection":0})", R"({"referenceWeight":101})", R"({"doodads":{"density":4294967296}})", R"({"doodads":{"enabled":1}})", R"({"pipeline":{"borderize":"yes"}})", R"({"seed":4294967296})", R"({"seed":1.5})",
			 R"({"source":"world","world":{"caveDepth":9}})",
			 R"({"source":"world","world":{"seaLevel":4294967296}})",
			 R"({"source":"world","world":{"shape":"typo"}})",
			 R"({"source":"world","world":{"protectExisting":0}})",
			 R"({"source":"world","world":{"diverseSettlements":1}})",
			 R"({"source":"world","world":{"wallBrush":false}})",
			 R"({"region":{"width":4294967296}})", R"({"world":null})", R"({"world":{"algorithmVersion":99}})", R"({"world":{"heightmap":42}})" }) {
		std::ofstream output(path);
		output << malformed;
		output.close();
		GenerationSpec before;
		before.seed = 12345;
		Check(!ProceduralRecipe::Load(file, before, error), "Malformed recipe must fail");
		Check(before.seed == 12345, "Failed load must leave caller configuration intact");
	}
	{
		std::ofstream output(path);
		output << R"({"version":3,"seed":-1})";
		output.close();
		Check(ProceduralRecipe::Load(file, loaded, error) && loaded.seed == 0xFFFFFFFFu, "Legacy signed uint32 seeds must stay compatible");
	}
	for (const char* example : { "example_forest.json", "example_village_forest.json", "example_desert_village.json", "example_training_ground.json" }) {
		Check(ProceduralRecipe::Load("data/procedural/recipes/" + wxString::FromUTF8(example), loaded, error), "Existing v3 recipes must remain loadable");
	}
	for (const char* example : { "example_continent.json", "example_world_layers.json", "example_city_blocks.json", "example_ridged_continent.json", "example_large_continent_1098.json", "example_balanced_city.json", "example_layered_town.json" }) {
		Check(ProceduralRecipe::Load("data/procedural/recipes/" + wxString::FromUTF8(example), loaded, error) && loaded.source == GenerationSource::WorldPlan, "Bundled world recipes must load");
	}
	std::filesystem::remove(path);
}

static void TestRelativeRecipes() {
	const auto root = std::filesystem::temp_directory_path() / ("rme-recipe-paths-" + std::to_string(wxGetProcessId()));
	const auto source = root / "source", moved = root / "moved";
	std::filesystem::create_directories(source / "assets");
	std::filesystem::create_directories(moved / "assets");
	auto wxPath = [](const std::filesystem::path& path) {const auto text=path.u8string();return wxString::FromUTF8(std::string(text.begin(),text.end())); };
	GenerationSpec spec;
	spec.source = GenerationSource::WorldPlan;
	spec.world.heightmapPath = wxPath(source / "assets" / "height.png");
	spec.world.biomeHintsPath = wxPath(source / "assets" / "biomes.png");
	spec.world.catalogPath = wxPath(source / "assets" / "buildings.json");
	spec.imageMask.legendPath = wxPath(source / "assets" / "legend.json");
	spec.imageMask.imagePath = wxPath(source / "assets" / "reference.png");
	wxString error;
	const auto recipe = source / "recipe.json";
	Check(ProceduralRecipe::Save(wxPath(recipe), spec, error, ProceduralRecipe::PathMode::RelativeToRecipe), "Recipe-relative save must succeed");
	{
		std::ifstream file(recipe);
		json::mValue root;
		Check(json::read(file, root), "Saved relative recipe must be valid JSON");
		Check(root.get_obj().at("pathBase").get_str() == "recipe" && root.get_obj().at("world").get_obj().at("heightmap").get_str() == "assets/height.png", "Relative saves must mark their base and avoid machine-specific absolute paths for neighboring assets");
	}
	std::filesystem::copy_file(recipe, moved / "recipe.json", std::filesystem::copy_options::overwrite_existing);
	GenerationSpec loaded;
	Check(ProceduralRecipe::Load(wxPath(moved / "recipe.json"), loaded, error), "Moved recipe must load independently of process working directory");
	Check(loaded.world.heightmapPath == wxPath(moved / "assets" / "height.png") && loaded.world.biomeHintsPath == wxPath(moved / "assets" / "biomes.png") && loaded.world.catalogPath == wxPath(moved / "assets" / "buildings.json") && loaded.imageMask.legendPath == wxPath(moved / "assets" / "legend.json") && loaded.imageMask.imagePath == wxPath(moved / "assets" / "reference.png"), "Every asset path must resolve against the moved recipe directory");
	std::ifstream beforeFile(recipe);
	const std::string before((std::istreambuf_iterator<char>(beforeFile)), {});
	beforeFile.close();
	spec.world.heightmapPath = wxString::FromUTF8(std::string("bad\0path", 8));
	Check(!ProceduralRecipe::Save(wxPath(recipe), spec, error, ProceduralRecipe::PathMode::RelativeToRecipe), "Embedded NUL paths must fail before replacing an existing recipe");
	std::ifstream afterFile(recipe);
	const std::string after((std::istreambuf_iterator<char>(afterFile)), {});
	afterFile.close();
	Check(before == after, "Failed recipe validation must preserve the original file");
	std::filesystem::remove(recipe);
	std::filesystem::remove(moved / "recipe.json");
	std::filesystem::remove(source / "assets");
	std::filesystem::remove(moved / "assets");
	std::filesystem::remove(source);
	std::filesystem::remove(moved);
	std::filesystem::remove(root);
}

static void TestScopedRandom() {
	mt_seed(42);
	const auto state = mt_capture_state();
	unsigned long first = 0;
	{
		ScopedMTRandom outer(1337);
		first = mt_randi();
		const auto outerState = mt_capture_state();
		{
			ScopedMTRandom inner(7);
			for (int i = 0; i < 1000; ++i) {
				mt_randi();
			}
		}
		const auto restored = mt_capture_state();
		Check(outerState.values == restored.values && outerState.index == restored.index, "Nested random scopes must restore state");
	}
	const auto restored = mt_capture_state();
	Check(state.values == restored.values && state.index == restored.index, "Brush scope must preserve editor global RNG");
	{
		ScopedMTRandom repeat(1337);
		Check(mt_randi() == first, "Scoped brush seed must reproduce selection");
	}
}

static void TestCatalogJson() {
	WorldGen::BlueprintCatalog catalog;
	std::string error;
	Check(WorldGen::LoadBlueprintCatalog("data/procedural/buildings.json", catalog, error), "Bundled construction catalog must load");
	Check(catalog.size() == WorldGen::DefaultBlueprints().size(), "Bundled and built-in catalogs must contain the same templates");
	for (size_t i = 0; i < catalog.size(); ++i) {
		Check(catalog[i].id == WorldGen::DefaultBlueprints()[i].id && catalog[i].rows == WorldGen::DefaultBlueprints()[i].rows && catalog[i].upperRows == WorldGen::DefaultBlueprints()[i].upperRows, "Data catalog must match the fallback used without client files");
	}
	const auto path = std::filesystem::temp_directory_path() / ("rme-world-catalog-" + std::to_string(wxGetProcessId()) + ".json");
	Check(WorldGen::LoadBlueprintCatalog("data/procedural/layered_buildings.json", catalog, error) && catalog[0].upperFloors.size() == 2, "Explicit upper floor arrays must parse from the bundled layered catalog");
	for (const char* floors : { "false", "[]", "[[]]", "[[1,2,3,4,5]]", "[null]", "[[\"#####\",\"#a.b#\",\"#...#\",\"#####\"]]" }) {
		std::ofstream(path) << "{\"version\":1,\"templates\":[{\"id\":\"layered\",\"maxLevels\":2,\"rows\":[\"#####\",\"#a.b#\",\"#...#\",\"#...#\",\"##+##\"],\"upperFloors\":" << floors << "}]}";
		Check(!WorldGen::LoadBlueprintCatalog(path.string(), catalog, error) && catalog[0].upperFloors.size() == 2, "Malformed upper floor arrays must fail without replacing the previous catalog");
	}
	{
		std::ifstream input("data/procedural/buildings.json", std::ios::binary);
		const std::string saved((std::istreambuf_iterator<char>(input)), {});
		std::ofstream(path, std::ios::binary) << "\xef\xbb\xbf" << saved;
		Check(WorldGen::LoadBlueprintCatalog(path.string(), catalog, error), "UTF-8 BOM construction catalogs must load");
		std::ofstream(path, std::ios::binary) << saved << "{}";
		Check(!WorldGen::LoadBlueprintCatalog(path.string(), catalog, error), "A second JSON document must not be silently ignored");
	}
	for (const char* invalid : {
			 R"({"version":2,"templates":[]})", R"({"version":1,"templates":[false]})",
			 R"({"version":1,"templates":[{"id":"bad","rows":"not an array"}]})",
			 R"({"version":1,"templates":[{"id":"bad","weight":4294967296,"rows":[]}]})",
			 R"({"version":1,"templates":[{"id":"bad","rotate":1,"rows":[]}]})" }) {
		std::ofstream file(path);
		file << invalid;
		file.close();
		Check(!WorldGen::LoadBlueprintCatalog(path.string(), catalog, error), "Malformed construction catalog must fail");
		Check(catalog.size() == WorldGen::DefaultBlueprints().size(), "Failed catalog load must preserve the caller catalog");
	}
	for (const char* objects : { R"([{"x":2,"y":2,"brush":"table"}])", R"([{"x":2,"y":2,"floor":1,"required":true,"brushes":["n","e","s","w"]}])" }) {
		std::ofstream file(path);
		file << "{\"version\":1,\"templates\":[{\"id\":\"furnished\",\"maxLevels\":2,\"rows\":[\"#####\",\"#a.b#\",\"#...#\",\"#...#\",\"##+##\"],\"objects\":" << objects << "}]}";
		file.close();
		Check(WorldGen::LoadBlueprintCatalog(path.string(), catalog, error) && catalog[0].objects.size() == 1, "Furniture JSON must load both singular and orientation brush forms");
	}
	for (const char* objects : { R"([{"x":2,"y":2}])", R"([{"x":2,"y":2,"brush":7}])", R"([{"x":4294967296,"y":2,"brush":"table"}])", R"([{"x":2,"y":2,"required":1,"brush":"table"}])", R"([{"x":2,"y":2,"brush":"table","brushes":["n","e","s","w"]}])", R"([{"x":2,"y":2,"brushes":["n","e"]}])" }) {
		std::ofstream file(path);
		file << "{\"version\":1,\"templates\":[{\"id\":\"furnished\",\"maxLevels\":2,\"rows\":[\"#####\",\"#a.b#\",\"#...#\",\"#...#\",\"##+##\"],\"objects\":" << objects << "}]}";
		file.close();
		Check(!WorldGen::LoadBlueprintCatalog(path.string(), catalog, error), "Malformed furniture data must fail atomically");
	}
	for (const char* materials : { R"({"floor":"wooden floor","wall":"stone"})", R"({"floor":"wooden floor"})", R"({"wall":"stone"})" }) {
		std::ofstream(path) << "{\"version\":1,\"templates\":[{\"id\":\"styled\",\"rows\":[\"#####\",\"#a.b#\",\"#...#\",\"#...#\",\"##+##\"],\"materials\":" << materials << "}]}";
		Check(WorldGen::LoadBlueprintCatalog(path.string(), catalog, error), "Partial/exact template materials must parse");
	}
	for (const char* materials : { "false", R"({"floor":0})", R"({"floor":""})", R"({"wall":[]})" }) {
		std::ofstream(path) << "{\"version\":1,\"templates\":[{\"id\":\"styled\",\"rows\":[\"#####\",\"#a.b#\",\"#...#\",\"#...#\",\"##+##\"],\"materials\":" << materials << "}]}";
		Check(!WorldGen::LoadBlueprintCatalog(path.string(), catalog, error), "Malformed template material bindings must fail atomically");
	}

	for (const char* materials : { R"([{}])", R"([{}, {"floor":"wooden floor","wall":"stone wall"}])" }) {
		std::ofstream(path) << "{\"version\":1,\"templates\":[{\"id\":\"styled\",\"maxLevels\":2,\"rows\":[\"#####\",\"#a.b#\",\"#...#\",\"#...#\",\"##+##\"],\"floorMaterials\":" << materials << "}]}";
		Check(WorldGen::LoadBlueprintCatalog(path.string(), catalog, error), "Partial per-floor material arrays must parse");
	}
	for (const char* materials : { "false", "[]", "[7]", R"([{"floor":0}])", R"([{"wall":""}])", "[{},{},{}]" }) {
		std::ofstream(path) << "{\"version\":1,\"templates\":[{\"id\":\"styled\",\"maxLevels\":2,\"rows\":[\"#####\",\"#a.b#\",\"#...#\",\"#...#\",\"##+##\"],\"floorMaterials\":" << materials << "}]}";
		Check(!WorldGen::LoadBlueprintCatalog(path.string(), catalog, error), "Malformed per-floor materials must fail");
	}

	for (const char* metadata : { R"("role":"civic","maxPerSettlement":1)", R"("role":"residential","maxPerSettlement":0)", R"("minPerSettlement":2,"maxPerSettlement":2)" }) {
		std::ofstream(path) << "{\"version\":1,\"templates\":[{\"id\":\"role\",\"rows\":[\"#####\",\"#a.b#\",\"#...#\",\"#...#\",\"##+##\"]," << metadata << "}]}";
		Check(WorldGen::LoadBlueprintCatalog(path.string(), catalog, error), "Role and per-town template caps must parse");
	}
	for (const char* metadata : { R"("role":4)", R"("role":"")", R"("maxPerSettlement":-1)", R"("maxPerSettlement":65)", R"("maxPerSettlement":true)", R"("minPerSettlement":-1)", R"("minPerSettlement":65)", R"("minPerSettlement":true)", R"("minPerSettlement":1.5)", R"("minPerSettlement":2,"maxPerSettlement":1)" }) {
		std::ofstream(path) << "{\"version\":1,\"templates\":[{\"id\":\"role\",\"rows\":[\"#####\",\"#a.b#\",\"#...#\",\"#...#\",\"##+##\"]," << metadata << "}]}";
		Check(!WorldGen::LoadBlueprintCatalog(path.string(), catalog, error), "Malformed role/quota metadata must fail atomically");
	}

	std::filesystem::remove(path);
}

static void TestDiagnostics() {
	WorldGen::Settings settings;
	settings.width = settings.height = 64;
	settings.caveDepth = 1;
	WorldGen::Plan plan;
	std::string error;
	Check(WorldGen::Build(settings, plan, error), "Diagnostic fixture must build");
	plan.warnings.push_back("Quote \" and slash \\ and newline\n and tab\t");
	const auto prefix = std::filesystem::temp_directory_path() / std::filesystem::path(u8"rme-world-export-\u00E7");
	const auto encoded = prefix.u8string();
	const std::string path(encoded.begin(), encoded.end());
	Check(WorldGen::ExportDiagnostics(plan, path, error), "Diagnostics must support Unicode filenames");
	std::ifstream file(std::filesystem::u8path(path + ".json"));
	json::mValue root;
	Check(json::read(file, root) && root.type() == json::obj_type, "Diagnostic manifest must be valid JSON, including escaped warnings");
	Check(root.get_obj().at("algorithmVersion").get_int() == WorldGen::AlgorithmVersion, "Manifest must identify the planner algorithm");
	Check(root.get_obj().at("fingerprint").get_str() == std::to_string(WorldGen::Fingerprint(plan)), "Manifest must preserve the complete 64-bit plan fingerprint without JSON rounding");
	Check(root.get_obj().at("warnings").get_array().back().get_str() == plan.warnings.back(), "Diagnostic strings must round-trip control characters");
	Check(root.get_obj().at("layers").get_array().size() == plan.layers.size() && root.get_obj().at("verticalLinks").get_array().size() == plan.links.size(), "Manifest must describe every floor and travel edge");
	for (const auto& layer : plan.layers) {
		const auto pathLayer = std::filesystem::u8path(path + "_z" + std::to_string(layer.offsetZ) + ".ppm");
		Check(std::filesystem::file_size(pathLayer) > layer.cells.size() * 3, "Each terrain diagnostic must include all raster cells");
		std::filesystem::remove(pathLayer);
	}
	file.close();
	std::filesystem::remove(std::filesystem::u8path(path + ".json"));
	plan.layers.clear();
	Check(!WorldGen::ExportDiagnostics(plan, path, error), "Invalid plans must fail before diagnostic output");
}

static void TestWorldInputs() {
	WorldGen::GeographyInput input;
	wxString error;
	wxImage image(2, 2);
	image.InitAlpha();
	image.SetAlpha(0, 0, 255);
	image.SetAlpha(1, 0, 255);
	image.SetAlpha(0, 1, 255);
	image.SetAlpha(1, 1, 0);
	image.SetRGB(0, 0, 0, 0, 0);
	image.SetRGB(1, 0, 255, 255, 255);
	image.SetRGB(0, 1, 128, 128, 128);
	image.SetRGB(1, 1, 43, 200, 99);
	Check(WorldGen::DecodeHeightmap(image, 4, 4, input, error), "Grayscale source with transparent procedural holes must load");
	Check(input.elevation[0] == 0 && input.elevation[3] == 10000 && input.elevation[12] == 5020 && input.elevation[15] == 65535, "Raster sampling must preserve endpoints and transparent holes");
	const auto before = input.elevation;
	image.SetAlpha(0, 0, 128);
	Check(!WorldGen::DecodeHeightmap(image, 1, 1, input, error) && input.elevation == before, "Partial alpha must fail without changing input");
	image.SetAlpha(0, 0, 255);
	image.SetRGB(0, 0, 0, 1, 0);
	Check(!WorldGen::DecodeHeightmap(image, 1, 1, input, error), "Colored heightmaps must fail");
	for (int y = 0; y < 2; ++y) {
		for (int x = 0; x < 2; ++x) {
			uint32_t color = WorldGen::TerrainColor(WorldGen::Terrain::Forest);
			image.SetRGB(x, y, color >> 16, (color >> 8) & 255, color & 255);
		}
	}
	Check(WorldGen::DecodeBiomeHints(image, 4, 4, input, error) && input.biomes[0] == WorldGen::Terrain::Forest && input.biomes[15] == WorldGen::Terrain::Void, "Biome palette must preserve categories and alpha");
	image.SetRGB(1, 0, 1, 2, 3);
	Check(!WorldGen::DecodeBiomeHints(image, 1, 1, input, error), "Downsampling cannot conceal unknown source colors");
	WorldGen::Settings settings;
	settings.width = settings.height = 32;
	settings.riverCount = 0;
	settings.settlementCount = 0;
	input.elevation.assign(1024, 6000);
	input.biomes.assign(1024, WorldGen::Terrain::Desert);
	WorldGen::Plan plan;
	std::string message;
	Check(WorldGen::Build(settings, plan, message, nullptr, &input), "Raster constrained plan must build");
	Check(plan.FindLayer(0)->cells[16 * 32 + 16].terrain == WorldGen::Terrain::Desert, "Dry biome hints must override procedural climate");
	Check(plan.FindLayer(0)->cells[0].terrain == WorldGen::Terrain::Ocean, "Ocean boundary must survive dry input pixels");
	Check(WorldGen::Validate(plan).valid, "Raster constrained plan must preserve world invariants");
	input.elevation[500] = 12000;
	Check(!WorldGen::Build(settings, plan, message, nullptr, &input), "Out of range elevations must fail");
	input.elevation.clear();
	input.biomes.assign(1024, WorldGen::Terrain::Road);
	Check(!WorldGen::Build(settings, plan, message, nullptr, &input), "Infrastructure cannot masquerade as a natural biome hint");
	const auto path = std::filesystem::temp_directory_path() / "rme-world-invalid.png";
	{
		std::ofstream file(path, std::ios::binary);
		file << "not a PNG";
	}
	wxImage loaded;
	Check(!WorldGen::LoadWorldPNG(wxString::FromUTF8(path.string()), loaded, error), "Invalid PNG signature must fail before decode");
	std::filesystem::remove(path);
}

static void TestEditableInputs() {
	WorldGen::Settings settings;
	settings.width = settings.height = 32;
	settings.settlementCount = settings.buildingsPerSettlement = settings.riverCount = 0;
	WorldGen::Plan plan;
	std::string message;
	Check(WorldGen::Build(settings, plan, message), "Editable-input fixture must build");
	auto& cells = plan.FindLayer(0)->cells;
	for (size_t i = 0; i < cells.size(); ++i) cells[i].elevation = uint16_t((i * 73) % 10001);
	for (int i = int(WorldGen::Terrain::Beach); i <= int(WorldGen::Terrain::Mountain); ++i) cells[size_t(i)].terrain = WorldGen::Terrain(i);
	wxImage heightmap, hints;
	wxString error;
	Check(WorldGen::EncodeGeographyInputs(plan, heightmap, hints, error), "Editable PNG images must encode");
	WorldGen::GeographyInput decoded;
	Check(WorldGen::DecodeHeightmap(heightmap, 32, 32, decoded, error) && WorldGen::DecodeBiomeHints(hints, 32, 32, decoded, error), "Encoded images must pass the strict production decoders");
	for (size_t i = 0; i < cells.size(); ++i) {
		const auto& cell = cells[i];
		Check(std::abs(int(decoded.elevation[i]) - int(cell.elevation)) <= 20, "8-bit elevation quantization must remain within 20 normalized units");
		const bool natural = cell.terrain >= WorldGen::Terrain::Beach && cell.terrain <= WorldGen::Terrain::Mountain;
		Check(decoded.biomes[i] == (natural ? cell.terrain : WorldGen::Terrain::Void), "Only natural terrain belongs in editable biome hints; water and infrastructure must remain transparent");
	}
	const auto prefix = std::filesystem::temp_directory_path() / (std::filesystem::path(u8"rme-editable-\u00e7-").native() + std::filesystem::path(std::to_string(wxGetProcessId())).native());
	const auto utf8 = prefix.u8string();
	wxString heightPath, biomePath;
	Check(WorldGen::ExportGeographyInputs(plan, wxString::FromUTF8(std::string(utf8.begin(), utf8.end())), heightPath, biomePath, error), "Editable PNG files must save to Unicode paths");
	wxImage reopened;
	WorldGen::GeographyInput replay;
	Check(WorldGen::LoadWorldPNG(heightPath, reopened, error) && WorldGen::DecodeHeightmap(reopened, 32, 32, replay, error), "Saved editable heightmap must reload");
	Check(WorldGen::LoadWorldPNG(biomePath, reopened, error) && WorldGen::DecodeBiomeHints(reopened, 32, 32, replay, error), "Saved biome alpha/palette must reload");
	Check(decoded.elevation == replay.elevation && decoded.biomes == replay.biomes, "PNG encoding must preserve every editable sample and transparent hole");
	std::filesystem::remove(std::filesystem::u8path(nstr(heightPath)));
	std::filesystem::remove(std::filesystem::u8path(nstr(biomePath)));
	auto invalid = plan;
	invalid.FindLayer(0)->cells[0].elevation = 10001;
	const auto before = heightmap.GetRed(0, 0);
	Check(!WorldGen::EncodeGeographyInputs(invalid, heightmap, hints, error) && heightmap.GetWidth() == 32 && heightmap.GetRed(0, 0) == before, "Failed encoding must preserve the caller's images");
}

int main() {
	try {
		TestDiagnostics();
		TestWorldInputs();
		TestEditableInputs();
		TestRelativeRecipes();
		TestRecipes();
		TestScopedRandom();
		TestCatalogJson();
		std::cout << "PASS: v3/v4 recipes, uint32 seeds, target origins, malformed inputs and scoped brush RNG\n";
		return 0;
	} catch (const std::exception& e) {
		std::cerr << "FAIL: " << e.what() << '\n';
		return 1;
	}
}
