#ifndef RME_RENDERING_UTILITIES_TOOLTIP_DRAWER_H_
#define RME_RENDERING_UTILITIES_TOOLTIP_DRAWER_H_

#include "position.h"
#include "rendering/core/render_view.h"

#include <string>
#include <vector>

struct SimpleTooltip {
	int screen_x = 0;
	int screen_y = 0;
	std::string text;
	uint8_t r = 255;
	uint8_t g = 255;
	uint8_t b = 255;
};

class TooltipDrawer {
public:
	TooltipDrawer() = default;
	~TooltipDrawer() = default;

	void addTooltip(int screen_x, int screen_y, const std::string& text, uint8_t r = 255, uint8_t g = 255, uint8_t b = 255);
	void addWaypointTooltip(const Position& pos, const std::string& name, const RenderView& view);
	void draw(wxDC& dc, const RenderView& view) const;
	void clear();

	const std::vector<SimpleTooltip>& getTooltips() const {
		return tooltips_;
	}

private:
	std::vector<SimpleTooltip> tooltips_;
};

#endif
