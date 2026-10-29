#include "main.h"

#include "world_editor.h"
#include "world_decoration.h"
#include "procedural_recipe.h"
#include "procedural_common.h"
#include "world_batch.h"
#include "json.h"
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
#include "gui.h"
#include "town.h"

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <chrono>
#include <cstdlib>

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
		FixtureItem(80, ITEM_GROUP_NONE);
		g_items[80].is_metaitem = true;
		FixtureItem(100, ITEM_GROUP_GROUND);
		FixtureItem(101, ITEM_GROUP_GROUND, true);
		FixtureItem(104, ITEM_GROUP_GROUND);
		FixtureItem(105, ITEM_GROUP_GROUND);
		FixtureItem(106, ITEM_GROUP_GROUND);
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
		pugi::xml_document groundXml;
		Check(bool(groundXml.load(R"(<brush><item id="104" chance="1"/></brush>)")), "Styled ground fixture XML");
		auto ground = std::make_unique<GroundBrush>();
		ground->setName("fixture floor");
		Check(ground->load(groundXml.child("brush"), warnings), "Styled ground fixture must load");
		g_brushes.addBrush(ground.release());
		pugi::xml_document equivalentXml;
		Check(bool(equivalentXml.load(R"(<brush><item id="105" chance="1"/><border super="true" align="inner" to="none" ground_equivalent="105"><borderitem edge="dnw" item="106"/></border></brush>)")), "Equivalent ground fixture XML");
		auto equivalent = std::make_unique<GroundBrush>();
		equivalent->setName("fixture equivalent ground");
		Check(equivalent->load(equivalentXml.child("brush"), warnings), "Equivalent ground brush must load");
		g_brushes.addBrush(equivalent.release());
		pugi::xml_document replacementXml;
		Check(bool(replacementXml.load(R"(<brush><composite chance="1"><tile x="0" y="0"><item id="106"/></tile></composite></brush>)")), "Ground-replacing composite fixture XML");
		auto replacement = std::make_unique<DoodadBrush>();
		replacement->setName("fixture ground-replacing composite");
		Check(replacement->load(replacementXml.child("brush"), warnings), "Ground-replacing composite fixture must load");
		g_brushes.addBrush(replacement.release());
		pugi::xml_document metaXml;
		Check(bool(metaXml.load(R"(<brush><item id="80" chance="1"/></brush>)")), "Editor-only doodad fixture XML");
		auto meta = std::make_unique<DoodadBrush>();
		meta->setName("fixture editor-only object");
		Check(meta->load(metaXml.child("brush"), warnings), "Editor-only doodad fixture must load");
		g_brushes.addBrush(meta.release());
		Check(!g_items[106].isGroundTile() && g_items[106].ground_equivalent == 105 && g_items[105].has_equivalent, "Inline ground borders must bind their ground equivalent before serialization");
		for (int id = 204; id < 208; ++id) {
			FixtureItem(id, ITEM_GROUP_NONE, true);
		}
		pugi::xml_document decoyXml;
		Check(bool(decoyXml.load(R"(<brush><wall type="horizontal"><item id="204" chance="1"/></wall><wall type="vertical"><item id="205" chance="1"/></wall><wall type="corner"><item id="206" chance="1"/></wall><wall type="pole"><item id="207" chance="1"/></wall></brush>)")), "Decoy wall fixture XML");
		auto decoy = std::make_unique<WallBrush>();
		decoy->setName("a stone decoy");
		Check(decoy->load(decoyXml.child("brush"), warnings), "Decoy wall fixture must load");
		g_brushes.addBrush(decoy.release());
		Check(ProceduralCommon::FindWallBrush("stone")->getName() == "stone", "An exact wall name must win over an alphabetically earlier partial match");

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
		std::ofstream out(path, std::ios::binary);
		out << "\xef\xbb\xbf" << "{\"version\":1,\"templates\":[";
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
		std::ofstream out(path, std::ios::binary);
		out << "\xef\xbb\xbf" << "{\"entries\":[";
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

	std::string MetadataFixture(uint16_t maximum = 65535, uint32_t animatedMinimum = 100, uint32_t animatedMaximum = 100, bool animated = false) {
		std::ostringstream bytes(std::ios::binary);
		auto put = [&](uint32_t value, int count) { for (int i = 0; i < count; ++i){ bytes.put(char(value >> (8 * i)));
} };
		put(0x12345678, 4);
		put(maximum, 2);
		put(0, 2);
		put(0, 2);
		put(0, 2);
		for (uint32_t id = 100; id <= maximum; ++id) {
			put(255, 1); // Flag terminator, including at the maximum 16-bit ID.
			const int side = id == 300 ? 2 : 1;
			put(side, 1);
			put(side, 1);
			if (side > 1) {
				put(64, 1);
			}
			const int frames = animated && id == 100 ? 2 : 1;
			for (int field = 0; field < 5; ++field) {
				put(field == 4 ? frames : 1, 1); // Layers, patterns x/y/z, frames.
			}
			if (frames > 1) {
				put(1, 1); put(0, 4); put(0, 1); // Async, endless loops, first frame.
				for (int frame = 0; frame < frames; ++frame) { put(animatedMinimum, 4); put(animatedMaximum, 4); }
			}
			for (int pixel = 0; pixel < side * side * frames; ++pixel) {
				put(0, 4);
			}
		}
		return bytes.str();
	}

	void WriteBatchRegistry(const std::filesystem::path& directory) {
		MemoryNodeFileWriteHandle otb;
		otb.addNode(0);
		otb.addU32(0);
		otb.addU8(ROOT_ATTR_VERSION);
		otb.addU16(140);
		otb.addU32(3);
		otb.addU32(57);
		otb.addU32(0);
		const uint8_t reserved[128] = {};
		otb.addRAW(reserved, 128);
		for (int id : { 100, 101, 103, 200, 201, 202, 203, 300, 301, 302, 310, 311, 312, 313, 20001, 65535 }) {
			const bool ground = id == 100 || id == 101;
			otb.addNode(ground ? ITEM_GROUP_GROUND : ITEM_GROUP_NONE);
			otb.addU32(id == 101 || (id >= 200 && id <= 313 && id != 301) ? FLAG_UNPASSABLE : 0);
			otb.addU8(ITEM_ATTR_SERVERID);
			otb.addU16(2);
			otb.addU16(uint16_t(id));
			otb.addU8(ITEM_ATTR_CLIENTID);
			otb.addU16(2);
			otb.addU16(uint16_t(id));
			otb.endNode();
		}
		otb.endNode();
		{
			std::ofstream file(directory / "batch-items.otb", std::ios::binary);
			file.write("OTBI", 4);
			file.write(reinterpret_cast<const char*>(otb.getMemory()), std::streamsize(otb.getSize()));
		}
		{
			std::ofstream file(directory / "batch-items.xml");
			file << R"(<items><item id="103" name="portal"><attribute key="type" value="teleport"/></item><item id="20001" name="modern portal"><attribute key="type" value="teleport"/></item><item id="65535" name="maximum item"/></items>)";
		}
		std::ofstream file(directory / "batch-materials.xml");
		file << R"(<materials><brush name="stone" type="wall"><wall type="horizontal"><item id="200" chance="1"/></wall><wall type="vertical"><item id="201" chance="1"/></wall><wall type="corner"><item id="202" chance="1"/></wall><wall type="pole"><item id="203" chance="1"/></wall></brush><brush name="tree bush cactus snow crystal palm" type="doodad"><item id="300" chance="1"/><item id="301" chance="1"/></brush><brush name="tree rock" type="doodad"><composite chance="1"><tile x="0" y="0"><item id="302"/></tile><tile x="1" y="0"><item id="302"/></tile><tile x="0" y="1"><item id="302"/></tile><tile x="1" y="1"><item id="302"/></tile></composite></brush>)";
		for (int direction = 0; direction < 4; ++direction) {
			file << "<brush name=\"fixture furniture " << direction << "\" type=\"doodad\"><item id=\"" << 310 + direction << "\" chance=\"1\"/></brush>";
		}
		file << "</materials>";
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

static int RunPrefabScenario(const wxString& outputDirectory) {
    try {
        const auto directory = std::filesystem::u8path(nstr(outputDirectory));
        std::filesystem::create_directories(directory);
        PrepareRegistry();
        CopyBuffer copy;
        MapVersion version(MAP_OTBM_2, 57);
        const wxString prefabFile = outputDirectory + "/authored.otbm";
        auto blueprint = WorldGen::DefaultBlueprints()[1];
        blueprint.id = "authored_house";
        blueprint.maxLevels = 3;
        blueprint.allowRotation = false;
        blueprint.minPerSettlement = 1;
        blueprint.upperRows = blueprint.rows;
        for (auto& row : blueprint.upperRows) for (auto& cell : row) if (cell == '+') cell = '#';
        {
            Editor source(copy, version);
            source.map.setWidth(256); source.map.setHeight(256);
            for (int floor = 0; floor < 3; ++floor) {
                const auto& rows = WorldGen::BlueprintRowsForFloor(blueprint, floor);
                for (int y = 0; y < int(rows.size()); ++y) for (int x = 0; x < int(rows[y].size()); ++x) {
                    if (rows[y][x] == ' ') continue;
                    auto* tile = source.map.createTile(40 + x, 50 + y, 7 - floor);
                    tile->addItem(Item::Create(104));
                    if (rows[y][x] == '#') tile->addItem(Item::Create(200));
                    if (x == 3 && y == 3) {
                        auto* prop = Item::Create(301); prop->setText("Authored room " + std::to_string(floor));
                        tile->addItem(prop); tile->addItem(Item::Create(300));
                    }
                    tile->update();
                }
            }
            MapCodec codec(version);
            MemoryNodeFileWriteHandle encoded;
            Check(codec.Write(source.map, encoded), "Encode the authored multi-floor OTBM fixture");
            std::ofstream file(directory / "authored.otbm", std::ios::binary);
            file.write("OTBM", 4); file.write(reinterpret_cast<const char*>(encoded.getMemory()), std::streamsize(encoded.getSize()));
        }
        auto writeCatalog = [&](const std::filesystem::path& path) {
            std::ofstream out(path, std::ios::binary);
            out << R"({"version":1,"templates":[{"id":"authored_house","settlements":[1],"maxLevels":3,"rotate":false,"minPerSettlement":1,"rows":[)";
            for (size_t y = 0; y < blueprint.rows.size(); ++y) { if (y) out << ','; out << '"' << blueprint.rows[y] << '"'; }
            out << R"(],"upperRows":[)";
            for (size_t y = 0; y < blueprint.upperRows.size(); ++y) { if (y) out << ','; out << '"' << blueprint.upperRows[y] << '"'; }
            out << R"(],"prefab":{"file":"authored.otbm","origin":[40,50,7]}}]})";
        };
        writeCatalog(directory / "catalog.json");
        {
            WorldGen::BlueprintCatalog parsed;
            std::string reason;
            Check(WorldGen::LoadBlueprintCatalog(nstr((outputDirectory + "/catalog.json")), parsed, reason) && parsed.front().settlements == std::vector<int>{1}, "JSON catalog must retain settlement bindings");
            std::ifstream input(directory / "catalog.json");
            const std::string original((std::istreambuf_iterator<char>(input)), {});
            for (const auto& invalid : {"[0]", "[33]", "[1,1]", "[1.5]", "[\"1\"]", "1"}) {
                std::string bytes = original;
                const auto position = bytes.find("\"settlements\":[1]");
                bytes.replace(position, std::string("\"settlements\":[1]").size(), std::string("\"settlements\":") + invalid);
                { std::ofstream file(directory / "bad-catalog.json"); file << bytes; }
                Check(!WorldGen::LoadBlueprintCatalog(nstr((outputDirectory + "/bad-catalog.json")), parsed, reason), "Malformed settlement bindings must fail JSON loading");
            }
        }
        // No BuildingFloor mapping or wall brush is needed for an authored building.
        {
            std::ofstream out(directory / "legend.json"); out << "{\"entries\":[";
            bool first = true;
            for (int t = 0; t < int(WorldGen::Terrain::Void); ++t) {
                if (t == int(WorldGen::Terrain::BuildingFloor)) continue;
                if (!first) out << ','; first = false;
                const auto terrain = WorldGen::Terrain(t);
                const auto color = wxString::Format("#%06X", unsigned(WorldGen::TerrainColor(terrain)));
                out << "{\"color\":\"" << nstr(color) << "\",\"tileId\":" << ((WorldGen::IsWater(terrain) || terrain == WorldGen::Terrain::Mountain || terrain == WorldGen::Terrain::CaveWall) ? 101 : 100) << '}';
            }
            out << "]}";
        }
        Editor editor(copy, version);
        editor.map.setWidth(256); editor.map.setHeight(256);
        GenerationSpec spec;
        spec.source = GenerationSource::WorldPlan;
        spec.region.originX = spec.region.originY = 16;
        spec.region.width = spec.region.height = 192;
        spec.world.settings.settlementCount = 1;
        spec.world.settings.buildingsPerSettlement = 2;
        spec.world.settings.buildingLevels = 3;
        spec.world.settings.coastRoughness = 0;
        spec.world.connectionItemId = 103;
        spec.world.wallBrushName.clear();
        spec.world.catalogPath = outputDirectory + "/catalog.json";
        spec.imageMask.legendPath = outputDirectory + "/legend.json";
        spec.pipeline.borderizeAfter = true;
        spec.pipeline.randomizeGround = true;
        const auto before = MapHash(editor.map);
        GenerationSpec resolved;
        WorldGen::Plan plan;
        wxString error;
        Check(WorldEditor::BuildPlan(editor, spec, resolved, plan, error), "Build authored OTBM plan: " + nstr(error));
        Check(!plan.buildings.empty() && plan.buildings.front().rotation == 0, "Prefab catalog must actually place fixed-orientation buildings");
        WorldEditor::ApplicationStats stats;
        GameSprite wallExtent;
        wallExtent.width = wallExtent.height = 2;
        GameSprite* previousWallSprite = g_items[200].sprite;
        g_items[200].sprite = &wallExtent;
        const bool appliedPrefab = WorldEditor::ApplyPlan(editor, resolved, plan, error, &stats);
        g_items[200].sprite = previousWallSprite;
        Check(appliedPrefab, "Apply authored multi-floor OTBM: " + nstr(error));
        Check(stats.prefabBuildings == plan.buildings.size() && stats.prefabTiles == plan.buildings.size() * blueprint.rows.size() * blueprint.rows.front().size() * 3, "Count actual prefab buildings and copied tiles");
        for (const auto& building : plan.buildings) for (int floor = 0; floor < building.levels; ++floor) {
            const auto* tile = editor.map.getTile(resolved.region.originX + building.origin.x + 3, resolved.region.originY + building.origin.y + 3, resolved.region.z - floor);
            Check(tile && tile->ground && tile->ground->getID() == 104 && tile->items.size() == 2, "Preserve exact authored ground and furniture stacks despite automatic borders/randomization");
            bool text = false, blocked = false;
            for (const auto* item : tile->items) { text = text || (item->getID() == 301 && item->getText() == "Authored room " + std::to_string(floor)); blocked = blocked || item->getID() == 300; }
            Check(text && blocked, "Preserve authored attributes and allow connected blocking furniture");
        }
        CheckTravel(editor, resolved, plan);
        const auto generated = MapHash(editor.map);
        MapCodec codec(version);
        MemoryNodeFileWriteHandle encoded;
        Check(codec.Write(editor.map, encoded), "Export the generated authored world");
        Editor reopened(copy, version);
        MemoryNodeFileReadHandle nodes(encoded.getMemory(), encoded.getSize());
        Check(codec.Read(reopened.map, nodes) && MapHash(reopened.map) == generated, "Authored structures and generated links must survive production OTBM readback");
        const auto& readBuilding = plan.buildings.front();
        const auto* readTile = reopened.map.getTile(resolved.region.originX + readBuilding.origin.x + 3, resolved.region.originY + readBuilding.origin.y + 3, resolved.region.z - 2);
        Check(readTile && std::any_of(readTile->items.begin(), readTile->items.end(), [](const Item* item) { return item->getID() == 301 && item->getText() == "Authored room 2"; }), "OTBM readback must preserve authored text attributes too");
        editor.actionQueue->undo();
        Check(MapHash(editor.map) == before && !editor.actionQueue->canUndo(), "Prefab application must undo as one operation");
        editor.actionQueue->redo();
        Check(MapHash(editor.map) == generated, "Prefab application must redo exactly");
        editor.actionQueue->undo();
        Check(!WorldEditor::ApplyPlan(editor, resolved, plan, error, nullptr, [](const char* stage, int) { return std::string(stage) != "Loading OTBM construction prefabs"; }) && error == "World generation cancelled." && MapHash(editor.map) == before, "Prefab loading cancellation must preserve target tiles and history");
        wallExtent.width = 3;
        g_items[200].sprite = &wallExtent;
        const bool oversizedWall = WorldEditor::ApplyPlan(editor, resolved, plan, error);
        g_items[200].sprite = previousWallSprite;
        Check(!oversizedWall && error.Contains("one-cell wall margin") && MapHash(editor.map) == before, "Wall overhangs larger than the reserved margin must preserve the target map");
        std::ifstream original(directory / "authored.otbm", std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(original)), {}); original.close();
        auto writeBytes = [&](const std::string& value) { std::ofstream(directory / "authored.otbm", std::ios::binary).write(value.data(), std::streamsize(value.size())); };
        writeBytes(bytes + "changed");
        Check(!WorldEditor::ApplyPlan(editor, resolved, plan, error) && error.Contains("changed since preview") && MapHash(editor.map) == before, "Changed prefab bytes must invalidate a displayed plan");
        auto reject = [&](const std::string& value, const char* message) {
            writeBytes(value);
            GenerationSpec candidate; WorldGen::Plan invalid;
            const bool planned = WorldEditor::BuildPlan(editor, spec, candidate, invalid, error);
            Check((!planned || !WorldEditor::ApplyPlan(editor, candidate, invalid, error)) && !error.empty() && MapHash(editor.map) == before && !editor.actionQueue->canUndo(), message);
        };
        reject(bytes + "trailing", "Reject trailing nodes/bytes before the legacy reader");
        reject(bytes.substr(0, bytes.size() - 1), "Reject truncated prefab trees");
        auto unknownItem = bytes;
        const auto itemOffset = unknownItem.find(std::string("\xfe\x06\x2d\x01", 4));
        Check(itemOffset != std::string::npos, "Fixture must contain authored item nodes");
        unknownItem[itemOffset + 2] = char(0xe7); unknownItem[itemOffset + 3] = char(0x03);
        reject(unknownItem, "Unknown source item IDs must not silently disappear from copied prefabs");

        auto nodeEnd = [&](size_t start) {
            int depth = 0;
            for (size_t i = start; i < bytes.size(); ++i) {
                const auto value = static_cast<unsigned char>(bytes[i]);
                if (value == 0xfd) ++i;
                else if (value == 0xfe) ++depth;
                else if (value == 0xff && --depth == 0) return i + 1;
            }
            throw std::runtime_error("Malformed fixture node");
        };
        const auto propEnd = nodeEnd(itemOffset);
        auto scriptedProp = bytes.substr(itemOffset, propEnd - itemOffset);
        scriptedProp.insert(scriptedProp.size() - 1, std::string("\x04\x39\x30", 3));
        auto script = bytes; script.replace(itemOffset, propEnd - itemOffset, scriptedProp);
        reject(script, "Reject authored script action IDs before changing the target map");
        FixtureItem(400, ITEM_GROUP_CONTAINER, false, ITEM_TYPE_CONTAINER);
        g_items[400].volume = 64;
        std::string nestedChain;
        for (int i = 0; i < 25; ++i) nestedChain += std::string("\xfe\x06\x90\x01", 4);
        nestedChain += bytes.substr(itemOffset, propEnd - itemOffset) + std::string(25, char(0xff));
        auto deeplyNested = bytes; deeplyNested.replace(itemOffset, propEnd - itemOffset, nestedChain);
        reject(deeplyNested, "Reject deep valid container nesting before recursive decoding");

        auto nestedScript = bytes;
        nestedScript.replace(itemOffset, propEnd - itemOffset, std::string("\xfe\x06\x90\x01", 4) + scriptedProp + char(0xff));
        reject(nestedScript, "Script IDs inside nested containers must not bypass preflight");
        auto nested = bytes;
        nested.replace(itemOffset, propEnd - itemOffset, std::string("\xfe\x06\x90\x01", 4) + bytes.substr(itemOffset, propEnd - itemOffset) + char(0xff));
        writeBytes(nested);
        GenerationSpec nestedResolved; WorldGen::Plan nestedPlan;
        const bool nestedBuilt = WorldEditor::BuildPlan(editor, spec, nestedResolved, nestedPlan, error);
        const bool nestedApplied = nestedBuilt && WorldEditor::ApplyPlan(editor, nestedResolved, nestedPlan, error);
        Check(nestedApplied, "Plain nested containers must copy successfully: " + nstr(error));
        editor.actionQueue->undo();
        Check(MapHash(editor.map) == before, "Authored nested containers must undo with the world");
        auto badWall = bytes;
        const auto wallOffset = badWall.find(std::string("\xfe\x06\xc8\x00", 4));
        Check(wallOffset != std::string::npos, "Fixture must contain explicit wall items");
        badWall[wallOffset + 2] = char(0x2d); badWall[wallOffset + 3] = char(0x01);
        reject(badWall, "Declared prefab walls must actually block in the active client");
        const auto tileOffset = bytes.find(std::string("\xfe\x05", 2));
        Check(tileOffset != std::string::npos, "Fixture must contain tile nodes");
        auto unterminatedZones = bytes;
        unterminatedZones.insert(tileOffset + 4, std::string("\x03\x40\x00\x00\x00\x01\x00", 7));
        reject(unterminatedZones, "Reject unterminated zone lists before the legacy reader can loop");
        auto missingGround = bytes;
        missingGround.erase(tileOffset + 4, 3);
        reject(missingGround, "Every declared prefab cell must have its own ground");
        auto outside = bytes;
        outside[tileOffset + 2] = char(200);
        reject(outside, "Reject source tiles outside the declared authored region");
        const auto areaOffset = bytes.find(std::string("\xfe\x04", 2));
        Check(areaOffset != std::string::npos, "Fixture must contain tile areas");
        auto invalidFloor = bytes;
        invalidFloor.replace(areaOffset + 6, 1, std::string("\xfd\xff", 2));
        reject(invalidFloor, "Reject escaped out-of-range floors before map allocation");
        auto invalidCoordinate = bytes;
        invalidCoordinate.replace(areaOffset + 2, 2, std::string("\xfd\xff\xfd\xff", 4));
        reject(invalidCoordinate, "Reject overflowing tile-area offsets before map allocation");
        auto wrongClient = bytes; wrongClient[18] = 58;
        reject(wrongClient, "Reject mismatched OTB client versions without prompting or mutation");
        auto future = bytes; future[6] = 5;
        reject(future, "Reject unsupported OTBM formats without prompting or mutation");
        reject(std::string(WorldGen::MaximumPrefabBytes + 1, 'x'), "Reject oversized prefab files before planning");
        writeBytes(bytes);
        std::filesystem::remove(directory / "authored.otbm");
        Check(!WorldEditor::ApplyPlan(editor, resolved, plan, error) && MapHash(editor.map) == before, "Missing source files must not partially apply a prefab world");
        writeBytes(bytes);
        const auto relocated = directory / std::filesystem::path(u8"relocated-\u00e7");
        std::filesystem::create_directory(relocated);
        std::ofstream(relocated / "authored.otbm", std::ios::binary).write(bytes.data(), std::streamsize(bytes.size()));
        writeCatalog(relocated / "catalog.json");
        const auto utf8 = (relocated / "catalog.json").u8string();
        auto moved = spec; moved.world.catalogPath = wxString::FromUTF8(std::string(utf8.begin(), utf8.end()));
        GenerationSpec movedResolved; WorldGen::Plan movedPlan;
        const bool movedBuilt = WorldEditor::BuildPlan(editor, moved, movedResolved, movedPlan, error);
        Check(movedBuilt && WorldGen::Fingerprint(movedPlan) == WorldGen::Fingerprint(plan), "Moving a catalog and identical prefab bytes to a Unicode folder must preserve the plan fingerprint");
        auto changedDigest = movedPlan;
        ++changedDigest.catalog.front().prefabFingerprint;
        Check(WorldGen::Fingerprint(changedDigest) != WorldGen::Fingerprint(plan), "Plan fingerprints must include authored prefab contents");
        std::cout << "PASS: authored OTBM catalog import, fixed multi-floor contents, attributes, physical navigation, digest pinning, cancellation, undo/redo and round-trip\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "FAIL: OTBM prefabs: " << exception.what() << '\n';
        return 1;
    }
}

static int RunPngScenario(const wxString& outputDirectory) {
	try {
		const auto directory = std::filesystem::u8path(nstr(outputDirectory));
		std::filesystem::create_directories(directory);
		PrepareRegistry();
		FixtureItem(303, ITEM_GROUP_NONE);
		g_items[303].isBorder = true;
		if (!wxImage::FindHandler(wxBITMAP_TYPE_PNG)) wxImage::AddHandler(new wxPNGHandler);
		const wxString imagePath = outputDirectory + "/mask.png", legendPath = outputDirectory + "/legend.json";
		wxImage image(5, 1);
		image.InitAlpha();
		const unsigned char colors[5][3] = {{0,255,0},{0,0,0},{0,255,255},{255,0,0},{0,0,255}};
		for (int x = 0; x < 5; ++x) {
			image.SetRGB(x, 0, colors[x][0], colors[x][1], colors[x][2]);
			image.SetAlpha(x, 0, x == 2 ? 0 : (x == 0 ? 1 : 255));
		}
		Check(image.SaveFile(imagePath, wxBITMAP_TYPE_PNG), "Write actual PNG mask fixture");
		std::ofstream(directory / "legend.json") << R"({"colors":[{"hex":"#00FF00","tileId":104},{"hex":"#000000","tileId":105},{"hex":"#00FFFF","tileId":104},{"hex":"#FF0000","tileId":0}]})";
		CopyBuffer copy;
		MapVersion version(MAP_OTBM_2, 57);
		Editor editor(copy, version);
		editor.map.setWidth(64);
		editor.map.setHeight(64);
		for (int y = 8; y < 10; ++y) for (int x = 8; x < 18; ++x) {
			auto* tile = editor.map.createTile(x, y, 7);
			tile->addItem(Item::Create(100));
			if (x == 8 && y == 8) tile->addBorderItem(Item::Create(303));
			tile->update();
		}
		const uint64_t before = MapHash(editor.map);
		GenerationSpec spec;
		spec.source = GenerationSource::ImageMask;
		spec.region.originX = spec.region.originY = 8;
		spec.region.width = 10;
		spec.region.height = 2;
		spec.imageMask.imagePath = imagePath;
		spec.imageMask.legendPath = legendPath;
		spec.pipeline.borderizeAfter = false;
		const int previousAutomagic = g_settings.getInteger(Config::USE_AUTOMAGIC);
		g_settings.setInteger(Config::USE_AUTOMAGIC, 1);
		wxString error;
		const bool generated = ProceduralGenerator::Run(editor, spec, error);
		g_settings.setInteger(Config::USE_AUTOMAGIC, previousAutomagic);
		Check(generated, "Actual PNG import failed: " + nstr(error));
		for (int y = 8; y < 10; ++y) for (int x = 8; x < 18; ++x) {
			const int category = (x - 8) / 2;
			const auto* tile = editor.map.getTile(x, y, 7);
			Check(tile && tile->ground && tile->ground->getID() == (category == 0 ? 104 : category == 1 ? 105 : 100), "PNG import must preserve exact/black IDs and skip alpha-zero, zero-ID and unmapped colors after resize");
		}
		const auto* bordered = editor.map.getTile(8, 8, 7);
		Check(bordered->items.size() == 1 && bordered->items.front()->getID() == 303, "Disabling recipe borders must preserve existing borders even with global automagic enabled");
		const auto generatedHash = MapHash(editor.map);
		editor.actionQueue->undo();
		Check(MapHash(editor.map) == before && !editor.actionQueue->canUndo(), "Raw PNG import must undo in one operation");
		editor.actionQueue->redo();
		Check(MapHash(editor.map) == generatedHash, "Raw PNG import must redo exactly");
		MapCodec codec(version);
		MemoryNodeFileWriteHandle encoded;
		Check(codec.Write(editor.map, encoded), "Encode imported PNG as OTBM");
		Editor reopened(copy, version);
		MemoryNodeFileReadHandle nodes(encoded.getMemory(), encoded.getSize());
		Check(codec.Read(reopened.map, nodes) && MapHash(reopened.map) == generatedHash, "OTBM must retain imported PNG IDs and skipped tiles");
		std::cout << "PASS: PNGtoOTBM import, alpha, categorical resize, border opt-out, undo/redo and OTBM readback\n";
		return 0;
	} catch (const std::exception& exception) {
		std::cerr << "FAIL: PNG import: " << exception.what() << '\n';
		return 1;
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
		Tile* equivalentTile = editor.map.createTile(450, 450, 7);
		equivalentTile->addItem(Item::Create(105));
		equivalentTile->addBorderItem(Item::Create(106));
		equivalentTile->update();
		const uint64_t before = MapHash(editor.map);
		GenerationSpec spec;
		spec.source = GenerationSource::WorldPlan;
		spec.world.settings.coastRoughness = 0;
		spec.world.settings.riverWidth = 1;
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
		spec.world.settings.caveEntrances = 3;
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
		const auto recipePath = wxString::FromUTF8((directory / "recipe.json").string());
		Check(ProceduralRecipe::Save(recipePath, spec, error, ProceduralRecipe::PathMode::RelativeToRecipe), "Production scenario must save a relative recipe");
		Check(ProceduralRecipe::Load(recipePath, spec, error), "Production scenario must reload its PNG/catalog paths from the recipe directory");
		Check(WorldEditor::BuildPlan(editor, spec, resolved, plan, error), "Real PNG must feed the production world planner: " + nstr(error));
		Check(!plan.buildings.empty() && !plan.links.empty(), "Smoke fixture must exercise buildings and vertical travel");
		const auto roles = WorldEditor::Render(plan, 0, WorldEditor::PreviewLayer::ConstructionRoles);
		const auto& firstBuilding = plan.buildings.front();
		const auto firstBlueprint = WorldGen::RotateBlueprint(plan.catalog[firstBuilding.blueprint], firstBuilding.rotation);
		bool inspectedRole = false;
		for (int y = 0; y < firstBuilding.height && !inspectedRole; ++y) {
			for (int x = 0; x < firstBuilding.width && !inspectedRole; ++x) {
				const int px = firstBuilding.origin.x + x, py = firstBuilding.origin.y + y;
				if (firstBlueprint.rows[y][x] == '.' && !(plan.FindLayer(0)->cells[plan.Index({px, py})].flags & WorldGen::Connection)) {
					const uint32_t color = WorldGen::RoleColor(WorldGen::BlueprintRole(firstBlueprint));
					Check(roles.GetRed(px, py) == uint8_t(color >> 16) && roles.GetGreen(px, py) == uint8_t(color >> 8) && roles.GetBlue(px, py) == uint8_t(color), "Construction role preview must color actual interior cells by role");
					inspectedRole = true;
				}
			}
		}
		Check(inspectedRole, "Role preview must contain an inspectable interior");
		// Script action IDs are protected even when ordinary terrain replacement is allowed.
		const Position questPosition(resolved.region.originX, resolved.region.originY, resolved.region.z);
		auto* questTile = editor.map.createTile(questPosition.x, questPosition.y, questPosition.z);
		questTile->addItem(Item::Create(100));
		GenerationSpec replacement = resolved;
		replacement.world.protectExisting = false;
		for (bool onGround : {true, false}) {
			Item* scripted = onGround ? questTile->ground : Item::Create(301);
			if (!onGround) questTile->addItem(scripted);
			scripted->setActionID(4500);
			questTile->update();
			Check(!WorldEditor::ApplyPlan(editor, replacement, plan, error) && error.Contains("protected tile"), "Action-ID grounds and items must remain protected when replacing ordinary tiles");
			Check(editor.map.getTile(questPosition) == questTile && scripted->getActionID() == 4500 && !editor.actionQueue->canUndo(), "Scripted tile rejection must preserve the tile, action ID and undo history");
			scripted->setActionID(0);
		}
		delete editor.map.swapTile(questPosition, nullptr);
		Check(MapHash(editor.map) == before, "Removing the quest fixture must restore the original map");
		Check(!WorldEditor::ApplyPlan(editor, resolved, plan, error, nullptr, {}, 1) && MapHash(editor.map) == before && !editor.actionQueue->canUndo(), "Tile budget rejection must happen before staging or undo mutations");
		Check(!WorldEditor::ApplyPlan(editor, resolved, plan, error, nullptr, {}, WorldEditor::MaximumTileBudget + 1) && MapHash(editor.map) == before, "Tile budgets above the supported memory bound must fail");
		auto wrongSeed = resolved;
		wrongSeed.seed ^= 1u;
		const bool acceptedWrongSeed = WorldEditor::ApplyPlan(editor, wrongSeed, plan, error);
		Check(!acceptedWrongSeed && error.Contains("seed") && MapHash(editor.map) == before && !editor.actionQueue->canUndo(), "Mismatched seeds must fail before staging or undo mutations");
		auto wrongOwnership = plan;
		wrongOwnership.settlements[0].buildings.clear();
		Check(!WorldEditor::ApplyPlan(editor, resolved, wrongOwnership, error) && MapHash(editor.map) == before, "Malformed settlement ownership must fail before editor mutation");
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
		size_t describedTiles = 0;
		const auto description = WorldEditor::Describe(plan, &describedTiles);
		Check(describedTiles == stats.changedTiles && description.Contains("Editor budget"), "Preview must report the actual whole-world tile count before application");
		CheckTravel(editor, resolved, plan);

		{
			auto local=resolved;local.world.protectExisting=false;local.world.objectSeed=934521;
			local.world.natureMask.assign(size_t(plan.settings.width)*plan.settings.height,0);
			for(int y=12;y<plan.settings.height/2;++y)for(int x=12;x<plan.settings.width/2;++x)local.world.natureMask[size_t(y)*plan.settings.width+x]=1;
            Check(WorldGen::PaintNatureMask(local.world.natureMask,plan.settings.width,plan.settings.height,{12,12},{plan.settings.width/2,plan.settings.height/2},6,true),"Paint a continuous local selection using the preview brush engine");
            Check(WorldGen::PaintNatureMask(local.world.natureMask,plan.settings.width,plan.settings.height,{24,24},{40,24},3,false),"Erase a preservation stroke before applying the local selection");
			std::vector<std::pair<Position,Tile*>> untouched;
			for(const auto& layer:plan.layers)for(int y=0;y<plan.settings.height;++y)for(int x=0;x<plan.settings.width;++x) {
				const auto i=size_t(y)*plan.settings.width+x;const auto& cell=layer.cells[i];
				const bool natural=cell.terrain==WorldGen::Terrain::Grass || cell.terrain==WorldGen::Terrain::Forest || cell.terrain==WorldGen::Terrain::Desert || cell.terrain==WorldGen::Terrain::Snow || cell.terrain==WorldGen::Terrain::Beach;
				if(layer.offsetZ!=0 || !local.world.natureMask[i] || !natural || (cell.flags&WorldGen::Reserved)) {
					Position pos(local.region.originX+x,local.region.originY+y,local.region.z+layer.offsetZ);untouched.push_back({pos,editor.map.getTile(pos)});
				}
			}
			WorldEditor::ApplicationStats localStats;
			const auto basePreview=WorldEditor::Render(plan,0);
			const auto scopedPreview=WorldEditor::Render(plan,0,WorldEditor::PreviewLayer::Terrain,&local);
            const auto accessPreview=WorldEditor::Render(plan,0,WorldEditor::PreviewLayer::PathDistance,&local);
            Check(accessPreview.IsOk() && accessPreview.GetWidth()==plan.settings.width,"Path-access diagnostic renders the complete selected floor");
			Check(scopedPreview.IsOk() && scopedPreview.GetBlue(0,0)==basePreview.GetBlue(0,0)/4,"Local preview dims preserved cells");
			const auto surfaceStart=std::chrono::steady_clock::now();
			const bool localApplied=WorldEditor::ApplyPlan(editor,local,plan,error,&localStats);
			if(std::getenv("RME_WORLD_PERF"))std::cout << "METRIC surface " << blocks << ' ' << std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-surfaceStart).count() << ' ' << MapHash(editor.map) << '\n';
			Check(localApplied,"Local nature generation validates the combined map: "+nstr(error));
			Check(localStats.preparedTiles==localStats.changedTiles && localStats.contextCopies>localStats.preparedTiles,"Only selected tiles are rebuilt; unchanged context is copied once");
			Check(localStats.changedTiles>0 && localStats.changedTiles<stats.changedTiles && localStats.furniture==0,"Local edit applies only selected natural tiles");
			for(const auto& [pos,tile]:untouched)Check(editor.map.getTile(pos)==tile,"Every unselected tile, road, building and other floor retains its original tile object");
			const auto localHash=MapHash(editor.map);
			editor.actionQueue->undo();Check(MapHash(editor.map)==applied,"Local nature edit undoes as one transaction");
			editor.actionQueue->redo();Check(MapHash(editor.map)==localHash,"Local nature edit redoes exactly");
			editor.actionQueue->undo();
			local.world.natureFloor=1;
			const auto caveStart=std::chrono::steady_clock::now();
			const bool caveApplied=WorldEditor::ApplyPlan(editor,local,plan,error,&localStats);
			if(std::getenv("RME_WORLD_PERF"))std::cout << "METRIC cave " << blocks << ' ' << std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-caveStart).count() << ' ' << MapHash(editor.map) << '\n';
			Check(caveApplied && localStats.changedTiles>0,"The same local-mask transaction supports underground nature: "+nstr(error));
			editor.actionQueue->undo();Check(MapHash(editor.map)==applied,"Underground nature regeneration undoes without changing any other floor");
			local.world.natureFloor=0;
			bool preserveVisited=false;
			const bool cancelledLocal=WorldEditor::ApplyPlan(editor,local,plan,error,nullptr,[&](const char* stage,int) { if(std::string(stage)=="Preserving unselected tiles"){preserveVisited=true;return false;}return true; });
			Check(!cancelledLocal && preserveVisited && MapHash(editor.map)==applied,"Cancellation while combining untouched tiles preserves the complete original map");
			local.world.natureFloor=8;
			Check(!WorldEditor::ApplyPlan(editor,local,plan,error) && MapHash(editor.map)==applied,"Missing mask floor fails without changing the map");
			local.world.natureFloor=0;std::fill(local.world.natureMask.begin(),local.world.natureMask.end(),0);
			Check(!WorldEditor::ApplyPlan(editor,local,plan,error) && MapHash(editor.map)==applied,"Empty effective mask fails atomically");
			local.world.natureMask.clear();local.world.natureMaskPath="unresolved-mask.png";
			Check(!WorldEditor::ApplyPlan(editor,local,plan,error) && MapHash(editor.map)==applied,"An unresolved mask path cannot silently fall back to replacing the complete world");
		}

		editor.actionQueue->undo();
		Check(MapHash(editor.map) == before, "One undo must restore terrain, walls, travel and untouched tiles");
		Check(!editor.actionQueue->canUndo(), "World generation must occupy one undo entry");
		editor.actionQueue->redo();
		Check(MapHash(editor.map) == applied, "Redo must restore exact generated content");
		Check(!WorldEditor::ApplyPlan(editor, resolved, plan, error) && MapHash(editor.map) == applied, "Collision rejection must leave existing world unchanged");
		editor.actionQueue->undo();
		auto styled = plan;
		for (auto& blueprint : styled.catalog) {
			blueprint.floorBrush = "fixture floor";
			blueprint.wallBrush = "a stone decoy";
		}
		GenerationSpec styledSpec = resolved;
		styledSpec.world.wallBrushName.clear(); // Every used template supplies its own wall.
		Check(WorldEditor::ApplyPlan(editor, styledSpec, styled, error), "Exact template floors/walls must replace inherited materials: " + nstr(error));
		for (const auto& building : styled.buildings) {
			for (int floor = 0; floor < building.levels; ++floor) {
				const Tile* entrance = editor.map.getTile(resolved.region.originX + building.entrance.x, resolved.region.originY + building.entrance.y, resolved.region.z - floor);
				Check(entrance && entrance->ground->getID() == 104, "Styled floor must apply on every building level");
				const Tile* corner = editor.map.getTile(resolved.region.originX + building.origin.x, resolved.region.originY + building.origin.y, resolved.region.z - floor);
				Check(std::any_of(corner->items.begin(), corner->items.end(), [](const Item* item) { return item->getID() >= 204 && item->getID() <= 207; }), "Styled walls must use the exact per-template brush");
			}
		}
		CheckTravel(editor, styledSpec, styled);
		editor.actionQueue->undo();
		Check(MapHash(editor.map) == before, "Styled construction application must remain one undo operation");
		auto floorStyled = styled;
		for (auto& blueprint : floorStyled.catalog) {
			blueprint.maxLevels = 4;
			blueprint.floorMaterials.resize(4);
			blueprint.floorMaterials[1] = { "fixture equivalent ground", "stone" };
			blueprint.floorMaterials[3] = { "missing unused floor", "missing unused wall" };
		}
		const bool appliedFloorStyles = WorldEditor::ApplyPlan(editor, styledSpec, floorStyled, error);
		Check(appliedFloorStyles, "Used floors must resolve their own bindings, ignoring unused floors: " + nstr(error));
		for (const auto& building : floorStyled.buildings) {
			for (int floor = 0; floor < building.levels; ++floor) {
				const Tile* corner = editor.map.getTile(resolved.region.originX + building.origin.x, resolved.region.originY + building.origin.y, resolved.region.z - floor);
				Check(corner && corner->ground && corner->ground->getID() == (floor == 1 ? 105 : 104), "Per-floor grounds must override or inherit independently");
				const int firstWall = floor == 1 ? 200 : 204;
				Check(std::any_of(corner->items.begin(), corner->items.end(), [&](const Item* item) { return item->getID() >= firstWall && item->getID() <= firstWall + 3; }), "Per-floor walls must override or inherit independently");
			}
		}
		CheckTravel(editor, styledSpec, floorStyled);
		editor.actionQueue->undo();
		Check(MapHash(editor.map) == before, "Per-floor materials must undo with the complete world");
		for (const bool floorBinding : { true, false }) {
			auto missingFloorStyle = floorStyled;
			for (auto& blueprint : missingFloorStyle.catalog) (floorBinding ? blueprint.floorMaterials[1].floorBrush : blueprint.floorMaterials[1].wallBrush) = "missing used-floor style";
			Check(!WorldEditor::ApplyPlan(editor, styledSpec, missingFloorStyle, error) && MapHash(editor.map) == before, "Missing used-floor bindings must reject before editor mutations");
		}
		for (const bool floorBinding : { true, false }) {
			auto missingStyle = styled;
			for (auto& blueprint : missingStyle.catalog) (floorBinding ? blueprint.floorBrush : blueprint.wallBrush) = "missing exact style";
			Check(!WorldEditor::ApplyPlan(editor, styledSpec, missingStyle, error) && MapHash(editor.map) == before, "Missing exact template materials must reject staging without fallback or mutations");
		}

		auto groundReplacingFurniture = plan;
		for (auto& b : groundReplacingFurniture.catalog) for (auto& object : b.objects) object.brushes = { "fixture ground-replacing composite" };
		Check(!WorldEditor::ApplyPlan(editor, resolved, groundReplacingFurniture, error) && MapHash(editor.map) == before, "Ground-equivalent composite furniture must reject preparation without replacing planned floors");
		auto editorOnlyFurniture = plan;
		for (auto& b : editorOnlyFurniture.catalog) for (auto& object : b.objects) object.brushes = { "fixture editor-only object" };
		Check(!WorldEditor::ApplyPlan(editor, resolved, editorOnlyFurniture, error) && MapHash(editor.map) == before, "Required editor-only furniture must fail instead of being counted and silently omitted from OTBM");
		g_items[100].is_metaitem = true;
		const bool acceptedMetaGround = WorldEditor::ApplyPlan(editor, resolved, plan, error);
		g_items[100].is_metaitem = false;
		Check(!acceptedMetaGround && MapHash(editor.map) == before, "Editor-only ground variants must fail before changing the map");
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

static int RunPhysicalMaskScenario(const wxString& outputDirectory) {
	try {
		const std::filesystem::path directory=nstr(outputDirectory);
		std::filesystem::create_directories(directory);PrepareRegistry();WriteLegend(directory/"legend.json");
		for(int i=0;i<5;++i){FixtureItem(320+i,ITEM_GROUP_NONE);g_items[320+i].moveable=false;}
		g_items[320].floorChangeDown=true;g_items[321].floorChangeNorth=true;g_items[322].floorChangeEast=true;g_items[323].floorChangeSouth=true;g_items[324].floorChangeWest=true;
		MapVersion version;version.otbm=MAP_OTBM_2;version.client=ClientVersionID(57);
		CopyBuffer buffer;Editor editor(buffer,version);editor.map.setWidth(128);editor.map.setHeight(128);
		GenerationSpec spec;spec.source=GenerationSource::WorldPlan;spec.region.width=spec.region.height=64;
		spec.world.settings.settlementCount=1;spec.world.settings.buildingsPerSettlement=0;spec.world.settings.buildingLevels=1;
		spec.world.settings.caveDepth=1;spec.world.settings.physicalStairs=true;spec.world.stairItems={320,321,322,323,324};
		spec.world.connectionItemId=103;spec.world.settings.riverCount=0;spec.imageMask.legendPath=wxstr((directory/"legend.json").string());
		spec.doodads.enabled=true;spec.doodads.density=20;
		GenerationSpec resolved;WorldGen::Plan plan;wxString error;
		Check(WorldEditor::BuildPlan(editor,spec,resolved,plan,error),"Physical local fixture plan: "+nstr(error));
		Check(WorldEditor::ApplyPlan(editor,resolved,plan,error),"Physical local fixture apply: "+nstr(error));
		const auto before=MapHash(editor.map);std::vector<std::pair<Position,Tile*>> endpoints;
		for(const auto& link:plan.links)for(const auto& endpoint:{std::pair{link.upper,link.upperOffsetZ},std::pair{link.lower,link.lowerOffsetZ}}){Position p(endpoint.first.x,endpoint.first.y,7+endpoint.second);endpoints.push_back({p,editor.map.getTile(p)});}
		resolved.world.protectExisting=false;resolved.world.natureMask.assign(4096,1);resolved.world.objectSeed=4911;
		WorldEditor::ApplicationStats stats;
		const bool accepted=WorldEditor::ApplyPlan(editor,resolved,plan,error,&stats);
		Check(accepted && stats.preparedTiles==stats.changedTiles && stats.changedTiles>0,"Physical stairs survive selective staging: "+nstr(error));
		for(const auto& [p,tile]:endpoints)Check(editor.map.getTile(p)==tile,"Physical endpoint tiles retain identity instead of acquiring duplicate travel items");
		editor.actionQueue->undo();Check(MapHash(editor.map)==before,"Selective update with directional stairs undoes exactly");
		std::cout << "PASS: masked nature around physical stairs, preserved endpoints, no duplicate floor changes and exact undo\n";
		return 0;
	} catch(const std::exception& exception){std::cerr << "FAIL: physical local mask: " << exception.what() << '\n';return 1;}
}

static int RunBatchScenario(const wxString& outputDirectory) {
	try {
		const std::filesystem::path directory = nstr(outputDirectory);
		WriteBatchRegistry(directory);
		const auto data = directory / "batch-data";
		std::filesystem::create_directories(data / "procedural");
		std::ofstream(data / "procedural/default_legend.json") << "{}";
		std::ofstream(data / "procedural/biome_doodads.json", std::ios::binary) << "\xef\xbb\xbf" << R"({"presets":{"forest":{"keywords":["tree"]}}})";
		WorldBatch::Options options;
		options.dataDirectory = wxString::FromUTF8(data.string());
		options.townPrefix = "Fixture";
		options.verifyOutput = true;
		options.recipe = outputDirectory + "/recipe.json";
		options.itemsOtb = outputDirectory + "/batch-items.otb";
		options.itemsXml = outputDirectory + "/batch-items.xml";
		options.materials = outputDirectory + "/batch-materials.xml";
		options.outputDirectory = outputDirectory + "/batch-" + std::to_string(wxGetProcessId()) + "-" + std::to_string(wxGetUTCTimeMillis().GetValue());
		WorldBatch::Result result;
		wxString error;
		const bool generatedBatch = WorldBatch::Generate(options, result, error);
		Check(generatedBatch, "Batch registry/XML/recipe generation: " + nstr(error));
		Check(std::none_of(result.warnings.begin(), result.warnings.end(), [](const wxString& warning) { return warning.Contains("Biome decoration settings could not be read"); }), "UTF-8 BOM biome settings must load without silently replacing configured keywords");
		Check(result.verified, "Batch verification must reopen and exactly re-encode the written OTBM bundle");
		Check(g_items[20001].isTeleport(), "Explicit registry client version must preserve modern item XML IDs");
		Check(g_items[65535].name == "maximum item", "Maximum server ID must load without a uint16 range-loop wrap");
		Check(result.application.changedTiles > 0 && result.application.furniture > 0, "Batch generation must use the full production adapter, including furniture");
		const auto output = std::filesystem::u8path(nstr(options.outputDirectory));
		std::ifstream file(output / "world.otbm", std::ios::binary);
		std::string bytes((std::istreambuf_iterator<char>(file)), {});
		file.close();
		Check(bytes.size() > 4 && bytes.substr(0, 4) == "OTBM", "Batch artifact must use the real OTBM magic header");
		CopyBuffer copy;
		MapVersion version(MAP_OTBM_2, 57);
		Editor reopened(copy, version);
		MapCodec codec(version);
		MemoryNodeFileReadHandle memory(reinterpret_cast<const uint8_t*>(bytes.data() + 4), bytes.size() - 4);
		Check(codec.Read(reopened.map, memory), "Batch OTBM must reload using the production reader");
		Check(reopened.map.getSpawnFilename() == "world-spawns.xml" && reopened.map.getHouseFilename() == "world-houses.xml", "Batch OTBM must reference its emitted sidecars");
		for (const char* filename : { "world-spawns.xml", "world-houses.xml" }) {
			pugi::xml_document document;
			Check(bool(document.load_file((output / filename).wstring().c_str())), "Batch XML sidecars must be valid");
		}
		GenerationSpec spec, resolved;
		WorldGen::Plan plan;
		Check(ProceduralRecipe::Load(options.recipe, spec, error), "Original batch recipe must reload");
		Check(WorldEditor::BuildPlan(reopened, spec, resolved, plan, error), "Batch plan must be reproducible from original inputs");
		Check(WorldGen::Fingerprint(plan) == result.fingerprint, "Batch result must record the exact semantic plan");
		Check(reopened.map.towns.count() == plan.settlements.size() && result.towns == plan.settlements.size(), "Generated town records must survive the production OTBM reader");
		for (const auto& pair : reopened.map.towns) {
			const Town* town = pair.second;
			Check(town->getName() == "Fixture " + std::to_string(pair.first), "Town IDs and names must be deterministic");
			const Tile* tile = reopened.map.getTile(town->getTemplePosition());
			Check(tile && tile->ground && !g_items[tile->ground->getID()].unpassable && tile->getLocation()->getTownCount() == 1, "Town temple position must have a walkable, protected tile");
			Check(std::none_of(tile->items.begin(), tile->items.end(), [](const Item* item) { return g_items[item->getID()].isTeleport() || g_items[item->getID()].unpassable; }), "Town spawn must never land directly on the cave portal");
		}
		CheckTravel(reopened, resolved, plan);
		std::ifstream reportFile(output / "generation.json");
		json::mValue report;
		Check(json::read(reportFile, report) && report.get_obj().at("planFingerprint").get_str() == std::to_string(result.fingerprint), "Batch report must record the exact plan identity");
		Check(report.get_obj().at("furniture").get_int64() > 0 && !report.get_obj().at("warnings").get_array().empty(), "Batch report must retain placement counts and metadata-availability warnings");
		const auto before = MapHash(reopened.map);
		WorldBatch::Options invalidPrefix = options;
		invalidPrefix.outputDirectory += "-invalid-prefix";
		invalidPrefix.townPrefix = wxString::FromUTF8(std::string("bad\0prefix", 10));
		const ItemType* existingRegistryItem = &g_items[65535];
		Check(!WorldBatch::Generate(invalidPrefix, result, error) && !wxDirExists(invalidPrefix.outputDirectory) && &g_items[65535] == existingRegistryItem && MapHash(reopened.map) == before, "Invalid town names must fail before registry/output mutations");

		Check(!WorldBatch::Generate(options, result, error) && MapHash(reopened.map) == before, "Existing output must be rejected before touching the registry or map");
		std::ifstream unchanged(output / "world.otbm", std::ios::binary);
		const std::string after((std::istreambuf_iterator<char>(unchanged)), {});
		Check(bytes == after, "Repeated batch requests must never replace existing output");
		std::cout << "PASS: batch OTB/XML/material/relative-recipe loading, modern/max item IDs, OTBM/XML bundle and no-overwrite protection\n";
		return 0;
	} catch (const std::exception& exception) {
		std::cerr << "FAIL: batch world: " << exception.what() << '\n';
		return 1;
	}
}

static int RunMetadataScenario(const wxString& outputDirectory) {
	try {
		const auto directory = std::filesystem::u8path(nstr(outputDirectory));
		const auto data = directory / "batch-data";
		std::ofstream(data / "clients.xml") << R"(<client_config><otbs><otb client="fixture" version="3" id="57"/></otbs><clients><client name="metadata fixture" data_directory="fixture" otb="fixture"><otbm version="2"/><data format="10.57" dat="12345678" spr="87654321"/></client></clients></client_config>)";
		const wxString clients = wxString::FromUTF8((data / "clients.xml").string());
		ClientVersion::loadVersions(clients);
		auto* version = ClientVersion::get(57);
		Check(version && version->getDatFormatForSignature(0x12345678) == DAT_FORMAT_1057, "Explicit clients.xml must register DAT signatures portably");
		const auto metadata = MetadataFixture();
		const wxString filename = outputDirectory + "/batch-client.dat";
		wxString error;
		wxArrayString warnings;
		for (const auto bounds : { std::pair<uint32_t, uint32_t> {0, 0}, {0, 100}, {100, 50}, {100, UINT32_MAX} }) {
			const auto custom = MetadataFixture(100, bounds.first, bounds.second, true);
			std::ofstream(std::filesystem::u8path(nstr(filename)), std::ios::binary).write(custom.data(), std::streamsize(custom.size()));
			g_gui.gfx.clear(); g_gui.gfx.client_version = version; warnings.clear(); error.clear();
			const bool loaded = g_gui.gfx.loadSpriteMetadata(filename, error, warnings);
			if (!bounds.first && !bounds.second) {
				Check(loaded && warnings.size() == 1, "Unset animation durations must load with an explicit fallback warning");
				auto* sprite = static_cast<GameSprite*>(g_gui.gfx.getSprite(100));
				Check(sprite && sprite->animator && sprite->animator->getFrameDuration(0)->getDuration() > 0 && sprite->animator->getFrameDuration(1)->getDuration() > 0, "Zero durations must never reach the renderer's animator");
			} else Check(!loaded && error.Contains("sprite 100"), "Inverted, partially zero and overflowing animation ranges must remain rejected with item context");
		}
		warnings.clear(); error.clear();
		for (size_t length : { size_t(0), size_t(4), size_t(10), size_t(12), size_t(15), metadata.size() - 1 }) {
			std::ofstream(std::filesystem::u8path(nstr(filename)), std::ios::binary).write(metadata.data(), std::streamsize(length));
			g_gui.gfx.clear();
			g_gui.gfx.client_version = version;
			Check(!g_gui.gfx.loadSpriteMetadata(filename, error, warnings) && !error.empty(), "Truncated DAT metadata must fail before uninitialized dimensions are used");
		}
		std::ofstream(std::filesystem::u8path(nstr(filename)), std::ios::binary).write(metadata.data(), std::streamsize(metadata.size()));
		g_gui.gfx.clear();
		g_gui.gfx.client_version = version;
		Check(g_gui.gfx.loadSpriteMetadata(filename, error, warnings), "Complete metadata through ID 65535 must load without wrapping");
		Check(g_gui.gfx.getSprite(65535) && static_cast<GameSprite*>(g_gui.gfx.getSprite(300))->width == 2, "Metadata must retain maximum ID and multi-tile dimensions");
		WorldBatch::Options options;
		options.recipe = outputDirectory + "/recipe.json";
		options.itemsOtb = outputDirectory + "/batch-items.otb";
		options.itemsXml = outputDirectory + "/batch-items.xml";
		options.materials = outputDirectory + "/batch-materials.xml";
		options.dataDirectory = wxString::FromUTF8(data.string());
		options.clientDat = filename;
		options.exportPreviews = false;
		options.outputDirectory = outputDirectory + "/metadata-" + std::to_string(wxGetProcessId()) + "-" + std::to_string(wxGetUTCTimeMillis().GetValue());
		WorldBatch::Result result;
		const wxString shortDat = outputDirectory + "/short-client.dat";
		const std::string shortBytes = MetadataFixture(103);
		std::ofstream(std::filesystem::u8path(nstr(shortDat)), std::ios::binary).write(shortBytes.data(), std::streamsize(shortBytes.size()));
		WorldBatch::Options incomplete = options;
		incomplete.clientDat = shortDat;
		incomplete.outputDirectory += "-incomplete";
		const bool acceptedIncomplete = WorldBatch::Generate(incomplete, result, error);
		Check(!acceptedIncomplete && error.Contains("sprite metadata") && !wxDirExists(incomplete.outputDirectory), "Valid DAT headers with missing used sprite IDs must fail before creating output");
		const bool generatedMetadata = WorldBatch::Generate(options, result, error);
		Check(generatedMetadata, "Batch generation with explicit metadata: " + nstr(error));
		Check(!wxFileExists(options.outputDirectory + "/preview_z7_terrain.png"), "Disabling preview export must leave the OTBM bundle usable without PNG artifacts");
		Check(g_items[300].sprite && g_items[300].sprite->width == 2 && result.application.furniture > 0, "Batch adapter must bind real metadata before placing decorations and furniture");
		Check(std::none_of(result.warnings.begin(), result.warnings.end(), [](const wxString& warning) { return warning.Contains("No client DAT supplied"); }), "Loaded metadata must not be reported as unavailable");
		std::cout << "PASS: explicit clients.xml/DAT, truncated metadata rejection, maximum sprite ID and metadata-backed batch generation\n";
		return 0;
	} catch (const std::exception& exception) {
		std::cerr << "FAIL: client metadata: " << exception.what() << '\n';
		return 1;
	}
}

int RunWorldEditorSelfTest(const wxString& outputDirectory) {
	const int prefabs = RunPrefabScenario(outputDirectory + "/otbm-prefabs");
	if (prefabs != 0) return prefabs;
	const int png = RunPngScenario(outputDirectory + "/png-import");
	if (png != 0) return png;
	const int village = RunWorldScenario(outputDirectory, false);
	if (village != 0) {
		return village;
	}
	const int city = RunWorldScenario(outputDirectory + "/city", true);
	if (city != 0) {
		return city;
	}
	const int physical = RunPhysicalMaskScenario(outputDirectory + "/physical-mask");
	if (physical != 0) return physical;
	const int batch = RunBatchScenario(outputDirectory);
	if (batch != 0) {
		return batch;
	}
	return RunMetadataScenario(outputDirectory);
}
