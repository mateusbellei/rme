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
