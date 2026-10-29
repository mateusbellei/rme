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

	// Bounded Manhattan distances on the immutable plan, independent of scatter
	// order and selection masks. 33 means no source within the supported radius.
	struct DecorationEnvironment {
		std::vector<uint8_t> water, path, forestEdge;
		std::vector<uint16_t> slope;
	};
	bool BuildDecorationEnvironment(const Layer& layer, int width, int height, DecorationEnvironment& result);
	int TransitionDensity(int distance, int width, int edgeDensity);
	// Paint an interpolated circular stroke, clipped to raster boundaries.
	bool PaintNatureMask(std::vector<uint8_t>& mask, int width, int height, Point from, Point to, int radius, bool replace);

	// The expanded rectangle must be free, in one compatible habitat, and all
	// remaining walkable cells must still reach its perimeter after placement.
	// Thus a decoration cannot sever a corridor or create an isolated floor pocket.
	bool CanPlaceDecoration(const DecorationGrid& grid, Point anchor, const std::vector<DecorationCell>& footprint, std::string* reason = nullptr);
	void MarkDecoration(DecorationGrid& grid, Point anchor, const std::vector<DecorationCell>& footprint);

}
#endif
