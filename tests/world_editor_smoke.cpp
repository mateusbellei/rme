#include "main.h"

#include "world_editor.h"
#include <wx/imagpng.h>
#include <wx/wfstream.h>
#include "editor.h"
#include "copybuffer.h"
#include "items.h"
#include "brush.h"
#include "wall_brush.h"
#include "doodad_brush.h"
#include "ground_brush.h"
#include "iomap_otbm.h"
#include "mt_rand.h"
#include "settings.h"

#include <filesystem>
#include <memory>
#include <stdexcept>

namespace {
	void Check(bool condition, const std::string& message) {
		if (!condition) {
			throw std::runtime_error(message);
		}
	}

	class MapCodec : public IOMapOTBM {
	public:
		explicit MapCodec(MapVersion version) :
			IOMapOTBM(version) { }
		bool Write(Map& map, NodeFileWriteHandle& file) {
			return saveMap(map, file);
		}
		bool Read(Map& map, NodeFileReadHandle& file) {
			return loadMap(map, file);
		}
	};

	uint64_t MapHash(Map& map) {
		uint64_t hash = 1469598103934665603ull;
		auto add = [&](uint32_t value) { for (int b=0;b<4;++b) {hash^=(value>>(b*8))&255;hash*=1099511628211ull;} };
		for (auto it = map.begin(); it != map.end(); ++it) {
			Tile* tile = (*it)->get();
			if (!tile || tile->empty()) {
				continue;
			}
			const auto pos = tile->getPosition();
			add(pos.x);
			add(pos.y);
			add(pos.z);
			add(tile->ground ? tile->ground->getID() : 0);
			add(tile->getMapFlags());
			for (Item* item : tile->items) {
				add(item->getID());
				if (auto* teleport = dynamic_cast<Teleport*>(item)) {
					const auto target = teleport->getDestination();
					add(target.x);
					add(target.y);
					add(target.z);
				}
			}
		}
		return hash;
	}

	void FixtureItem(int id, ItemGroup_t group, bool blocked = false, ItemTypes_t type = ITEM_TYPE_NONE) {
		auto* item = new ItemType();
		item->id = uint16_t(id);
		item->group = group;
		item->unpassable = blocked;
		item->type = type;
		item->clientID = uint16_t(id);
		g_items.items.set(id, item);
	}

	void PrepareRegistry() {
		// Synthetic client definitions keep this regression public and asset-free.
		g_brushes.clear();
		g_items.clear();
		g_items.MajorVersion = 3;
		g_items.MinorVersion = 57;
		g_items.BuildNumber = 0;
		FixtureItem(100, ITEM_GROUP_GROUND);
		FixtureItem(101, ITEM_GROUP_GROUND, true);
		FixtureItem(103, ITEM_GROUP_TELEPORT, false, ITEM_TYPE_TELEPORT);
		for (int id = 200; id < 204; ++id) {
			FixtureItem(id, ITEM_GROUP_NONE, true);
		}
		FixtureItem(300, ITEM_GROUP_NONE, true);
		FixtureItem(301, ITEM_GROUP_NONE, false);
		FixtureItem(302, ITEM_GROUP_NONE, true);
		for (int id = 310; id < 314; ++id) {
			FixtureItem(id, ITEM_GROUP_NONE, true);
		}
		WallBrush::init();
		GroundBrush::init();
		pugi::xml_document xml;
		Check(bool(xml.load(R"(<brush><wall type="horizontal"><item id="200" chance="1"/></wall><wall type="vertical"><item id="201" chance="1"/></wall><wall type="corner"><item id="202" chance="1"/></wall><wall type="pole"><item id="203" chance="1"/></wall></brush>)")), "Wall fixture XML");
		auto wall = std::make_unique<WallBrush>();
		wall->setName("stone");
		wxArrayString warnings;
		Check(wall->load(xml.child("brush"), warnings), "Wall fixture must load");
		g_brushes.addBrush(wall.release());
		for (const auto& definition : { std::pair { "tree bush cactus snow crystal palm", R"(<brush><item id="300" chance="1"/><item id="301" chance="1"/></brush>)" }, std::pair { "tree rock", R"(<brush><composite chance="1"><tile x="0" y="0"><item id="302"/></tile><tile x="1" y="0"><item id="302"/></tile><tile x="0" y="1"><item id="302"/></tile><tile x="1" y="1"><item id="302"/></tile></composite></brush>)" } }) {
			pugi::xml_document props;
			Check(bool(props.load(definition.second)), "Decoration fixture XML");
			auto doodad = std::make_unique<DoodadBrush>();
			doodad->setName(definition.first);
			Check(doodad->load(props.child("brush"), warnings), "Decoration fixture must load");
			g_brushes.addBrush(doodad.release());
		}
		for (int direction = 0; direction < 4; ++direction) {
			pugi::xml_document props;
			const std::string xml = "<brush><item id=\"" + std::to_string(310 + direction) + "\" chance=\"1\"/></brush>";
			Check(bool(props.load(xml.c_str())), "Furniture fixture XML");
			auto brush = std::make_unique<DoodadBrush>();
			brush->setName("fixture furniture " + std::to_string(direction));
			Check(brush->load(props.child("brush"), warnings), "Furniture brush must load");
			g_brushes.addBrush(brush.release());
		}
	}

	void WriteFurnishedCatalog(const std::filesystem::path& path) {
		std::ofstream out(path);
		out << "{\"version\":1,\"templates\":[";
		bool first = true;
		for (const auto& b : WorldGen::DefaultBlueprints()) {
			if (!first) {
				out << ',';
			}
			first = false;
			out << "{\"id\":\"" << b.id << "\",\"weight\":" << b.weight << ",\"maxLevels\":" << b.maxLevels << ",\"rows\":[";
			for (size_t y = 0; y < b.rows.size(); ++y) {
				if (y) {
					out << ',';
				}
				out << '"' << b.rows[y] << '"';
			}
			out << ']';
			if (!b.upperRows.empty()) {
				out << ",\"upperRows\":[";
				for (size_t y = 0; y < b.upperRows.size(); ++y) {
					if (y) {
						out << ',';
					}
					out << '"' << b.upperRows[y] << '"';
				}
				out << ']';
			}
			if (b.id == "house" || b.id == "inn") {
				const int x = b.id == "house" ? 3 : 6, y = b.id == "house" ? 3 : 4;
				out << ",\"objects\":[";
				for (int floor = 0; floor < 2; ++floor) {
					if (floor) {
						out << ',';
					}
					out << "{\"x\":" << x << ",\"y\":" << y << ",\"floor\":" << floor << ",\"required\":true,\"brushes\":[\"fixture furniture 0\",\"fixture furniture 1\",\"fixture furniture 2\",\"fixture furniture 3\"]}";
				}
				out << ']';
			}
			out << '}';
		}
		out << "]}";
	}

	void WriteLegend(const std::filesystem::path& path, bool bad = false) {
		std::ofstream out(path);
		out << "{\"entries\":[";
		for (int i = 0; i < int(WorldGen::Terrain::Void); ++i) {
			const auto terrain = static_cast<WorldGen::Terrain>(i);
			const uint32_t color = WorldGen::TerrainColor(terrain);
			if (i) {
				out << ',';
			}
			out << "{\"color\":\"#" << std::hex << std::setw(6) << std::setfill('0') << color << std::dec << "\",\"tileId\":";
			const bool barrier = WorldGen::IsWater(terrain) || terrain == WorldGen::Terrain::Mountain || terrain == WorldGen::Terrain::CaveWall;
			out << (bad ? 65530 : (barrier ? 101 : 100)) << '}';
		}
		out << "]}";
	}

	void CheckTravel(Editor& editor, const GenerationSpec& resolved, const WorldGen::Plan& plan) {
		for (const auto& link : plan.links) {
			const Position upper(resolved.region.originX + link.upper.x, resolved.region.originY + link.upper.y, resolved.region.z + link.upperOffsetZ);
			const Position lower(resolved.region.originX + link.lower.x, resolved.region.originY + link.lower.y, resolved.region.z + link.lowerOffsetZ);
			const Position upperArrival(resolved.region.originX + link.upperArrival.x, resolved.region.originY + link.upperArrival.y, resolved.region.z + link.upperOffsetZ);
			const Position lowerArrival(resolved.region.originX + link.lowerArrival.x, resolved.region.originY + link.lowerArrival.y, resolved.region.z + link.lowerOffsetZ);
			for (const auto pair : { std::pair { upper, lowerArrival }, std::pair { lower, upperArrival } }) {
				Tile* portalTile = editor.map.getTile(pair.first);
				Check(portalTile != nullptr, "Portal tile must exist");
				int found = 0;
				for (Item* item : portalTile->items) {
					if (auto* teleport = dynamic_cast<Teleport*>(item)) {
						++found;
						Check(teleport->getDestination() == pair.second, "Portal must encode the planned adjacent arrival");
					}
				}
				Check(found == 1, "Exactly one travel portal per endpoint");
				Tile* arrival = editor.map.getTile(pair.second);
				Check(arrival && arrival->hasGround() && !arrival->isBlocking(), "Actual client arrival must be walkable");
				for (Item* item : arrival->items) {
					Check(!dynamic_cast<Teleport*>(item), "Arrival must not create a teleport loop");
				}
			}
		}
	}
}

static int RunWorldScenario(const wxString& outputDirectory, bool blocks) {
	try {
		const std::filesystem::path directory = nstr(outputDirectory);
		std::filesystem::create_directories(directory);
		PrepareRegistry();
		WriteLegend(directory / "world-legend.json");
		WriteFurnishedCatalog(directory / "furnished-catalog.json");
		WriteLegend(directory / "invalid-legend.json", true);
		MapVersion version;
		version.otbm = MAP_OTBM_2;
		version.client = ClientVersionID(57);
		CopyBuffer copyBuffer;
		Editor editor(copyBuffer, version);
		editor.map.setWidth(512);
		editor.map.setHeight(512);
		Tile* untouched = editor.map.createTile(480, 480, 7);
		untouched->addItem(Item::Create(100));
		untouched->update();
		const uint64_t before = MapHash(editor.map);
		GenerationSpec spec;
		spec.source = GenerationSource::WorldPlan;
		spec.world.settings.settlementLayout = blocks ? WorldGen::SettlementLayout::Blocks : WorldGen::SettlementLayout::Organic;
		spec.seed = blocks ? 42u : 1337u;
		spec.region.originX = 16;
		spec.region.originY = 24;
		spec.region.width = blocks ? 256 : 192;
		spec.region.height = blocks ? 256 : 192;
		spec.region.z = 7;
		spec.world.settings.settlementCount = 2;
		spec.world.settings.buildingsPerSettlement = 4;
		spec.world.settings.caveDepth = 2;
		spec.world.settings.buildingLevels = 3;
		spec.world.connectionItemId = 103;
		spec.world.catalogPath = wxString::FromUTF8((directory / "furnished-catalog.json").string());
		spec.doodads.enabled = true;
		spec.doodads.density = 45;
		// World buildings must have physical walls even when the legacy toggle is off.
		spec.pipeline.placeWalls = false;
		spec.pipeline.borderizeAfter = true;
		spec.pipeline.randomizeGround = true;
		spec.imageMask.legendPath = wxString::FromUTF8((directory / "world-legend.json").string());
		GenerationSpec resolved;
		WorldGen::Plan plan;
		wxString error;
		Check(WorldEditor::BuildPlan(editor, spec, resolved, plan, error), "Build real editor plan: " + nstr(error));
		// Feed a real PNG back into the production adapter before OTBM generation.
		wxImage heightmap(plan.settings.width, plan.settings.height);
		for (int y = 0; y < plan.settings.height; ++y) {
			for (int x = 0; x < plan.settings.width; ++x) {
				const unsigned char value = static_cast<unsigned char>((uint32_t(plan.FindLayer(0)->cells[size_t(y) * plan.settings.width + x].elevation) * 255 + 5000) / 10000);
				heightmap.SetRGB(x, y, value, value, value);
			}
		}
		spec.world.heightmapPath = wxString::FromUTF8((directory / "heightmap.png").string());
		{
			wxPNGHandler encoder;
			wxFileOutputStream stream(spec.world.heightmapPath);
			Check(encoder.SaveFile(&heightmap, stream, false), "Heightmap fixture must save as real PNG");
		}
		Check(WorldEditor::BuildPlan(editor, spec, resolved, plan, error), "Real PNG must feed the production world planner: " + nstr(error));
		Check(!plan.buildings.empty() && !plan.links.empty(), "Smoke fixture must exercise buildings and vertical travel");
		mt_seed(717);
		const auto randomBefore = mt_capture_state();
		for (const char* stage : { "Checking target and materials", "Copying existing context", "Preparing grounds and walls", "Resolving borders and walls", "Decorating biomes", "Furnishing construction templates", "Validating physical navigation", "Applying world as one undo operation" }) {
			bool visited = false;
			Check(!WorldEditor::ApplyPlan(editor, resolved, plan, error, nullptr, [&](const char* current, int) {if(std::string(current)==stage){visited=true;return false;}return true; }), "Cancellation must stop editor staging");
			Check(visited && MapHash(editor.map) == before && !editor.actionQueue->canUndo(), "Cancellation at every stage must preserve the original map and undo history");
			const auto afterCancel = mt_capture_state();
			Check(afterCancel.values == randomBefore.values && afterCancel.index == randomBefore.index, "Cancellation must preserve global brush RNG");
		}
		WorldEditor::ApplicationStats stats;
		Check(WorldEditor::ApplyPlan(editor, resolved, plan, error, &stats), "Apply staged world: " + nstr(error));
		Check(stats.furniture > 0, "Construction catalogs must place their required furniture on multiple floors");
		Check(stats.doodads > 0 && stats.changedTiles > 0, "Real brushes must place deterministic biome decorations");
		std::set<int> decorationIds;
		for (const auto& layer : plan.layers) {
			for (int y = 0; y < plan.settings.height; ++y) {
				for (int x = 0; x < plan.settings.width; ++x) {
					const auto& cell = layer.cells[size_t(y) * plan.settings.width + x];
					const Tile* tile = editor.map.getTile(resolved.region.originX + x, resolved.region.originY + y, resolved.region.z + layer.offsetZ);
					if (!tile) {
						continue;
					}
					for (const Item* item : tile->items) {
						if (item->getID() >= 300 && item->getID() <= 302) {
							decorationIds.insert(item->getID());
							Check(!(cell.flags & WorldGen::Reserved), "Decorations must avoid roads, doors and vertical travel reservations");
						}
					}
				}
			}
		}
		Check(decorationIds.count(300) && decorationIds.count(301) && decorationIds.count(302), "Fixture must exercise blocking, nonblocking and composite decorations");
		for (const auto& building : plan.buildings) {
			const auto blueprint = WorldGen::RotateBlueprint(plan.catalog[building.blueprint], building.rotation);
			for (const auto& object : blueprint.objects) {
				if (object.floor < building.levels) {
					const Tile* tile = editor.map.getTile(resolved.region.originX + building.origin.x + object.x, resolved.region.originY + building.origin.y + object.y, resolved.region.z - object.floor);
					bool found = false;
					for (const Item* item : tile->items) {
						found = found || item->getID() == 310 + building.rotation;
					}
					Check(found, "Furniture position and brush orientation must follow the rotated building on each floor");
				}
			}
		}
		const auto randomAfter = mt_capture_state();
		Check(randomBefore.values == randomAfter.values && randomBefore.index == randomAfter.index, "Complete application must preserve global brush RNG");
		const uint64_t applied = MapHash(editor.map);
		Check(applied != before, "Application must create tiles");
		CheckTravel(editor, resolved, plan);
		editor.actionQueue->undo();
		Check(MapHash(editor.map) == before, "One undo must restore terrain, walls, travel and untouched tiles");
		Check(!editor.actionQueue->canUndo(), "World generation must occupy one undo entry");
		editor.actionQueue->redo();
		Check(MapHash(editor.map) == applied, "Redo must restore exact generated content");
		Check(!WorldEditor::ApplyPlan(editor, resolved, plan, error) && MapHash(editor.map) == applied, "Collision rejection must leave existing world unchanged");
		editor.actionQueue->undo();
		auto missingFurniture = plan;
		for (auto& b : missingFurniture.catalog) {
			for (auto& object : b.objects) {
				object.brushes = { "missing required furniture" };
			}
		}
		Check(!WorldEditor::ApplyPlan(editor, resolved, missingFurniture, error) && MapHash(editor.map) == before, "Missing required furniture must reject the complete staged world without mutations");
		auto blockedFurniture = plan;
		for (auto& b : blockedFurniture.catalog) {
			for (auto& object : b.objects) {
				object.x = 2;
				object.y = 1;
			}
		}
		Check(!WorldEditor::ApplyPlan(editor, resolved, blockedFurniture, error) && MapHash(editor.map) == before, "Required furniture near travel landings must fail without editor mutations");
		for (auto& b : missingFurniture.catalog) {
			for (auto& object : b.objects) {
				object.required = false;
			}
		}
		WorldEditor::ApplicationStats optionalStats;
		Check(WorldEditor::ApplyPlan(editor, resolved, missingFurniture, error, &optionalStats) && optionalStats.furniture == 0 && optionalStats.skippedFurniture > 0 && !optionalStats.warnings.empty(), "Optional missing furniture must be reported and skipped");
		editor.actionQueue->undo();
		Check(MapHash(editor.map) == before, "Optional furniture skips must remain one atomic undo operation");
		GenerationSpec invalid = resolved;
		invalid.imageMask.legendPath = wxString::FromUTF8((directory / "invalid-legend.json").string());
		Check(!WorldEditor::ApplyPlan(editor, invalid, plan, error) && MapHash(editor.map) == before, "Invalid client materials must fail without any editor mutation");
		g_items[100].floorChangeDown = true;
		Check(!WorldEditor::ApplyPlan(editor, resolved, plan, error) && MapHash(editor.map) == before, "Movement-changing client grounds must fail before commit");
		g_items[100].floorChangeDown = false;
		Check(WorldEditor::ApplyPlan(editor, resolved, plan, error) && MapHash(editor.map) == applied, "Regeneration from the same plan must reproduce tile IDs and walls");
		MapCodec codec(version);
		MemoryNodeFileWriteHandle memory;
		Check(codec.Write(editor.map, memory), "Actual OTBM writer must serialize the world");
		{
			std::ofstream file(directory / "world.otbm", std::ios::binary);
			file.write("OTBM", 4);
			file.write(reinterpret_cast<const char*>(memory.getMemory()), std::streamsize(memory.getSize()));
		}
		Editor reopened(copyBuffer, version);
		MemoryNodeFileReadHandle input(memory.getMemory(), memory.getSize());
		Check(codec.Read(reopened.map, input), "Actual OTBM reader must reload the world");
		Check(MapHash(reopened.map) == applied, "OTBM round-trip must preserve terrain, walls and teleport destinations");
		CheckTravel(reopened, resolved, plan);
		std::ofstream result(directory / "result.txt");
		result << "PASS: real editor application, material preflight, collision protection, deterministic walls and biome decorations, RNG isolation, single undo/redo and OTBM round-trip\n";
		std::cout << "PASS: real editor application, material preflight, collision protection, deterministic walls and biome decorations, RNG isolation, single undo/redo and OTBM round-trip\n";
		return 0;
	} catch (const std::exception& exception) {
		std::cerr << "FAIL: world editor smoke: " << exception.what() << '\n';
		std::ofstream result(nstr(outputDirectory) + "/result.txt");
		result << "FAIL: " << exception.what() << '\n';
		return 1;
	}
}

int RunWorldEditorSelfTest(const wxString& outputDirectory) {
	const int village = RunWorldScenario(outputDirectory, false);
	if (village != 0) {
		return village;
	}
	return RunWorldScenario(outputDirectory + "/city", true);
}
