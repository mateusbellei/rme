#include "main.h"
#include "main_menubar_view_handler.h"
#include "main_menubar.h"
#include "settings.h"
#include "gui.h"

namespace MenuBarViewHandler {

void SyncViewSettingsFromMenu(MainMenuBar* menu) {
	if (!menu) {
		return;
	}

	g_settings.setInteger(Config::SHOW_ALL_FLOORS, menu->IsItemChecked(MenuBar::SHOW_ALL_FLOORS));
	g_settings.setInteger(Config::TRANSPARENT_FLOORS, menu->IsItemChecked(MenuBar::GHOST_HIGHER_FLOORS));
	g_settings.setInteger(Config::TRANSPARENT_ITEMS, menu->IsItemChecked(MenuBar::GHOST_ITEMS));
	g_settings.setInteger(Config::SHOW_INGAME_BOX, menu->IsItemChecked(MenuBar::SHOW_INGAME_BOX));
	g_settings.setInteger(Config::SHOW_LIGHTS, menu->IsItemChecked(MenuBar::SHOW_LIGHTS));
	g_settings.setInteger(Config::SHOW_LIGHT_STR, menu->IsItemChecked(MenuBar::SHOW_LIGHT_STR));
	g_settings.setInteger(Config::SHOW_TECHNICAL_ITEMS, menu->IsItemChecked(MenuBar::SHOW_TECHNICAL_ITEMS));
	g_settings.setInteger(Config::SHOW_WAYPOINTS, menu->IsItemChecked(MenuBar::SHOW_WAYPOINTS));
	g_settings.setInteger(Config::SHOW_GRID, menu->IsItemChecked(MenuBar::SHOW_GRID));
	g_settings.setInteger(Config::SHOW_EXTRA, !menu->IsItemChecked(MenuBar::SHOW_EXTRA));
	g_settings.setInteger(Config::SHOW_SHADE, menu->IsItemChecked(MenuBar::SHOW_SHADE));
	g_settings.setInteger(Config::SHOW_SPECIAL_TILES, menu->IsItemChecked(MenuBar::SHOW_SPECIAL));
	g_settings.setInteger(Config::SHOW_ZONE_AREAS, menu->IsItemChecked(MenuBar::SHOW_ZONES));
	g_settings.setInteger(Config::SHOW_AS_MINIMAP, menu->IsItemChecked(MenuBar::SHOW_AS_MINIMAP));
	g_settings.setInteger(Config::SHOW_ONLY_TILEFLAGS, menu->IsItemChecked(MenuBar::SHOW_ONLY_COLORS));
	g_settings.setInteger(Config::SHOW_ONLY_MODIFIED_TILES, menu->IsItemChecked(MenuBar::SHOW_ONLY_MODIFIED));
	g_settings.setInteger(Config::SHOW_CREATURES, menu->IsItemChecked(MenuBar::SHOW_CREATURES));
	g_settings.setInteger(Config::SHOW_SPAWNS, menu->IsItemChecked(MenuBar::SHOW_SPAWNS));
	g_settings.setInteger(Config::SHOW_HOUSES, menu->IsItemChecked(MenuBar::SHOW_HOUSES));
	g_settings.setInteger(Config::HIGHLIGHT_ITEMS, menu->IsItemChecked(MenuBar::HIGHLIGHT_ITEMS));
	g_settings.setInteger(Config::HIGHLIGHT_LOCKED_DOORS, menu->IsItemChecked(MenuBar::HIGHLIGHT_LOCKED_DOORS));
	g_settings.setInteger(Config::SHOW_BLOCKING, menu->IsItemChecked(MenuBar::SHOW_PATHING));
	g_settings.setInteger(Config::SHOW_TOOLTIPS, menu->IsItemChecked(MenuBar::SHOW_TOOLTIPS));
	g_settings.setInteger(Config::SHOW_PREVIEW, menu->IsItemChecked(MenuBar::SHOW_PREVIEW));
	g_settings.setInteger(Config::SHOW_WALL_HOOKS, menu->IsItemChecked(MenuBar::SHOW_WALL_HOOKS));
	g_settings.setInteger(Config::SHOW_TOWNS, menu->IsItemChecked(MenuBar::SHOW_TOWNS));
	g_settings.setInteger(Config::ALWAYS_SHOW_ZONES, menu->IsItemChecked(MenuBar::ALWAYS_SHOW_ZONES));
	g_settings.setInteger(Config::EXT_HOUSE_SHADER, menu->IsItemChecked(MenuBar::EXT_HOUSE_SHADER));
	g_settings.setInteger(Config::EXPERIMENTAL_FOG, menu->IsItemChecked(MenuBar::EXPERIMENTAL_FOG));
	g_settings.setInteger(Config::SHOW_FPS, menu->IsItemChecked(MenuBar::SHOW_FPS));
	g_settings.setInteger(Config::USE_MODERN_RENDERER, menu->IsItemChecked(MenuBar::USE_MODERN_RENDERER));
}

void CheckViewSettingsOnMenu(MainMenuBar* menu) {
	if (!menu) {
		return;
	}
	menu->CheckItem(MenuBar::SHOW_FPS, g_settings.getBoolean(Config::SHOW_FPS));
	menu->CheckItem(MenuBar::USE_MODERN_RENDERER, g_settings.getBoolean(Config::USE_MODERN_RENDERER));
}

} // namespace MenuBarViewHandler
