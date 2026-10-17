#ifndef RME_PROCEDURAL_LEGEND_H_
#define RME_PROCEDURAL_LEGEND_H_

#include "json.h"
#include <cstdint>
#include <set>
#include <string>
#include <vector>

class GroundBrush;

struct LegendEntry {
	uint8_t r = 0, g = 0, b = 0;
	std::string brushName;
	GroundBrush* brush = nullptr;
	// -1 = named brush, 0 = skip, >0 = exact ground ID in the loaded client.
	int tileId = -1;
};

struct LegendMapping {
	std::vector<LegendEntry> entries;
	std::string defaultBrushName;
	GroundBrush* defaultBrush = nullptr;
	bool exactIds = false;
};

namespace ProceduralLegend {
inline bool ParseColor(const std::string& text, LegendEntry& entry) {
	if (text.size() != 7 || text[0] != '#') return false;
	uint32_t color = 0;
	for (size_t i = 1; i < text.size(); ++i) {
		const char c = text[i];
		const int digit = c >= '0' && c <= '9' ? c - '0' :
			(c >= 'a' && c <= 'f' ? c - 'a' + 10 : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1));
		if (digit < 0) return false;
		color = (color << 4) | digit;
	}
	entry.r = static_cast<uint8_t>(color >> 16);
	entry.g = static_cast<uint8_t>(color >> 8);
	entry.b = static_cast<uint8_t>(color);
	return true;
}

inline bool Parse(const json::mValue& root, LegendMapping& mapping, std::string& error) {
	mapping = LegendMapping();
	error.clear();
	if (root.type() != json::obj_type) {
		error = "Legend must be a JSON object.";
		return false;
	}
	const auto& object = root.get_obj();
	auto entries = object.find("entries");
	if (entries == object.end()) {
		entries = object.find("colors");
		mapping.exactIds = true;
	}
	if (entries == object.end() || entries->second.type() != json::array_type || entries->second.get_array().empty()) {
		error = "Legend must contain a non-empty 'entries' or PNGtoOTBM 'colors' array.";
		return false;
	}
	const auto defaultIt = object.find("default");
	if (defaultIt != object.end() && defaultIt->second.type() != json::str_type) {
		error = "Legend default must be a brush name.";
		return false;
	}
	mapping.defaultBrushName = mapping.exactIds ? "" : (defaultIt != object.end() ? defaultIt->second.get_str() : "grass");
	std::set<uint32_t> seen;
	for (const auto& value : entries->second.get_array()) {
		if (value.type() != json::obj_type) {
			error = "Every legend entry must be an object.";
			return false;
		}
		const auto& objectEntry = value.get_obj();
		const auto color = objectEntry.find(mapping.exactIds ? "hex" : "color");
		LegendEntry entry;
		if (color == objectEntry.end() || color->second.type() != json::str_type || !ParseColor(color->second.get_str(), entry)) {
			error = "Every legend color must use #RRGGBB.";
			return false;
		}
		const uint32_t rgb = (uint32_t(entry.r) << 16) | (uint32_t(entry.g) << 8) | entry.b;
		if (!seen.insert(rgb).second) {
			error = "Duplicate legend color: " + color->second.get_str();
			return false;
		}
		const auto id = objectEntry.find("tileId");
		const auto brush = objectEntry.find("brush");
		if (id != objectEntry.end()) {
			if (id->second.type() != json::int_type || id->second.get_int64() < 0 || id->second.get_int64() > 65535 || brush != objectEntry.end()) {
				error = "tileId must be an integer in 0..65535 and cannot be combined with brush.";
				return false;
			}
			entry.tileId = static_cast<int>(id->second.get_int64());
		} else if (!mapping.exactIds && brush != objectEntry.end() && brush->second.type() == json::str_type) {
			entry.brushName = brush->second.get_str();
		} else {
			error = "Every legend entry needs a brush or tileId; converter colors require tileId.";
			return false;
		}
		mapping.entries.push_back(entry);
	}
	return true;
}

inline const LegendEntry* Find(const LegendMapping* mapping, uint8_t r, uint8_t g, uint8_t b) {
	if (mapping) {
		for (const auto& entry : mapping->entries) {
			if (entry.r == r && entry.g == g && entry.b == b) return &entry;
		}
	}
	return nullptr;
}
}

#endif
