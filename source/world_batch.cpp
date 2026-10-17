#include "main.h"
#include "world_batch.h"
#include "procedural_recipe.h"
#include "world_export.h"
#include "world_inputs.h"
#include "editor.h"
#include "copybuffer.h"
#include "items.h"
#include "materials.h"
#include "brush.h"
#include "iomap_otbm.h"
#include "gui.h"
#include "settings.h"
#include "json.h"
#include "town.h"
#include <wx/file.h>
#include <wx/imagpng.h>
#include <wx/wfstream.h>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <cstring>
namespace WorldBatch {
	namespace {
		class Codec : public IOMapOTBM {
		public:
			explicit Codec(MapVersion version) :
				IOMapOTBM(version) { }
			bool Encode(Map& map, MemoryNodeFileWriteHandle& file) {
				return saveMap(map, file);
			}
			bool Decode(Map& map, NodeFileReadHandle& file) {
				return loadMap(map, file);
			}
			bool Spawns(Map& map, pugi::xml_document& doc) {
				return saveSpawns(map, doc);
			}
			bool Houses(Map& map, pugi::xml_document& doc) {
				return saveHouses(map, doc);
			}
		};
		struct SignatureSetting {
			int previous = g_settings.getInteger(Config::CHECK_SIGNATURES);
			SignatureSetting() {
				g_settings.setInteger(Config::CHECK_SIGNATURES, 0);
			}
			~SignatureSetting() {
				g_settings.setInteger(Config::CHECK_SIGNATURES, previous);
			}
		};
		bool RegisterTowns(Map& map, const GenerationSpec& spec, const WorldGen::Plan& plan, const wxString& prefix, json::mArray& records, wxString& error) {
			if (prefix.empty()) {
				return true;
			}
			for (size_t i = 0; i < plan.settlements.size(); ++i) {
				const auto center = plan.settlements[i].center;
				Position position;
				bool found = false;
				for (int radius = 0; radius <= 2 && !found; ++radius) {
					for (int dy = -radius; dy <= radius && !found; ++dy) {
						for (int dx = -radius; dx <= radius && !found; ++dx) {
							if (std::abs(dx) + std::abs(dy) != radius) {
								continue;
							}
							const WorldGen::Point point { center.x + dx, center.y + dy };
							if (!plan.Contains(point) || (plan.FindLayer(0)->cells[plan.Index(point)].flags & WorldGen::Connection)) {
								continue;
							}
							const Position candidate(spec.region.originX + point.x, spec.region.originY + point.y, spec.region.z);
							Tile* tile = map.getTile(candidate);
							if (!tile || !tile->ground) {
								continue;
							}
							auto unsafe = [](const Item* item) { const auto& type = g_items[item->getID()]; return type.unpassable || type.isTeleport() || type.isFloorChange(); };
							if (unsafe(tile->ground) || std::any_of(tile->items.begin(), tile->items.end(), unsafe)) {
								continue;
							}
							position = candidate;
							found = true;
						}
					}
				}
				if (!found) {
					error = "No safe plaza position is available for the generated town record.";
					return false;
				}
				auto town = std::make_unique<Town>(map.towns.getEmptyID());
				const std::string name = nstr(prefix) + " " + std::to_string(i + 1);
				town->setName(name);
				town->setTemplePosition(position);
				json::mObject record;
				record["id"] = int(town->getID());
				record["name"] = name;
				json::mArray target;
				target.push_back(position.x);
				target.push_back(position.y);
				target.push_back(position.z);
				record["templePosition"] = target;
				if (!map.towns.addTown(town.get())) {
					error = "Could not register a generated town ID.";
					return false;
				}
				town.release();
				map.getTile(position)->getLocation()->increaseTownCount();
				records.push_back(record);
			}
			return true;
		}
		bool Write(const wxString& filename, const void* bytes, size_t size, wxString& error) {
			wxTempFile file(filename);
			if (!file.IsOpened() || !file.Write(bytes, size) || !file.Commit()) {
				error = "Could not write batch artifact: " + filename;
				return false;
			}
			return true;
		}
	}
	bool Generate(const Options& options, Result& result, wxString& error, const WorldGen::ProgressCallback& progress) {
		error.clear();
		if (g_gui.IsVersionLoaded() || g_gui.GetCurrentEditor()) {
			error = "World batch generation requires an isolated process without an open editor/client.";
			return false;
		}
		if (options.outputDirectory.empty() || options.outputDirectory.find(wxUniChar(0)) != wxString::npos) {
			error = "Choose a new batch output directory.";
			return false;
		}
		const auto outputPath = std::filesystem::u8path(nstr(options.outputDirectory));
		std::error_code filesystemError;
		if (std::filesystem::exists(outputPath, filesystemError) || filesystemError) {
			error = "Batch output directory already exists or cannot be inspected. Choose a new directory.";
			return false;
		}
		const auto parent = outputPath.parent_path().empty() ? std::filesystem::path(".") : outputPath.parent_path();
		if (!std::filesystem::is_directory(parent, filesystemError) || filesystemError) {
			error = "The batch output parent directory must already exist.";
			return false;
		}
		const auto prefix = options.townPrefix.utf8_string();
		wxString visiblePrefix = options.townPrefix;
		visiblePrefix.Trim(true).Trim(false);
		if (options.townPrefix.find(wxUniChar(0)) != wxString::npos || prefix.size() > 64 || (!options.townPrefix.empty() && visiblePrefix.empty()) || std::any_of(prefix.begin(), prefix.end(), [](unsigned char c) { return c < 32 || c == 127; })) {
			error = "Town prefix must contain 1..64 printable UTF-8 bytes, or be empty to omit town records.";
			return false;
		}
		if (!options.tileBudget || options.tileBudget > WorldEditor::MaximumTileBudget) {
			error = wxString::Format("World tile budget must be between 1 and %llu for this build.", static_cast<unsigned long long>(WorldEditor::MaximumTileBudget));
			return false;
		}
		GenerationSpec spec;
		if (!ProceduralRecipe::Load(options.recipe, spec, error)) {
			return false;
		}
		if (options.seed) {
			spec.seed = *options.seed;
		}
		if (spec.source != GenerationSource::WorldPlan || spec.useSelection) {
			error = "Batch recipes must use source world and an explicit region, without editor selection.";
			return false;
		}
		const int64_t width = int64_t(spec.region.originX) + spec.region.width, height = int64_t(spec.region.originY) + spec.region.height;
		if (width > MAP_MAX_WIDTH || height > MAP_MAX_HEIGHT) {
			error = "Batch world extends beyond the map coordinate limits.";
			return false;
		}
		if (progress && !progress("Loading client item registry", 0)) {
			error = "World generation cancelled.";
			return false;
		}
		SignatureSetting signatures;
		g_materials.clear();
		g_brushes.clear();
		g_items.clear();
		g_gui.gfx.clear();
		g_gui.gfx.client_version = nullptr;
		wxString dataDirectory = options.dataDirectory;
		if (dataDirectory.empty()) {
			wxFileName parent = wxFileName::DirName(wxFileName(options.materials).GetPath());
			if (parent.GetDirCount() > 0) {
				parent.RemoveLastDir();
				const wxString root = parent.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
				if (wxFileExists(root + "procedural/default_legend.json")) {
					dataDirectory = root;
				}
			}
		}
		if (!dataDirectory.empty()) {
			g_settings.setString(Config::DATA_DIRECTORY, nstr(dataDirectory) + "/");
		}
		wxArrayString warnings;
		if (!g_items.loadFromOtb(options.itemsOtb, error, warnings)) {
			return false;
		}
		if (g_items.MajorVersion < 1 || g_items.MajorVersion > 3 || g_items.MinorVersion > 65535) {
			error = "Unsupported client item registry version.";
			return false;
		}
		if (!options.clientDat.empty()) {
			const wxString clients = GUI::GetDataDirectory() + "clients.xml";
			if (!wxFileExists(clients)) {
				error = "Client metadata requires a data directory containing clients.xml.";
				return false;
			}
			ClientVersion::loadVersions(clients);
			auto* version = ClientVersion::get(int(g_items.MinorVersion));
			if (!version) {
				error = "items.otb client version is not registered in clients.xml.";
				return false;
			}
			FileReadHandle header(nstr(options.clientDat));
			uint32_t signature = 0;
			if (!header.isOk() || !header.getU32(signature) || version->getDatFormatForSignature(signature) == DAT_FORMAT_UNKNOWN) {
				error = "Client DAT signature is not registered for this OTB version.";
				return false;
			}
			g_gui.gfx.client_version = version;
			if (!g_gui.gfx.loadSpriteMetadata(options.clientDat, error, warnings)) {
				return false;
			}
			for (int id = 1; id <= g_items.getMaxID(); ++id) {
				if (g_items.typeExists(id)) {
					g_items[id].sprite = static_cast<GameSprite*>(g_gui.gfx.getSprite(g_items[id].clientID));
				}
			}
		}
		if (!g_items.loadFromGameXml(options.itemsXml, error, warnings, int(g_items.MinorVersion))) {
			return false;
		}
		g_brushes.init();
		if (!g_materials.loadMaterials(options.materials, error, warnings)) {
			return false;
		}
		MapVersion version(MAP_OTBM_2, int(g_items.MinorVersion));
		CopyBuffer copy;
		Editor editor(copy, version);
		editor.map.setWidth(int(width));
		editor.map.setHeight(int(height));
		GenerationSpec resolved;
		WorldGen::Plan plan;
		auto planning = [&](const char* stage, int percent) { return !progress || progress(stage, percent * 40 / 100); };
		if (!WorldEditor::BuildPlan(editor, spec, resolved, plan, error, planning)) {
			return false;
		}
		Result prepared;
		auto applying = [&](const char* stage, int percent) { return !progress || progress(stage, 40 + percent * 45 / 100); };
		if (!WorldEditor::ApplyPlan(editor, resolved, plan, error, &prepared.application, applying, options.tileBudget)) {
			return false;
		}
		if (!options.clientDat.empty()) {
			if (progress && !progress("Checking client sprite coverage", 85)) {
				error = "World generation cancelled.";
				return false;
			}
			for (auto it = editor.map.begin(); it != editor.map.end(); ++it) {
				const Tile* tile = (*it)->get();
				if (!tile) {
					continue;
				}
				auto covered = [&](const Item* item) {
					if (!item || g_items[item->getID()].sprite) {
						return true;
					}
					error = wxString::Format("Client DAT has no sprite metadata for used server ID %u (client ID %u). Supply matching complete metadata or omit --client-dat for physical-only validation.", unsigned(item->getID()), unsigned(g_items[item->getID()].clientID));
					return false;
				};
				if (!covered(tile->ground) || !std::all_of(tile->items.begin(), tile->items.end(), covered)) {
					return false;
				}
			}
		}
		prepared.fingerprint = WorldGen::Fingerprint(plan);
		json::mArray townRecords;
		if (!RegisterTowns(editor.map, resolved, plan, options.townPrefix, townRecords, error)) {
			return false;
		}
		prepared.towns = townRecords.size();
		for (const auto& warning : warnings) {
			prepared.warnings.push_back(warning);
		}
		for (const auto& warning : plan.warnings) {
			prepared.warnings.push_back(wxString::FromUTF8(warning));
		}
		prepared.warnings.insert(prepared.warnings.end(), prepared.application.warnings.begin(), prepared.application.warnings.end());
		if (options.clientDat.empty()) {
			prepared.warnings.push_back("No client DAT supplied: physical blocking and composite coordinates were checked; sprite extents were unavailable.");
		}
		editor.map.setSpawnFilename("world-spawns.xml");
		editor.map.setHouseFilename("world-houses.xml");
		editor.map.setMapDescription("Generated world; algorithm " + std::to_string(WorldGen::AlgorithmVersion) + "; plan " + std::to_string(prepared.fingerprint));
		if (progress && !progress("Encoding OTBM and XML sidecars", 85)) {
			error = "World generation cancelled.";
			return false;
		}
		Codec codec(version);
		MemoryNodeFileWriteHandle memory;
		pugi::xml_document spawns, houses;
		if (!codec.Encode(editor.map, memory) || !codec.Spawns(editor.map, spawns) || !codec.Houses(editor.map, houses)) {
			error = "Could not encode the generated OTBM bundle.";
			return false;
		}
		if (progress && !progress("Writing batch artifacts", 95)) {
			error = "World generation cancelled.";
			return false;
		}
		if (!std::filesystem::create_directory(outputPath, filesystemError)) {
			error = "Could not create a new output directory. Existing output is never replaced.";
			return false;
		}
		const wxString output = wxFileName::DirName(options.outputDirectory).GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
		std::ostringstream spawnXml, houseXml;
		spawns.save(spawnXml);
		houses.save(houseXml);
		const std::string spawnContents = spawnXml.str(), houseContents = houseXml.str();
		if (!Write(output + "world-spawns.xml", spawnContents.data(), spawnContents.size(), error) || !Write(output + "world-houses.xml", houseContents.data(), houseContents.size(), error)) {
			return false;
		}
		wxTempFile map(output + "world.otbm");
		if (!map.IsOpened() || !map.Write("OTBM", 4) || !map.Write(memory.getMemory(), memory.getSize()) || !map.Commit()) {
			error = "Could not finish the OTBM artifact. The new output folder contains partial diagnostics.";
			return false;
		}
		if (options.verifyOutput) {
			std::ifstream saved(std::filesystem::u8path(nstr((output + "world.otbm"))), std::ios::binary | std::ios::ate);
			const size_t expected = memory.getSize() + 4;
			if (!saved || saved.tellg() != std::streamoff(expected)) {
				error = "Written OTBM size differs from the encoded map.";
				return false;
			}
			std::string bytes(expected, '\0');
			saved.seekg(0, std::ios::beg);
			if (!saved.read(bytes.data(), std::streamsize(bytes.size())) || bytes.compare(0, 4, "OTBM") != 0 || std::memcmp(bytes.data() + 4, memory.getMemory(), memory.getSize()) != 0) {
				error = "Written OTBM bytes differ from the generated map.";
				return false;
			}
			saved.close();
			Editor reopened(copy, version);
			MemoryNodeFileReadHandle nodes(reinterpret_cast<const uint8_t*>(bytes.data() + 4), bytes.size() - 4);
			MemoryNodeFileWriteHandle replay;
			if (!codec.Decode(reopened.map, nodes) || !codec.Encode(reopened.map, replay)) {
				error = "OTBM verification failed: the production reader/writer could not reopen the generated world.";
				return false;
			}
			if (replay.getSize() != memory.getSize() || std::memcmp(replay.getMemory(), memory.getMemory(), memory.getSize()) != 0) {
				size_t difference = 0;
				while (difference < std::min(replay.getSize(), memory.getSize()) && replay.getMemory()[difference] == memory.getMemory()[difference]) {
					++difference;
				}
				std::string diagnostic("OTBM", 4);
				diagnostic.append(reinterpret_cast<const char*>(replay.getMemory()), replay.getSize());
				wxString writeError;
				const bool diagnosticSaved = Write(output + "verification-readback.otbm", diagnostic.data(), diagnostic.size(), writeError);
				error = wxString::Format("OTBM verification mismatch: original %llu bytes, readback %llu bytes, first differing offset %llu.", static_cast<unsigned long long>(memory.getSize()), static_cast<unsigned long long>(replay.getSize()), static_cast<unsigned long long>(difference + 4));
				if (!diagnosticSaved) {
					error += " " + writeError;
				}
				return false;
			}

			for (const auto& sidecar : { std::pair { "world-spawns.xml", "spawns" }, std::pair { "world-houses.xml", "houses" } }) {
				pugi::xml_document document;
				if (!document.load_file((output + wxString::FromUTF8(sidecar.first)).wc_str()) || !document.child(sidecar.second)) {
					error = "Could not verify generated XML sidecar: " + wxString::FromUTF8(sidecar.first);
					return false;
				}
			}
			prepared.verified = true;
		}
		std::string diagnosticError;
		if (!WorldGen::ExportDiagnostics(plan, nstr((output + "plan")), diagnosticError)) {
			error = wxString::FromUTF8(diagnosticError);
			return false;
		}
		json::mArray previews;
		if (options.exportPreviews) {
			const char* names[] = { "terrain", "elevation", "moisture", "temperature", "roles" };
			wxPNGHandler encoder;
			for (const auto& layer : plan.layers) {
				for (int view = 0; view < 5; ++view) {
					const wxString filename = wxString::Format("preview_z%d_%s.png", resolved.region.z + layer.offsetZ, wxString::FromUTF8(names[view]));
					wxImage image = WorldEditor::Render(plan, layer.offsetZ, static_cast<WorldEditor::PreviewLayer>(view));
					wxTempFileOutputStream stream(output + filename);
					if (!stream.IsOk() || !encoder.SaveFile(&image, stream, false) || !stream.Commit()) {
						error = "Could not finish batch preview: " + filename;
						return false;
					}
					previews.push_back(nstr(filename));
				}
			}
		}
		json::mObject editableInputs;
		if (options.exportEditableInputs) {
			GenerationSpec editable = resolved;
			editable.world.settings = plan.settings;
			if (!WorldGen::ExportGeographyInputs(plan, output + "inputs", editable.world.heightmapPath, editable.world.biomeHintsPath, error) || !ProceduralRecipe::Save(output + "editable-recipe.json", editable, error, ProceduralRecipe::PathMode::RelativeToRecipe)) {
				return false;
			}
			editableInputs["heightmap"] = "inputs_heightmap.png";
			editableInputs["biomeHints"] = "inputs_biomes.png";
			editableInputs["recipe"] = "editable-recipe.json";
		}
		if (!ProceduralRecipe::Save(output + "recipe.json", resolved, error, ProceduralRecipe::PathMode::RelativeToRecipe)) {
			return false;
		}
		json::mObject report, client;
		report["schemaVersion"] = 1;
		report["tileBudget"] = json::mValue(uint64_t(options.tileBudget));
		report["verifiedOTBM"] = prepared.verified;
		report["previews"] = previews;
		report["editableInputs"] = editableInputs;
		report["towns"] = townRecords;
		report["townPrefix"] = nstr(options.townPrefix);
		report["algorithmVersion"] = int(WorldGen::AlgorithmVersion);
		report["seed"] = json::mValue(uint64_t(resolved.seed));
		report["planFingerprint"] = std::to_string(prepared.fingerprint);
		report["tiles"] = json::mValue(uint64_t(prepared.application.changedTiles));
		report["doodads"] = json::mValue(uint64_t(prepared.application.doodads));
		report["furniture"] = json::mValue(uint64_t(prepared.application.furniture));
		report["skippedDoodads"] = json::mValue(uint64_t(prepared.application.skippedDoodads));
		report["skippedFurniture"] = json::mValue(uint64_t(prepared.application.skippedFurniture));
		client["otbFormat"] = int(g_items.MajorVersion);
		client["clientVersion"] = int(g_items.MinorVersion);
		client["build"] = int(g_items.BuildNumber);
		client["spriteMetadataLoaded"] = !options.clientDat.empty();
		report["client"] = client;
		json::mArray messages;
		for (const auto& warning : prepared.warnings) {
			messages.push_back(nstr(warning));
		}
		report["warnings"] = messages;
		std::ostringstream reportJson;
		json::write(json::mValue(report), reportJson);
		const auto reportContents = reportJson.str();
		if (!Write(output + "generation.json", reportContents.data(), reportContents.size(), error)) {
			return false;
		}
		result = std::move(prepared);
		// Completion notification follows the final commit; it cannot cancel saved output.
		if (progress) {
			progress("World bundle saved", 100);
		}
		return true;
	}
	int CommandLine(const wxArrayString& args) {
		const char* usage = "Usage: rme --generate-world recipe.json items.otb items.xml materials.xml NEW_OUTPUT_DIRECTORY [--data-dir DIR] [--client-dat FILE] [--no-previews] [--editable-inputs] [--town-prefix NAME] [--tile-budget MAX_TILES] [--seed UINT32] [--verify]";
		if (args.size() == 1 && args[0] == "--help") {
			std::cout << usage << '\n';
			return 0;
		}
		if (args.size() < 5) {
			std::cerr << usage << '\n';
			return 2;
		}
		Options options;
		options.recipe = args[0];
		options.itemsOtb = args[1];
		options.itemsXml = args[2];
		options.materials = args[3];
		options.outputDirectory = args[4];
		for (size_t i = 5; i < args.size(); ++i) {
			if (args[i] == "--editable-inputs") {
				options.exportEditableInputs = true;
				continue;
			}
			if (args[i] == "--verify") {
				options.verifyOutput = true;
				continue;
			}
			if (args[i] == "--no-previews") {
				options.exportPreviews = false;
				continue;
			}
			if (i + 1 >= args.size()) {
				std::cerr << usage << '\n';
				return 2;
			}
			if (args[i] == "--data-dir") {
				options.dataDirectory = args[i + 1];
			} else if (args[i] == "--client-dat") {
				options.clientDat = args[i + 1];
			} else if (args[i] == "--town-prefix") {
				options.townPrefix = args[i + 1];
			} else if (args[i] == "--seed") {
				const auto& value = args[i + 1];
				unsigned long long seed = 0;
				if (options.seed || value.empty() || value.find_first_not_of("0123456789") != wxString::npos || !value.ToULongLong(&seed) || seed > UINT32_MAX) {
					std::cerr << "Seed must be one decimal unsigned 32-bit integer (0..4294967295). Specify --seed only once.\n";
					return 2;
				}
				options.seed = uint32_t(seed);
			} else if (args[i] == "--tile-budget") {
				unsigned long long budget = 0;
				if (!args[i + 1].ToULongLong(&budget) || !budget || budget > WorldEditor::MaximumTileBudget) {
					std::cerr << "Tile budget must be between 1 and " << WorldEditor::MaximumTileBudget << ".\n";
					return 2;
				}
				options.tileBudget = size_t(budget);
			} else {
				std::cerr << usage << '\n';
				return 2;
			}
			++i;
		}
		Result result;
		wxString error;
		std::string last;
		auto progress = [&](const char* stage, int) {if(last!=stage){std::cout<<stage<<'\n';last=stage;}return true; };
		try {
			if (!Generate(options, result, error, progress)) {
				std::cerr << "World generation failed: " << nstr(error) << '\n';
				return 1;
			}
		} catch (const std::exception& exception) {
			std::cerr << "World generation failed: " << exception.what() << '\n';
			return 1;
		}
		std::cout << "World saved: " << result.application.changedTiles << " tiles; " << result.towns << " towns; plan=" << result.fingerprint << '\n';
		for (const auto& warning : result.warnings) {
			std::cerr << "Warning: " << nstr(warning) << '\n';
		}
		return 0;
	}
}
