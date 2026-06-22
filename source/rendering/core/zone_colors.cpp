#include "rendering/core/zone_colors.h"

#include <algorithm>

namespace {
	std::vector<ZoneColor> zone_colors;
	bool generated = false;

	void generateColors() {
		int r = 250;
		int g = 100;
		int b = 100;
		const int step = 25;
		bool incrementing = true;

		while (true) {
			if (std::find(zone_colors.begin(), zone_colors.end(), ZoneColor { r, g, b }) == zone_colors.end()) {
				zone_colors.push_back({ r, g, b });
			}

			if (g < 250 && incrementing) {
				g += step;
			} else if (r > 100 && !incrementing && g == 250) {
				r -= step;
			} else if (b < 250 && r == 100) {
				b += step;
			} else if (g > 100 && b == 250) {
				g -= step;
			} else if (r < 250 && g == 100) {
				r += step;
			} else if (b > 100 && r == 250) {
				b -= step;
			} else if (b == 100 && g == 250) {
				incrementing = false;
			}

			if (r == 250 && g == 100 && b == 100 && !incrementing) {
				break;
			}
		}
	}
}

void EnsureZoneColorsGenerated() {
	if (!generated) {
		generateColors();
		generated = true;
	}
}

const std::vector<ZoneColor>& GetZoneColors() {
	EnsureZoneColorsGenerated();
	return zone_colors;
}

void ComputeZoneTint(const std::vector<uint16_t>& zone_ids, uint8_t& r, uint8_t& g, uint8_t& b) {
	EnsureZoneColorsGenerated();
	if (zone_ids.empty() || zone_colors.empty()) {
		return;
	}

	uint16_t r16 = 0;
	uint16_t g16 = 0;
	uint16_t b16 = 0;
	for (const auto zone_id : zone_ids) {
		const uint16_t color_index = zone_id % zone_colors.size();
		const ZoneColor& colour = zone_colors.at(color_index);
		r16 += static_cast<uint16_t>(std::get<0>(colour));
		g16 += static_cast<uint16_t>(std::get<1>(colour));
		b16 += static_cast<uint16_t>(std::get<2>(colour));
	}

	const size_t zones = zone_ids.size();
	r = static_cast<uint8_t>(r16 / zones);
	g = static_cast<uint8_t>(g16 / zones);
	b = static_cast<uint8_t>(b16 / zones);
}
