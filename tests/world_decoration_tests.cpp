#include "world_decoration.h"

#include <iostream>
#include <stdexcept>

static void Check(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

int main() {
	try {
		WorldGen::DecorationGrid grid;
		grid.width = grid.height = 32;
		grid.walkable.assign(1024, 1);
		grid.reserved.assign(1024, 0);
		grid.occupied.assign(1024, 0);
		grid.habitat.assign(1024, 1);
		std::string reason;
		std::vector<WorldGen::DecorationCell> single = { { { 0, 0 }, true } };
		Check(WorldGen::CanPlaceDecoration(grid, { 10, 10 }, single, &reason), "A single tree with clear surrounding floor must fit");
		Check(!WorldGen::CanPlaceDecoration(grid, { 0, 0 }, single, &reason), "Footprint clearance must stay within the region");
		grid.reserved[10 * 32 + 11] = 1;
		Check(!WorldGen::CanPlaceDecoration(grid, { 10, 10 }, single), "Road/entrance clearance must be protected");
		grid.reserved[10 * 32 + 11] = 0;
		grid.walkable[10 * 32 + 11] = 0;
		Check(!WorldGen::CanPlaceDecoration(grid, { 10, 10 }, single), "Decoration cannot choke a narrow cave corridor");
		grid.walkable[10 * 32 + 11] = 1;
		grid.habitat[10 * 32 + 11] = 2;
		Check(!WorldGen::CanPlaceDecoration(grid, { 10, 10 }, single), "Composite clearance cannot spill into an incompatible biome");
		grid.habitat[10 * 32 + 11] = 1;
		WorldGen::MarkDecoration(grid, { 10, 10 }, single);
		Check(!grid.walkable[10 * 32 + 10] && grid.occupied[10 * 32 + 10], "Accepted blocking footprint must update occupancy");
		Check(!WorldGen::CanPlaceDecoration(grid, { 11, 10 }, single), "Subsequent objects must respect existing props");
		std::vector<WorldGen::DecorationCell> composite;
		for (int y = 0; y < 3; ++y) {
			for (int x = 0; x < 3; ++x) {
				if (x == 0 || y == 0 || x == 2 || y == 2) {
					composite.push_back({ { x, y }, true });
				}
			}
		}
		Check(!WorldGen::CanPlaceDecoration(grid, { 20, 20 }, composite, &reason), "A blocking ring cannot trap a walkable pocket");
		composite.push_back({ { 1, 1 }, true });
		Check(WorldGen::CanPlaceDecoration(grid, { 20, 20 }, composite), "A solid multi-tile rock with an open perimeter must fit");
		Check(!WorldGen::CanPlaceDecoration(grid, { 12, 12 }, { { { 32767, 0 }, true } }), "Oversized composite offsets must fail before indexing");
		grid.occupied.clear();
		Check(!WorldGen::CanPlaceDecoration(grid, { 10, 10 }, single), "Malformed occupancy data must fail safely");
		std::cout << "PASS: decoration bounds, multi-tile occupancy, biome clearance, route protection and local connectivity\n";
		return 0;
	} catch (const std::exception& e) {
		std::cerr << "FAIL: " << e.what() << '\n';
		return 1;
	}
}
