#include "main.h"
#include "rendering/utilities/tooltip_drawer.h"
#include "rendering/core/render_view.h"
#include "position.h"

void TooltipDrawer::addTooltip(int screen_x, int screen_y, const std::string& text, uint8_t r, uint8_t g, uint8_t b) {
	if (text.empty()) {
		return;
	}
	tooltips_.push_back(SimpleTooltip { screen_x, screen_y, text, r, g, b });
}

void TooltipDrawer::addWaypointTooltip(const Position& pos, const std::string& name, const RenderView& view) {
	if (name.empty()) {
		return;
	}
	int screen_x = 0;
	int screen_y = 0;
	view.getScreenPosition(pos.x, pos.y, pos.z, screen_x, screen_y);
	addTooltip(screen_x, screen_y + 8, "Waypoint: " + name, 64, 255, 64);
}

void TooltipDrawer::draw(wxDC& dc, const RenderView& view) const {
	if (tooltips_.empty()) {
		return;
	}

	const float zoom = view.zoom < 0.01f ? 1.0f : view.zoom;
	dc.SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));

	for (const auto& tooltip : tooltips_) {
		const int draw_x = static_cast<int>(tooltip.screen_x / zoom);
		const int draw_y = static_cast<int>(tooltip.screen_y / zoom);
		dc.SetTextForeground(wxColour(tooltip.r, tooltip.g, tooltip.b));
		dc.DrawText(wxstr(tooltip.text), draw_x, draw_y);
	}
}

void TooltipDrawer::clear() {
	tooltips_.clear();
}
