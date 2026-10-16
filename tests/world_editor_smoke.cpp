#include "main.h"

#include "world_editor.h"
#include "editor.h"
#include "copybuffer.h"
#include "items.h"
#include "brush.h"
#include "wall_brush.h"
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
		g_items.clear();
		g_brushes.clear();
		g_items.MajorVersion = 3;
		g_items.MinorVersion = 57;
		g_items.BuildNumber = 0;
		FixtureItem(100, ITEM_GROUP_GROUND);
		FixtureItem(101, ITEM_GROUP_GROUND, true);
		FixtureItem(103, ITEM_GROUP_TELEPORT, false, ITEM_TYPE_TELEPORT);
		for (int id = 200; id < 204; ++id) {
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

int RunWorldEditorSelfTest(const wxString& outputDirectory) {
	try {
		const std::filesystem::path directory = nstr(outputDirectory);
		std::filesystem::create_directories(directory);
		PrepareRegistry();
		WriteLegend(directory / "world-legend.json");
		WriteLegend(directory / "invalid-legend.json", true);
		MapVersion version;
		version.otbm = MAP_OTBM_2;
		version.client = ClientVersionID(57);
		CopyBuffer copyBuffer;
		Editor editor(copyBuffer, version);
		editor.map.setWidth(256);
		editor.map.setHeight(256);
		Tile* untouched = editor.map.createTile(240, 240, 7);
		untouched->addItem(Item::Create(100));
		untouched->update();
		const uint64_t before = MapHash(editor.map);
		GenerationSpec spec;
		spec.source = GenerationSource::WorldPlan;
		spec.region.originX = 16;
		spec.region.originY = 24;
		spec.region.width = 96;
		spec.region.height = 96;
		spec.region.z = 7;
		spec.world.settings.settlementCount = 2;
		spec.world.settings.buildingsPerSettlement = 4;
		spec.world.settings.caveDepth = 2;
		spec.world.settings.buildingLevels = 3;
		spec.world.connectionItemId = 103;
		spec.pipeline.placeWalls = true;
		spec.pipeline.borderizeAfter = true;
		spec.pipeline.randomizeGround = true;
		spec.imageMask.legendPath = wxString::FromUTF8((directory / "world-legend.json").string());
		GenerationSpec resolved;
		WorldGen::Plan plan;
		wxString error;
		Check(WorldEditor::BuildPlan(editor, spec, resolved, plan, error), "Build real editor plan: " + nstr(error));
		Check(!plan.buildings.empty() && !plan.links.empty(), "Smoke fixture must exercise buildings and vertical travel");
		mt_seed(717);
		const auto randomBefore = mt_capture_state();
		Check(WorldEditor::ApplyPlan(editor, resolved, plan, error), "Apply staged world: " + nstr(error));
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
		GenerationSpec invalid = resolved;
		invalid.imageMask.legendPath = wxString::FromUTF8((directory / "invalid-legend.json").string());
		Check(!WorldEditor::ApplyPlan(editor, invalid, plan, error) && MapHash(editor.map) == before, "Invalid client materials must fail without any editor mutation");
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
		result << "PASS: real editor application, material preflight, collision protection, deterministic walls, RNG isolation, single undo/redo and OTBM round-trip\n";
		std::cout << "PASS: real editor application, material preflight, collision protection, deterministic walls, RNG isolation, single undo/redo and OTBM round-trip\n";
		return 0;
	} catch (const std::exception& exception) {
		std::cerr << "FAIL: world editor smoke: " << exception.what() << '\n';
		std::ofstream result(nstr(outputDirectory) + "/result.txt");
		result << "FAIL: " << exception.what() << '\n';
		return 1;
	}
}
