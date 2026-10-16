#ifndef RME_WORLD_BATCH_H_
#define RME_WORLD_BATCH_H_
#include "world_editor.h"
#include <optional>
namespace WorldBatch {
	struct Options {
		wxString recipe, itemsOtb, itemsXml, materials, outputDirectory;
		wxString dataDirectory, clientDat, townPrefix;
		bool exportPreviews = true;
		bool verifyOutput = false;
		size_t tileBudget = WorldEditor::DefaultTileBudget;
		std::optional<uint32_t> seed;
	};
	struct Result {
		uint64_t fingerprint = 0;
		size_t towns = 0;
		bool verified = false;
		WorldEditor::ApplicationStats application;
		std::vector<wxString> warnings;
	};
	// Isolated process only: loads the specified client registry, never user settings.
	bool Generate(const Options& options, Result& result, wxString& error, const WorldGen::ProgressCallback& progress = {});
	int CommandLine(const wxArrayString& arguments);
}
#endif
