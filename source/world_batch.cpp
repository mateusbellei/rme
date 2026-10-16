#include "main.h"
#include "world_batch.h"
#include "procedural_recipe.h"
#include "world_export.h"
#include "editor.h"
#include "copybuffer.h"
#include "items.h"
#include "materials.h"
#include "brush.h"
#include "iomap_otbm.h"
#include "gui.h"
#include "settings.h"
#include "json.h"
#include <wx/file.h>
#include <wx/imagpng.h>
#include <wx/wfstream.h>
#include <filesystem>
#include <iostream>
namespace WorldBatch {
	namespace {
		class Codec : public IOMapOTBM {
		public:
			explicit Codec(MapVersion version) :
				IOMapOTBM(version) { }
			bool Encode(Map& map, MemoryNodeFileWriteHandle& file) {
				return saveMap(map, file);
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
		GenerationSpec spec;
		if (!ProceduralRecipe::Load(options.recipe, spec, error)) {
			return false;
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
		if (!WorldEditor::ApplyPlan(editor, resolved, plan, error, &prepared.application, applying)) {
			return false;
		}
		prepared.fingerprint = WorldGen::Fingerprint(plan);
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
		if (!ProceduralRecipe::Save(output + "recipe.json", resolved, error, ProceduralRecipe::PathMode::RelativeToRecipe)) {
			return false;
		}
		json::mObject report, client;
		report["schemaVersion"] = 1;
		report["previews"] = previews;
		report["algorithmVersion"] = int(WorldGen::AlgorithmVersion);
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
		const char* usage = "Usage: rme --generate-world recipe.json items.otb items.xml materials.xml NEW_OUTPUT_DIRECTORY [--data-dir DIR] [--client-dat FILE] [--no-previews]";
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
		std::cout << "World saved: " << result.application.changedTiles << " tiles; plan=" << result.fingerprint << '\n';
		for (const auto& warning : result.warnings) {
			std::cerr << "Warning: " << nstr(warning) << '\n';
		}
		return 0;
	}
}
