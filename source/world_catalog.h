#ifndef RME_WORLD_CATALOG_H_
#define RME_WORLD_CATALOG_H_

#include <string>
#include <cstddef>
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
		int placementRadius = 0; // Optional nearest safe anchor, Manhattan distance 0..3.
	};
	struct BlueprintMaterial {
		std::string floorBrush, wallBrush;
	};
	inline constexpr std::size_t MaximumPrefabBytes = 2 * 1024 * 1024;
	struct Blueprint {
		std::string id;
		int weight = 1;
		int maxLevels = 4;
		bool allowRotation = true;
		std::vector<std::string> rows;
		std::vector<std::string> upperRows;
		std::vector<std::string> biomes;
		// Optional one-based settlement ordinals, in anchor/generated town order.
		std::vector<int> settlements;
		std::vector<BlueprintProp> objects;
		// Exact client brush bindings; empty values inherit the world legend/settings.
		std::string floorBrush, wallBrush;
		// Optional non-accessible roof, one layer above the last occupied floor.
		std::string roofBrush;
		int windowChance = 0;
		std::string role;
		int maxPerSettlement = 0; // Zero means unlimited.
		int minPerSettlement = 0; // Required in each generated settlement of an allowed biome.
		int minimumPriority = 0; // Higher required groups get first access to central lots.
		// Optional explicit rows for levels 1..maxLevels-1; exclusive with upperRows.
		std::vector<std::vector<std::string>> upperFloors;
		// Entries start at the base floor. Empty names inherit template bindings.
		std::vector<BlueprintMaterial> floorMaterials;
		// Fixed-orientation OTBM region. Digest pins the authored bytes at preview time.
		std::string prefabPath;
		int prefabX = 0, prefabY = 0, prefabZ = 7;
		uint64_t prefabFingerprint = 0;
	};

	using BlueprintCatalog = std::vector<Blueprint>;

	bool ValidateBlueprint(const Blueprint& blueprint, std::string& error);
	bool BlueprintAllowedInSettlement(const Blueprint& blueprint, int ordinal, const std::string& biome);
	bool ValidateCatalog(const BlueprintCatalog& catalog, std::string& error);
	Blueprint RotateBlueprint(const Blueprint& blueprint, int quarterTurns);
	const std::vector<std::string>& BlueprintRowsForFloor(const Blueprint& blueprint, int level);
	BlueprintMaterial BlueprintMaterialForFloor(const Blueprint& blueprint, int level);
	std::pair<int, int> FindBlueprintSymbol(const std::vector<std::string>& rows, char symbol);
	const BlueprintCatalog& DefaultBlueprints();
	const std::string& BlueprintRole(const Blueprint& blueprint);
	uint32_t RoleColor(const std::string& role);

	// JSON/file adapter; compiled with the existing json_spirit parser.
	bool ReadPrefabFile(const std::string& filename, std::string& bytes, uint64_t& fingerprint, std::string& error);
	bool LoadBlueprintCatalog(const std::string& filename, BlueprintCatalog& catalog, std::string& error);

}
#endif
