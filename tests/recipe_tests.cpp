#include "procedural_recipe.h"
#include "mt_rand.h"
#include "world_catalog.h"

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
	original.world.settings.riverCount = 6;
	original.world.settings.settlementCount = 4;
	original.world.settings.buildingsPerSettlement = 5;
	original.world.settings.buildingLevels = 3;
	original.world.settings.caveDepth = 2;
	original.world.settings.caveFill = 40;
	original.world.settings.roadWidth = 3;
	original.world.connectionItemId = 1387;
	original.world.wallBrushName = "Stone walls";
	original.world.catalogPath = "custom-buildings.json";
	original.world.protectExisting = false;
	original.imageMask.legendPath = "custom-world.json";
	wxString error;
	Check(ProceduralRecipe::Save(file, original, error), "World recipe must save");
	GenerationSpec loaded;
	Check(ProceduralRecipe::Load(file, loaded, error), "World recipe must load");
	Check(loaded.source == original.source && loaded.seed == original.seed, "Full uint32 seed and world source must round-trip");
	Check(loaded.region.originX == 200 && loaded.region.originY == 300 && loaded.region.width == 128 && loaded.region.height == 96, "Target origin and dimensions must round-trip");
	Check(loaded.world.settings.shape == original.world.settings.shape && loaded.world.settings.seaLevel == 4000 && loaded.world.settings.riverCount == 6, "World geography must round-trip");
	Check(loaded.world.settings.settlementCount == 4 && loaded.world.settings.buildingsPerSettlement == 5 && loaded.world.settings.buildingLevels == 3, "World settlement settings must round-trip");
	Check(loaded.world.settings.caveDepth == 2 && loaded.world.settings.caveFill == 40 && loaded.world.settings.roadWidth == 3, "Caves and road settings must round-trip");
	Check(loaded.world.connectionItemId == 1387 && loaded.world.wallBrushName == "Stone walls" && !loaded.world.protectExisting, "Client/protection settings must round-trip");
	Check(loaded.world.catalogPath == original.world.catalogPath, "Construction catalog path must round-trip");
	const auto unicodePath = std::filesystem::temp_directory_path() / std::filesystem::path(u8"rme-world-\u00E7-\u6D4B\u8BD5.json");
	const auto encodedPath = unicodePath.u8string();
	const wxString unicodeFile = wxString::FromUTF8(std::string(encodedPath.begin(), encodedPath.end()));
	Check(ProceduralRecipe::Save(unicodeFile, original, error) && ProceduralRecipe::Load(unicodeFile, loaded, error), "Recipe paths must support Unicode on Windows and Linux");
	std::filesystem::remove(unicodePath);
	for (const char* malformed : {
			 R"({"version":99})", R"({"seed":4294967296})", R"({"seed":1.5})",
			 R"({"source":"world","world":{"caveDepth":9}})",
			 R"({"source":"world","world":{"seaLevel":4294967296}})",
			 R"({"source":"world","world":{"shape":"typo"}})",
			 R"({"source":"world","world":{"protectExisting":0}})",
			 R"({"source":"world","world":{"wallBrush":false}})",
			 R"({"region":{"width":4294967296}})", R"({"world":null})" }) {
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
	for (const char* example : { "example_continent.json", "example_world_layers.json" }) {
		Check(ProceduralRecipe::Load("data/procedural/recipes/" + wxString::FromUTF8(example), loaded, error) && loaded.source == GenerationSource::WorldPlan, "Bundled world recipes must load");
	}
	std::filesystem::remove(path);
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
	std::filesystem::remove(path);
}

int main() {
	try {
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
