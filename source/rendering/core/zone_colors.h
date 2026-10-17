#ifndef RME_RENDERING_CORE_ZONE_COLORS_H_
#define RME_RENDERING_CORE_ZONE_COLORS_H_

#include <cstdint>
#include <tuple>
#include <vector>

using ZoneColor = std::tuple<int, int, int>;

void EnsureZoneColorsGenerated();
const std::vector<ZoneColor>& GetZoneColors();

void ComputeZoneTint(const std::vector<uint16_t>& zone_ids, uint8_t& r, uint8_t& g, uint8_t& b);

#endif
