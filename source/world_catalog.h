#ifndef RME_WORLD_CATALOG_H_
#define RME_WORLD_CATALOG_H_

#include <string>
#include <cstdint>
#include <utility>
#include <vector>

namespace WorldGen {

	// A reusable client-independent construction footprint. Symbols:
	// # wall, . floor, + street entrance, a/b alternating vertical landings,
	// , courtyard ground, space = void (no tile above/below is implied).
	struct BlueprintProp {
		int x = 0, y = 0, floor = 0;
		// One orientation-independent brush or four clockwise orientation brushes.
		std::vector<std::string> brushes;
		bool required = false;
	};
	struct Blueprint {
		std::string id;
		int weight = 1;
		int maxLevels = 4;
		bool allowRotation = true;
		std::vector<std::string> rows;
		std::vector<std::string> upperRows;
		std::vector<std::string> biomes;
		std::vector<BlueprintProp> objects;
		// Exact client brush bindings; empty values inherit the world legend/settings.
		std::string floorBrush, wallBrush;
		std::string role;
		int maxPerSettlement = 0; // Zero means unlimited.
		int minPerSettlement = 0; // Required in each generated settlement of an allowed biome.
	};

	using BlueprintCatalog = std::vector<Blueprint>;

	bool ValidateBlueprint(const Blueprint& blueprint, std::string& error);
	bool ValidateCatalog(const BlueprintCatalog& catalog, std::string& error);
	Blueprint RotateBlueprint(const Blueprint& blueprint, int quarterTurns);
	std::pair<int, int> FindBlueprintSymbol(const std::vector<std::string>& rows, char symbol);
	const BlueprintCatalog& DefaultBlueprints();
	const std::string& BlueprintRole(const Blueprint& blueprint);
	uint32_t RoleColor(const std::string& role);

	// JSON/file adapter; compiled with the existing json_spirit parser.
	bool LoadBlueprintCatalog(const std::string& filename, BlueprintCatalog& catalog, std::string& error);

}
#endif
