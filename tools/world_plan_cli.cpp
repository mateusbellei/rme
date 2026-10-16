#include "world_plan.h"
#include "world_export.h"
#include <charconv>

#include <chrono>
#include <fstream>
#include <iostream>
#include <string>

namespace {
	template <class T>
	T Number(const char* text) {
		const std::string value = text;
		T number {};
		const auto result = std::from_chars(value.data(), value.data() + value.size(), number);
		if (result.ec != std::errc() || result.ptr != value.data() + value.size()) {
			throw std::runtime_error("Arguments must be complete decimal integers in range");
		}
		return number;
	}
}

// Client-independent diagnostic: rme_world_plan [output prefix] [seed] [size].
// Outputs PPM masks per floor and a JSON summary, not a playable OTBM.
int main(int argc, char** argv) {
	try {
		const std::string prefix = argc > 1 ? argv[1] : "world";
		WorldGen::Settings settings;
		if (argc > 8) {
			throw std::runtime_error("Usage: rme_world_plan [prefix] [uint32 seed] [size] [organic|blocks] [buildings] [smooth|ridges] [biome detail 0-100]");
		}
		if (argc > 2) {
			settings.seed = Number<uint32_t>(argv[2]);
		}
		settings.width = settings.height = argc > 3 ? Number<int>(argv[3]) : 256;
		if (argc > 4) {
			const std::string layout = argv[4];
			if (layout != "organic" && layout != "blocks") {
				throw std::runtime_error("Layout must be organic or blocks");
			}
			settings.settlementLayout = layout == "blocks" ? WorldGen::SettlementLayout::Blocks : WorldGen::SettlementLayout::Organic;
		}
		if (argc > 5) {
			settings.buildingsPerSettlement = Number<int>(argv[5]);
		}
		if (argc > 6) {
			const std::string relief = argv[6];
			if (relief != "smooth" && relief != "ridges") {
				throw std::runtime_error("Relief must be smooth or ridges");
			}
			settings.relief = relief == "ridges" ? WorldGen::ReliefStyle::Ridges : WorldGen::ReliefStyle::Smooth;
		}
		if (argc > 7) {
			settings.biomeDetail = Number<int>(argv[7]);
		}
		settings.caveDepth = 2;
		settings.buildingLevels = 2;
		WorldGen::Plan plan;
		std::string error;
		const auto start = std::chrono::steady_clock::now();
		if (!WorldGen::Build(settings, plan, error)) {
			std::cerr << error << '\n';
			return 1;
		}
		const auto report = WorldGen::Validate(plan);
		if (!WorldGen::ExportDiagnostics(plan, prefix, error)) {
			std::cerr << error << '\n';
			return 1;
		}
		const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
		std::cout << "seed=" << settings.seed << " size=" << settings.width << " settlements=" << plan.settlements.size() << " buildings=" << plan.buildings.size() << " links=" << plan.links.size() << " valid=" << report.valid << " ms=" << milliseconds << '\n';
		for (const auto& warning : plan.warnings) {
			std::cout << "Warning: " << warning << '\n';
		}
		for (const auto& e : report.errors) {
			std::cerr << e << '\n';
		}
		return report.valid ? 0 : 1;
	} catch (const std::exception& e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
