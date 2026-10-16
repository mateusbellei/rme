#include "world_catalog.h"

#include <iostream>
#include <stdexcept>

static void Check(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

int main() {
	try {
		std::string error;
		const auto& catalog = WorldGen::DefaultBlueprints();
		Check(WorldGen::ValidateCatalog(catalog, error), error.c_str());
		for (const auto& blueprint : catalog) {
			for (int rotation = 0; rotation < 4; ++rotation) {
				const auto rotated = WorldGen::RotateBlueprint(blueprint, rotation);
				Check(WorldGen::ValidateBlueprint(rotated, error), "Rotated blueprint must remain valid");
				const auto back = WorldGen::RotateBlueprint(rotated, 4 - rotation);
				Check(back.rows == blueprint.rows && back.upperRows == blueprint.upperRows, "Four rotations must restore every floor exactly");
			}
		}
		auto styled = catalog[1];
		styled.floorBrush = "floor";
		styled.wallBrush = "wall";
		Check(WorldGen::ValidateBlueprint(styled, error), "Optional exact client materials must validate");
		Check(WorldGen::RotateBlueprint(styled, 1).floorBrush == "floor", "Rotation must preserve material bindings");
		styled.floorBrush = std::string("bad\0floor", 9);
		Check(!WorldGen::ValidateBlueprint(styled, error), "NUL material names must fail before client lookup");
		auto furnished = catalog[1];
		furnished.objects.push_back({ 3, 3, 0, { "north", "east", "south", "west" }, true });
		Check(WorldGen::ValidateBlueprint(furnished, error), "Furniture anchors must validate on interior floor");
		for (int rotation = 0; rotation < 4; ++rotation) {
			const auto turned = WorldGen::RotateBlueprint(furnished, rotation);
			Check(WorldGen::ValidateBlueprint(turned, error), "Furniture must rotate with its footprint");
			Check(turned.objects[0].brushes.front() == furnished.objects[0].brushes[rotation], "Furniture orientation must follow the building rotation");
			const auto back = WorldGen::RotateBlueprint(turned, 4 - rotation);
			Check(back.objects[0].x == 3 && back.objects[0].y == 3 && back.objects[0].brushes == furnished.objects[0].brushes, "Furniture position and all orientation names must round-trip");
		}
		auto invalidFurniture = furnished;
		invalidFurniture.objects[0].x = 1;
		invalidFurniture.objects[0].y = 1;
		Check(!WorldGen::ValidateBlueprint(invalidFurniture, error), "Furniture cannot anchor on a landing");
		invalidFurniture = furnished;
		invalidFurniture.objects[0].floor = 4;
		Check(!WorldGen::ValidateBlueprint(invalidFurniture, error), "Furniture cannot occupy a nonexistent floor");
		invalidFurniture = furnished;
		invalidFurniture.objects[0].brushes = { "one", "two" };
		Check(!WorldGen::ValidateBlueprint(invalidFurniture, error), "Furniture needs one or exactly four orientation brushes");
		invalidFurniture = furnished;
		invalidFurniture.objects.push_back(invalidFurniture.objects[0]);
		Check(!WorldGen::ValidateBlueprint(invalidFurniture, error), "Stacked anchors must be authored as one composite brush");
		auto invalid = catalog[0];
		invalid.id = "../unsafe";
		Check(!WorldGen::ValidateBlueprint(invalid, error), "Invalid template IDs must fail");
		invalid = catalog[0];
		invalid.rows[2] = "#";
		Check(!WorldGen::ValidateBlueprint(invalid, error), "Ragged rows must fail safely");
		invalid = catalog[0];
		invalid.rows[2] = "#???#";
		Check(!WorldGen::ValidateBlueprint(invalid, error), "Unknown symbols must fail");
		invalid = catalog[0];
		invalid.rows[3] = "#####";
		Check(!WorldGen::ValidateBlueprint(invalid, error), "Disconnected rooms/entrances must fail");
		invalid = catalog[0];
		invalid.rows[1] = "#...#";
		Check(!WorldGen::ValidateBlueprint(invalid, error), "Multi-floor template needs its landing anchors");
		invalid = catalog[0];
		invalid.upperRows = invalid.rows;
		Check(!WorldGen::ValidateBlueprint(invalid, error), "Upper outer door must be closed");
		invalid = catalog[0];
		invalid.maxLevels = 0;
		Check(!WorldGen::ValidateBlueprint(invalid, error), "Invalid floor count must fail");
		auto duplicate = catalog;
		duplicate.push_back(catalog[0]);
		Check(!WorldGen::ValidateCatalog(duplicate, error), "Duplicate template IDs must fail");
		Check(!WorldGen::ValidateCatalog({}, error), "Empty catalog must fail");
		std::cout << "PASS: reusable blueprints, rotations, room connectivity, walls, entrances and vertical landing anchors\n";
		return 0;
	} catch (const std::exception& e) {
		std::cerr << "FAIL: " << e.what() << '\n';
		return 1;
	}
}
