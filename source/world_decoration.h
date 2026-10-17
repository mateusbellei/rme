#ifndef RME_WORLD_DECORATION_H_
#define RME_WORLD_DECORATION_H_

#include "world_plan.h"
#include <cstdint>
#include <string>
#include <vector>

namespace WorldGen {

	struct DecorationCell {
		Point offset;
		bool blocking = false;
	};

	struct DecorationGrid {
		int width = 0, height = 0;
		std::vector<uint8_t> walkable, reserved, occupied, habitat;
	};

	// The expanded rectangle must be free, in one compatible habitat, and all
	// remaining walkable cells must still reach its perimeter after placement.
	// Thus a decoration cannot sever a corridor or create an isolated floor pocket.
	bool CanPlaceDecoration(const DecorationGrid& grid, Point anchor, const std::vector<DecorationCell>& footprint, std::string* reason = nullptr);
	void MarkDecoration(DecorationGrid& grid, Point anchor, const std::vector<DecorationCell>& footprint);

}
#endif
