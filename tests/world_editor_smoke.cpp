#include "main.h"

#include "world_editor.h"
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

	std::string MetadataFixture(uint16_t maximum = 65535) {
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
			for (int field = 0; field < 5; ++field) {
				put(1, 1); // Layers, patterns x/y/z, frames.
			}
			for (int pixel = 0; pixel < side * side; ++pixel) {
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
		Check(!WorldEditor::ApplyPlan(editor, resolved, plan, error, nullptr, {}, 1) && MapHash(editor.map) == before && !editor.actionQueue->canUndo(), "Tile budget rejection must happen before staging or undo mutations");
		Check(!WorldEditor::ApplyPlan(editor, resolved, plan, error, nullptr, {}, WorldEditor::MaximumTileBudget + 1) && MapHash(editor.map) == before, "Tile budgets above the supported memory bound must fail");
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
	const int village = RunWorldScenario(outputDirectory, false);
	if (village != 0) {
		return village;
	}
	const int city = RunWorldScenario(outputDirectory + "/city", true);
	if (city != 0) {
		return city;
	}
	const int batch = RunBatchScenario(outputDirectory);
	if (batch != 0) {
		return batch;
	}
	return RunMetadataScenario(outputDirectory);
}
