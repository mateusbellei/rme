#ifndef RME_WORLD_PLAN_H_
#define RME_WORLD_PLAN_H_

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <functional>
#include "world_catalog.h"

// Client-independent planning. No map, brush registry, GUI or global RNG access.
namespace WorldGen {
	inline constexpr uint32_t AlgorithmVersion = 1;

	enum class Terrain : uint8_t {
		Ocean,
		Lake,
		River,
		Beach,
		Grass,
		Forest,
		Desert,
		Snow,
		Mountain,
		Road,
		Bridge,
		Plaza,
		BuildingFloor,
		CaveWall,
		CaveFloor,
		Void
	};

	enum CellFlags : uint8_t {
		None = 0,
		Reserved = 1,
		Wall = 2,
		Entrance = 4,
		Connection = 8
	};

	struct Point {
		int x = 0, y = 0;
		bool operator==(const Point& other) const {
			return x == other.x && y == other.y;
		}
	};

	struct Cell {
		Terrain terrain = Terrain::Void;
		uint8_t flags = None;
		// Normalized 0..10000, independent of client ground IDs.
		uint16_t elevation = 0, moisture = 0, temperature = 0;
	};

	struct Layer {
		int offsetZ = 0;
		std::vector<Cell> cells;
	};

	struct Building {
		Point origin;
		int width = 0, height = 0, levels = 1;
		Point entrance;
		int settlement = -1;
		int blueprint = 0, rotation = 0;
	};

	struct Settlement {
		Point center;
		int radius = 0;
		Terrain biome = Terrain::Grass;
		std::vector<int> buildings;
	};

	// Explicit abstract edge. The editor must resolve it to client-compatible
	// floor-change items; terrain at two z coordinates alone is not a connection.
	struct VerticalLink {
		Point upper, lower;
		int upperOffsetZ = 0, lowerOffsetZ = 1;
		Point upperArrival, lowerArrival;
	};

	enum class LandShape { Continent,
						   Archipelago };

	enum class ReliefStyle { Smooth,
							 Ridges };

	enum class SettlementLayout { Organic,
								  Blocks };
	struct Settings {
		int width = 256, height = 256;
		uint32_t seed = 1337;
		LandShape shape = LandShape::Continent;
		int seaLevel = 4200;
		int coastRoughness = 55;
		ReliefStyle relief = ReliefStyle::Smooth;
		int biomeDetail = 0;
		int temperatureBias = 0, moistureBias = 0;
		int riverWidth = 3;
		int riverCount = 8;
		int settlementCount = 3;
		int buildingsPerSettlement = 8;
		int buildingLevels = 1;
		int caveDepth = 0;
		int caveFill = 45;
		int roadWidth = 2;
		SettlementLayout settlementLayout = SettlementLayout::Organic;
	};

	// Optional raster-derived inputs, already resampled to the target dimensions.
	// 65535/ Void preserve procedural terrain. Biome hints apply only on dry land;
	// drainage and subsequent settlement construction retain precedence.
	struct GeographyInput {
		std::vector<uint16_t> elevation;
		std::vector<Terrain> biomes;
	};

	struct Plan {
		Settings settings;
		BlueprintCatalog catalog;
		std::vector<Layer> layers;
		std::vector<Settlement> settlements;
		std::vector<Building> buildings;
		std::vector<VerticalLink> links;
		std::vector<std::string> warnings;
		// Surface drainage graph: downstream index, -1 for ocean/boundary outlet.
		std::vector<int> drainage;
		std::vector<uint32_t> flow;
		Layer* FindLayer(int offsetZ);
		const Layer* FindLayer(int offsetZ) const;
		bool Contains(Point p) const;
		size_t Index(Point p) const;
	};

	struct Report {
		bool valid = true;
		size_t landTiles = 0, waterTiles = 0, roadTiles = 0;
		int surfaceComponents = 0, caveComponents = 0;
		std::vector<std::string> errors;
	};

	bool IsWater(Terrain terrain);
	bool IsWalkable(const Cell& cell);
	const char* TerrainName(Terrain terrain);
	uint32_t TerrainColor(Terrain terrain);
	bool ValidateSettings(const Settings& settings, std::string& error);
	using ProgressCallback = std::function<bool(const char* stage, int percent)>;
	bool Build(const Settings& settings, Plan& plan, std::string& error, const BlueprintCatalog* catalog = nullptr, const GeographyInput* input = nullptr, const ProgressCallback& progress = {});
	Report Validate(const Plan& plan);
	uint64_t Fingerprint(const Plan& plan);

	// Joins all interior walkable components without opening the enclosing wall.
	// Used for cave masks as well as tests with adversarial disconnected layouts.
	int ConnectCave(std::vector<Cell>& cells, int width, int height);

}
#endif
