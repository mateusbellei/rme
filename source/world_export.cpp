#include "world_export.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
namespace WorldGen {
	namespace {
		void String(std::ostream& out, const std::string& value) {
			out << '"';
			for (unsigned char c : value) {
				if (c == '"' || c == '\\') {
					out << '\\' << char(c);
				} else if (c < 32) {
					const auto flags = out.flags();
					const auto fill = out.fill();
					out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c);
					out.flags(flags);
					out.fill(fill);
				} else {
					out << char(c);
				}
			}
			out << '"';
		}
		void PointJSON(std::ostream& out, Point point, int floor) {
			out << '[' << point.x << ',' << point.y << ',' << floor << ']';
		}
	}
	bool ExportDiagnostics(const Plan& plan, const std::string& prefix, std::string& error) {
		error.clear();
		const auto report = Validate(plan);
		if (!report.valid) {
			error = "Cannot export an invalid world plan.";
			return false;
		}
		if (prefix.empty()) {
			error = "Choose a nonempty diagnostics output prefix.";
			return false;
		}
		const auto& s = plan.settings;
		try {
			const auto base = std::filesystem::u8path(prefix);
			if (!base.parent_path().empty()) {
				std::filesystem::create_directories(base.parent_path());
			}
			for (const auto& layer : plan.layers) {
				const std::string filename = prefix + "_z" + std::to_string(layer.offsetZ) + ".ppm";
				std::ofstream out(std::filesystem::u8path(filename), std::ios::binary);
				out << "P6\n"
					<< s.width << ' ' << s.height << "\n255\n";
				for (const auto& cell : layer.cells) {
					uint32_t color = TerrainColor(cell.terrain);
					if (cell.flags & Wall) {
						color = 0x404040;
					}
					if (cell.flags & Entrance) {
						color = 0xff40ff;
					}
					if (cell.flags & Connection) {
						color = 0xffff00;
					}
					const char rgb[] = { char(color >> 16), char(color >> 8), char(color) };
					out.write(rgb, 3);
				}
				out.flush();
				if (!out) {
					error = "Could not finish terrain diagnostic: " + filename;
					return false;
				}
			}
			std::ofstream out(std::filesystem::u8path(prefix + ".json"));
			out << "{\n  \"schemaVersion\": 1,\n  \"algorithmVersion\": " << AlgorithmVersion << ",\n  \"fingerprint\": \"" << Fingerprint(plan) << "\",\n  \"settings\": {\"width\":" << s.width << ",\"height\":" << s.height << ",\"seed\":" << s.seed << ",\"caveSeed\":" << s.caveSeed.value_or(s.seed) << ",\"shape\":\"" << (s.shape == LandShape::Continent ? "continent" : "archipelago") << "\",\"layout\":\"" << (s.settlementLayout == SettlementLayout::Blocks ? "blocks" : "organic") << "\",\"seaLevel\":" << s.seaLevel << ",\"coastRoughness\":" << s.coastRoughness << ",\"relief\":\"" << (s.relief == ReliefStyle::Ridges ? "ridges" : "smooth") << "\",\"biomeDetail\":" << s.biomeDetail << ",\"temperatureBias\":" << s.temperatureBias << ",\"moistureBias\":" << s.moistureBias << ",\"riverWidth\":" << s.riverWidth << ",\"rivers\":" << s.riverCount << ",\"settlements\":" << s.settlementCount << ",\"diverseSettlements\":" << (s.diverseSettlements ? "true" : "false") << ",\"buildings\":" << s.buildingsPerSettlement << ",\"buildingLevels\":" << s.buildingLevels << ",\"caveDepth\":" << s.caveDepth << ",\"caveEntrances\":" << s.caveEntrances << ",\"caveFill\":" << s.caveFill << ",\"roadWidth\":" << s.roadWidth << "},\n";
			out << "  \"counts\": {\"plannedTiles\":" << report.plannedTiles << ",\"landTiles\":" << report.landTiles << ",\"waterTiles\":" << report.waterTiles << ",\"roadTiles\":" << report.roadTiles << ",\"surfaceComponents\":" << report.surfaceComponents << ",\"caveComponents\":" << report.caveComponents << "},\n  \"layers\": [";
			bool first = true;
			for (const auto& layer : plan.layers) {
				if (!first) {
					out << ',';
				}
				first = false;
				const auto tiles = std::count_if(layer.cells.begin(), layer.cells.end(), [](const Cell& cell) { return cell.terrain != Terrain::Void; });
				out << "{\"offsetZ\":" << layer.offsetZ << ",\"cells\":" << layer.cells.size() << ",\"tiles\":" << tiles << '}';
			}
			out << "],\n  \"settlements\": [";
			first = true;
			for (size_t townIndex = 0; townIndex < plan.settlements.size(); ++townIndex) {
				const auto& town = plan.settlements[townIndex];
				if (!first) {
					out << ',';
				}
				first = false;
				out << "{\"center\":";
				PointJSON(out, town.center, 0);
				out << ",\"radius\":" << town.radius << ",\"biome\":";
				String(out, TerrainName(town.biome));
				out << ",\"buildings\":[";
				for (size_t i = 0; i < town.buildings.size(); ++i) {
					if (i) {
						out << ',';
					}
					out << town.buildings[i];
				}
				out << "],\"constructionMix\":[";
				bool firstTemplate = true;
				for (size_t i = 0; i < plan.catalog.size(); ++i) {
					const auto& blueprint = plan.catalog[i];
					if (!BlueprintAllowedInSettlement(blueprint, int(townIndex) + 1, TerrainName(town.biome))) {
						continue;
					}
					if (!firstTemplate) out << ',';
					firstTemplate = false;
					const auto count = std::count_if(plan.buildings.begin(), plan.buildings.end(), [&](const Building& building) { return building.settlement == int(townIndex) && building.blueprint == int(i); });
					out << "{\"blueprint\":";
					String(out, blueprint.id);
					out << ",\"role\":";
					String(out, BlueprintRole(blueprint));
					out << ",\"count\":" << count << ",\"min\":" << blueprint.minPerSettlement << ",\"max\":" << blueprint.maxPerSettlement << '}';
				}
				out << "]}";
			}
			out << "],\n  \"buildings\": [";
			first = true;
			for (const auto& building : plan.buildings) {
				if (!first) {
					out << ',';
				}
				first = false;
				out << "{\"origin\":";
				PointJSON(out, building.origin, 0);
				out << ",\"width\":" << building.width << ",\"height\":" << building.height << ",\"levels\":" << building.levels << ",\"entrance\":";
				PointJSON(out, building.entrance, 0);
				out << ",\"settlement\":" << building.settlement << ",\"rotation\":" << building.rotation << ",\"blueprint\":";
				String(out, plan.catalog[building.blueprint].id);
				out << ",\"role\":";
				String(out, BlueprintRole(plan.catalog[building.blueprint]));
				const auto& blueprint = plan.catalog[building.blueprint];
				if (!blueprint.prefabPath.empty()) out << ",\"prefabFingerprint\":\"" << blueprint.prefabFingerprint << "\"";
				out << '}';
			}
			out << "],\n  \"verticalLinks\": [";
			first = true;
			for (const auto& link : plan.links) {
				if (!first) {
					out << ',';
				}
				first = false;
				out << "{\"upper\":";
				PointJSON(out, link.upper, link.upperOffsetZ);
				out << ",\"lower\":";
				PointJSON(out, link.lower, link.lowerOffsetZ);
				out << ",\"upperArrival\":";
				PointJSON(out, link.upperArrival, link.upperOffsetZ);
				out << ",\"lowerArrival\":";
				PointJSON(out, link.lowerArrival, link.lowerOffsetZ);
				out << '}';
			}
			out << "],\n  \"warnings\": [";
			for (size_t i = 0; i < plan.warnings.size(); ++i) {
				if (i) {
					out << ',';
				}
				String(out, plan.warnings[i]);
			}
			out << "]\n}\n";
			out.flush();
			if (!out) {
				error = "Could not finish world diagnostic manifest.";
				return false;
			}
			return true;
		} catch (const std::filesystem::filesystem_error& exception) {
			error = exception.what();
			return false;
		}
	}
}
