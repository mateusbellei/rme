//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_PROCEDURAL_GENERATOR_H_
#define RME_PROCEDURAL_GENERATOR_H_

#include "main.h"
#include "position.h"

class Map;
class Editor;

struct GenerationRegion {
	int originX;
	int originY;
	int z;
	int width;
	int height;

	GenerationRegion() :
		originX(0), originY(0), z(7), width(512), height(512) { }
};

enum class GenerationSource {
	ImageMask,
	TextPrompt,
	PromptWithImage
};

enum class GenerationPreset {
	Auto,
	Forest,
	Desert,
	Cave,
	City,
	Coast,
	Mountain,
	Ice
};

struct GenerationElevation {
	int maxLevels;

	GenerationElevation() :
		maxLevels(0) { }
};

struct GenerationDoodads {
	bool enabled;
	int density;

	GenerationDoodads() :
		enabled(false), density(12) { }
};

struct GenerationPipeline {
	bool borderizeAfter;
	bool randomizeGround;
	bool placeWalls;

	GenerationPipeline() :
		borderizeAfter(true), randomizeGround(false), placeWalls(false) { }
};

struct ImageMaskRequest {
	wxString imagePath;
	wxString legendPath;
};

struct TextPromptRequest {
	wxString prompt;
};

struct GenerationReference {
	int blendWeight;

	GenerationReference() :
		blendWeight(0) { }
};

struct GenerationSpec {
	GenerationRegion region;
	bool useSelection;
	uint32_t seed;
	GenerationSource source;
	GenerationPreset preset;
	GenerationElevation elevation;
	GenerationDoodads doodads;
	GenerationPipeline pipeline;
	GenerationReference reference;
	bool useSidecar;
	ImageMaskRequest imageMask;
	TextPromptRequest textPrompt;

	GenerationSpec() :
		useSelection(false), seed(1337), source(GenerationSource::ImageMask), preset(GenerationPreset::Auto), useSidecar(false) { }
};

class ProceduralGenerator {
public:
	static bool Run(Editor& editor, const GenerationSpec& spec, wxString& error);
};

#endif
