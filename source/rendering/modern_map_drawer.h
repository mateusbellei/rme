#ifndef RME_MODERN_MAP_DRAWER_H_
#define RME_MODERN_MAP_DRAWER_H_

#include "rendering/core/drawing_options.h"

#include <memory>

class MapCanvas;
class wxDC;
class AtlasManager;

class ModernMapDrawer {
public:
	explicit ModernMapDrawer(MapCanvas* canvas);
	~ModernMapDrawer();

	void SetupVars();
	void SetupGL();
	void Release();
	void Draw();
	void DrawTooltips(wxDC& dc);

	DrawingOptions& getOptions() {
		return options_;
	}

private:
	void DrawBackground();
	void DrawMap();
	void DrawHigherFloors(AtlasManager& atlas);
	void DrawLight();

	struct Impl;
	std::unique_ptr<Impl> impl_;
	MapCanvas* canvas_;
	DrawingOptions options_;
};

#endif
