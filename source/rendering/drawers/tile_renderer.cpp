#include "rendering/rendering_gl_first.h"

#include "complexitem.h"
#include "creature.h"
#include "editor.h"
#include "graphics.h"
#include "gui.h"
#include "item.h"
#include "items.h"
#include "map.h"
#include "map_region.h"
#include "rendering/core/light_buffer.h"
#include "rendering/core/modern_sprite_bridge.h"
#include "rendering/core/render_constants.h"
#include "rendering/core/render_view.h"
#include "rendering/core/sprite_batch.h"
#include "rendering/core/zone_colors.h"
#include "rendering/core/drawing_options.h"
#include "rendering/drawers/tile_renderer.h"
#include "rendering/utilities/tooltip_drawer.h"
#include "sprites.h"
#include "tile.h"
#include "waypoints.h"

#include <algorithm>
#include <sstream>

namespace {
	const AtlasRegion* ensureAtlasSprite(GraphicManager& gfx, GameSprite* spr, int cx, int cy, int cf, int subtype, int pattern_x, int pattern_y, int pattern_z, int frame) {
		if (!spr) {
			return nullptr;
		}
		return ModernSpriteBridge::get().ensureAtlasSpritePart(gfx, *spr, cx, cy, cf, subtype, pattern_x, pattern_y, pattern_z, frame);
	}

	void blitAtlasQuad(SpriteBatch& batch, int sx, int sy, const AtlasRegion* region, float r, float g, float b, float a) {
		if (!region) {
			return;
		}
		batch.draw(static_cast<float>(sx), static_cast<float>(sy), static_cast<float>(TILE_SIZE), static_cast<float>(TILE_SIZE), *region, r, g, b, a);
	}
}

TileRenderer::TileRenderer(Editor* editor) :
	editor_(editor) {
}

void TileRenderer::BeginLayer() {
	zone_tiles_.clear();
}

void TileRenderer::calculateTileColor(const Tile* tile, const TileLocation* location, const DrawingOptions& options, uint32_t current_house_id, uint8_t& r, uint8_t& g, uint8_t& b) const {
	r = 255;
	g = 255;
	b = 255;

	const bool showspecial = options.show_only_colors || options.show_special_tiles;

	if (options.show_blocking && tile->isBlocking() && tile->size() > 0) {
		g = g / 3 * 2;
		b = b / 3 * 2;
	}

	const int item_count = static_cast<int>(tile->items.size());
	if (options.highlight_items && item_count > 0 && !tile->items.back()->isBorder()) {
		static const float factor[5] = { 0.75f, 0.6f, 0.48f, 0.40f, 0.33f };
		const int idx = (item_count < 5 ? item_count : 5) - 1;
		g = static_cast<uint8_t>(g * factor[idx]);
		r = static_cast<uint8_t>(r * factor[idx]);
	}

	if (options.show_spawns && location->getSpawnCount() > 0) {
		float f = 1.0f;
		for (uint32_t i = 0; i < location->getSpawnCount(); ++i) {
			f *= 0.7f;
		}
		g = static_cast<uint8_t>(g * f);
		b = static_cast<uint8_t>(b * f);
	}

	if (options.show_houses && tile->isHouseTile()) {
		if (static_cast<int>(tile->getHouseID()) == static_cast<int>(current_house_id)) {
			r /= 2;
		} else {
			r /= 2;
			g /= 2;
		}
	} else if (showspecial && tile->isPZ()) {
		r /= 2;
		b /= 2;
	}

	if (showspecial && tile->getMapFlags() & TILESTATE_PVPZONE) {
		g = r / 4;
		b = b / 3 * 2;
	}

	if (showspecial && tile->getMapFlags() & TILESTATE_NOLOGOUT) {
		b /= 2;
	}

	if (showspecial && tile->getMapFlags() & TILESTATE_NOPVP) {
		g /= 2;
	}

	if (options.show_zone_areas && tile->getMapFlags() & TILESTATE_ZONE_BRUSH) {
		ComputeZoneTint(tile->getZoneIds(), r, g, b);
	}
}

void TileRenderer::drawSquare(SpriteBatch& sprite_batch, AtlasManager& atlas, int screen_x, int screen_y, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha, int size) {
	if (size == 0) {
		size = TILE_SIZE;
	}
	sprite_batch.drawRect(
		static_cast<float>(screen_x),
		static_cast<float>(screen_y),
		static_cast<float>(size),
		static_cast<float>(size),
		glm::vec4(r / 255.0f, g / 255.0f, b / 255.0f, alpha / 255.0f),
		atlas
	);
}

void TileRenderer::drawRawBrush(SpriteBatch& sprite_batch, AtlasManager& atlas, GraphicManager& gfx, int screen_x, int screen_y, uint32_t sprite_id, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha) {
	blitSpriteType(sprite_batch, atlas, gfx, screen_x, screen_y, sprite_id, r, g, b, alpha);
}

void TileRenderer::blitSpriteType(SpriteBatch& sprite_batch, AtlasManager& atlas, GraphicManager& gfx, int screen_x, int screen_y, uint32_t sprite_id, int red, int green, int blue, int alpha) {
	(void)atlas;
	GameSprite* spr = g_items[sprite_id].sprite;
	if (!spr) {
		return;
	}

	screen_x -= spr->getDrawOffset().first;
	screen_y -= spr->getDrawOffset().second;
	for (int cx = 0; cx != spr->width; ++cx) {
		for (int cy = 0; cy != spr->height; ++cy) {
			for (int cf = 0; cf != spr->layers; ++cf) {
				const AtlasRegion* region = ensureAtlasSprite(gfx, spr, cx, cy, cf, -1, 0, 0, 0, 0);
				blitAtlasQuad(sprite_batch, screen_x - cx * TILE_SIZE, screen_y - cy * TILE_SIZE, region, red / 255.0f, green / 255.0f, blue / 255.0f, alpha / 255.0f);
			}
		}
	}
}

void TileRenderer::blitItem(
	SpriteBatch& sprite_batch,
	AtlasManager& atlas,
	GraphicManager& gfx,
	int& draw_x,
	int& draw_y,
	const Tile* tile,
	Item* item,
	const DrawingOptions& options,
	int red,
	int green,
	int blue,
	int alpha,
	LightBuffer* light_buffer,
	const RenderView& view
) {
	if (!item) {
		return;
	}

	ItemType& it = g_items[item->getID()];
	if (!options.ingame && options.highlight_locked_doors && it.isDoor() && it.isLocked) {
		blue /= 2;
		green /= 2;
	}

	if (!options.ingame && item->isSelected()) {
		red /= 2;
		blue /= 2;
		green /= 2;
	}

	GameSprite* spr = it.sprite;
	if (!options.ingame && options.show_tech_items) {
		if (it.id == 0) {
			drawSquare(sprite_batch, atlas, draw_x, draw_y, static_cast<uint8_t>(red), 0, 0, static_cast<uint8_t>(alpha));
			return;
		}
	}

	if (it.isMetaItem() || spr == nullptr || (!options.show_items && it.pickupable)) {
		return;
	}

	int screen_x = draw_x - spr->getDrawOffset().first;
	int screen_y = draw_y - spr->getDrawOffset().second;
	draw_x -= spr->getDrawHeight();
	draw_y -= spr->getDrawHeight();

	int subtype = -1;
	const Position& pos = tile ? tile->getPosition() : Position();
	int pattern_x = pos.x % spr->pattern_x;
	int pattern_y = pos.y % spr->pattern_y;
	int pattern_z = pos.z % spr->pattern_z;

	if (it.isSplash() || it.isFluidContainer()) {
		subtype = item->getSubtype();
	} else if (it.isHangable && tile) {
		if (tile->hasProperty(HOOK_SOUTH)) {
			pattern_x = 1;
		} else if (tile->hasProperty(HOOK_EAST)) {
			pattern_x = 2;
		} else {
			pattern_x = 0;
		}
	} else if (it.stackable) {
		const int count = item->getSubtype();
		if (count <= 1) {
			subtype = 0;
		} else if (count <= 2) {
			subtype = 1;
		} else if (count <= 3) {
			subtype = 2;
		} else if (count <= 4) {
			subtype = 3;
		} else if (count < 10) {
			subtype = 4;
		} else if (count < 25) {
			subtype = 5;
		} else if (count < 50) {
			subtype = 6;
		} else {
			subtype = 7;
		}
	}

	if (options.transparent_items && (!it.isGroundTile() || spr->width > 1 || spr->height > 1) && !it.isSplash() && (!it.isBorder || spr->width > 1 || spr->height > 1)) {
		alpha /= 2;
	}

	const int frame = item->getFrame();
	for (int cx = 0; cx != spr->width; ++cx) {
		for (int cy = 0; cy != spr->height; ++cy) {
			for (int cf = 0; cf != spr->layers; ++cf) {
				const AtlasRegion* region = ensureAtlasSprite(gfx, spr, cx, cy, cf, subtype, pattern_x, pattern_y, pattern_z, frame);
				blitAtlasQuad(sprite_batch, screen_x - cx * TILE_SIZE, screen_y - cy * TILE_SIZE, region, red / 255.0f, green / 255.0f, blue / 255.0f, alpha / 255.0f);
			}
		}
	}

	if (spr->hasLight() && light_buffer) {
		const SpriteLight light = item->getLight();
		if (light.intensity > 0) {
			light_buffer->AddScreenLight(draw_x, draw_y, view, light);
		}
	}
}

void TileRenderer::collectZonePositions(const Tile* tile) {
	const auto& zone_ids = tile->getZoneIds();
	if (zone_ids.empty()) {
		return;
	}
	for (const auto zone_id : zone_ids) {
		auto it = zone_tiles_.find(zone_id);
		if (it == zone_tiles_.end()) {
			zone_tiles_.emplace(zone_id, std::vector<FinderPosition> { FinderPosition(tile->getX(), tile->getY(), tile->getZ()) });
		} else {
			it->second.push_back(FinderPosition(tile->getX(), tile->getY(), tile->getZ()));
		}
	}
}

void TileRenderer::writeItemTooltip(Tile* tile, Item* item, TooltipDrawer* tooltip_drawer, bool is_house_tile, int map_z, const RenderView& view) {
	if (!tooltip_drawer || map_z != view.floor || !item) {
		return;
	}

	const uint16_t id = item->getID();
	if (id < 100) {
		return;
	}

	const auto& zone_ids = tile->getZoneIds();
	const uint16_t unique = item->getUniqueID();
	const uint16_t action = item->getActionID();
	const std::string& text = item->getText();
	uint8_t door_id = 0;

	if (is_house_tile && item->isDoor()) {
		if (Door* door = dynamic_cast<Door*>(item)) {
			if (door->isRealDoor()) {
				door_id = door->getDoorID();
			}
		}
	}

	Teleport* tp = dynamic_cast<Teleport*>(item);
	if (unique == 0 && action == 0 && door_id == 0 && text.empty() && !tp && zone_ids.empty()) {
		return;
	}

	collectZonePositions(tile);

	std::ostringstream stream;
	if (zone_ids.empty()) {
		stream << "id: " << id;
	}
	if (action > 0) {
		stream << "\naid: " << action;
	}
	if (unique > 0) {
		stream << "\nuid: " << unique;
	}
	if (door_id > 0) {
		stream << "\ndoor id: " << static_cast<int>(door_id);
	}
	if (!text.empty()) {
		stream << "\ntext: " << text;
	}
	if (tp) {
		const Position& dest = tp->getDestination();
		stream << "\ndestination: " << dest.x << ", " << dest.y << ", " << dest.z;
	}

	int screen_x = 0;
	int screen_y = 0;
	view.getScreenPosition(tile->getX(), tile->getY(), tile->getZ(), screen_x, screen_y);
	tooltip_drawer->addTooltip(screen_x, screen_y + 8, stream.str());
}

void TileRenderer::DrawTile(
	SpriteBatch& sprite_batch,
	AtlasManager& atlas,
	GraphicManager& gfx,
	const TileLocation* location,
	const RenderView& view,
	const DrawingOptions& options,
	TooltipDrawer* tooltip_drawer,
	int in_draw_x,
	int in_draw_y,
	LightBuffer* light_buffer
) {
	if (!location || !editor_) {
		return;
	}

	Tile* tile = const_cast<Tile*>(location->get());
	if (!tile) {
		return;
	}

	if (options.show_only_modified && !tile->isModified()) {
		return;
	}

	const int map_x = location->getX();
	const int map_y = location->getY();
	const int map_z = location->getZ();

	int draw_x = 0;
	int draw_y = 0;
	if (in_draw_x != -1 && in_draw_y != -1) {
		draw_x = in_draw_x;
		draw_y = in_draw_y;
	} else if (!view.IsTileVisible(map_x, map_y, map_z, draw_x, draw_y)) {
		return;
	}

	Waypoint* waypoint = nullptr;
	if (location->getWaypointCount() > 0) {
		waypoint = editor_->map.waypoints.getWaypoint(const_cast<TileLocation*>(location));
		if (options.show_tooltips && waypoint && map_z == view.floor) {
			tooltip_drawer->addWaypointTooltip(tile->getPosition(), waypoint->name, view);
		}
	}

	const bool as_minimap = options.show_as_minimap;
	const bool only_colors = as_minimap || options.show_only_colors;

	uint8_t r = 255;
	uint8_t g = 255;
	uint8_t b = 255;
	if (!as_minimap) {
		calculateTileColor(tile, location, options, options.current_house_id, r, g, b);
	}

	if (only_colors) {
		if (as_minimap) {
			const uint8_t color = tile->getMiniMapColor();
			r = static_cast<uint8_t>(int(color / 36) % 6 * 51);
			g = static_cast<uint8_t>(int(color / 6) % 6 * 51);
			b = static_cast<uint8_t>(color % 6 * 51);
			drawSquare(sprite_batch, atlas, draw_x, draw_y, r, g, b, 255);
		} else if (r != 255 || g != 255 || b != 255) {
			drawSquare(sprite_batch, atlas, draw_x, draw_y, r, g, b, 128);
		}
	} else {
		if (tile->ground) {
			if (options.show_preview && view.zoom <= 2.0) {
				tile->ground->animate();
			}
			blitItem(sprite_batch, atlas, gfx, draw_x, draw_y, tile, tile->ground, options, r, g, b, 255, light_buffer, view);
		} else if (options.always_show_zones && (r != 255 || g != 255 || b != 255)) {
			drawRawBrush(sprite_batch, atlas, gfx, draw_x, draw_y, SPRITE_ZONE, r, g, b, 60);
		}
	}

	if (options.show_tooltips && map_z == view.floor && tile->ground) {
		writeItemTooltip(tile, tile->ground, tooltip_drawer, tile->isHouseTile(), map_z, view);
	}

	if (!only_colors) {
		if (options.shouldDrawDetailedItems(view.zoom)) {
			for (Item* item : tile->items) {
				if (options.show_tooltips && map_z == view.floor) {
					writeItemTooltip(tile, item, tooltip_drawer, tile->isHouseTile(), map_z, view);
				}
				if (options.show_preview && view.zoom <= 2.0) {
					item->animate();
				}
				if (item->isBorder()) {
					blitItem(sprite_batch, atlas, gfx, draw_x, draw_y, tile, item, options, r, g, b, 255, light_buffer, view);
				} else {
					uint8_t ir = 255;
					uint8_t ig = 255;
					uint8_t ib = 255;
					if (options.extended_house_shader && options.show_houses && tile->isHouseTile()) {
						if (static_cast<int>(tile->getHouseID()) == static_cast<int>(options.current_house_id)) {
							ir /= 2;
						} else {
							ir /= 2;
							ig /= 2;
						}
					}
					blitItem(sprite_batch, atlas, gfx, draw_x, draw_y, tile, item, options, ir, ig, ib, 255, light_buffer, view);
				}
			}

			if (tile->creature && options.show_creatures) {
				const Outfit& outfit = tile->creature->getLookType();
				if (outfit.lookType != 0) {
					GameSprite* spr = g_gui.gfx.getCreatureSprite(outfit.lookType);
					if (spr) {
						int screen_x = draw_x - spr->getDrawOffset().first;
						int screen_y = draw_y - spr->getDrawOffset().second;
						const int direction = tile->creature->getDirection();
						const int pattern_z = tile->getZ() % spr->pattern_z;
						for (int cx = 0; cx != spr->width; ++cx) {
							for (int cy = 0; cy != spr->height; ++cy) {
								for (int cf = 0; cf != spr->layers; ++cf) {
									const AtlasRegion* region = ensureAtlasSprite(gfx, spr, cx, cy, cf, -1, direction, 0, pattern_z, 0);
									blitAtlasQuad(sprite_batch, screen_x - cx * TILE_SIZE, screen_y - cy * TILE_SIZE, region, 1.0f, 1.0f, 1.0f, 1.0f);
								}
							}
						}
					}
				}
			}
		}

		if (view.zoom < 10.0) {
			if (!options.ingame && waypoint && options.show_waypoints) {
				blitSpriteType(sprite_batch, atlas, gfx, draw_x, draw_y, SPRITE_WAYPOINT, 64, 64, 255, 255);
			}
			if (tile->isHouseExit() && options.show_houses) {
				if (tile->hasHouseExit(options.current_house_id)) {
					blitSpriteType(sprite_batch, atlas, gfx, draw_x, draw_y, SPRITE_HOUSE_EXIT, 64, 255, 255, 255);
				} else {
					blitSpriteType(sprite_batch, atlas, gfx, draw_x, draw_y, SPRITE_HOUSE_EXIT, 64, 64, 255, 255);
				}
			}
			if (options.show_towns && tile->isTownExit(editor_->map)) {
				blitSpriteType(sprite_batch, atlas, gfx, draw_x, draw_y, SPRITE_TOWN_TEMPLE, 255, 255, 64, 170);
			}
			if (tile->spawn && options.show_spawns) {
				if (tile->spawn->isSelected()) {
					blitSpriteType(sprite_batch, atlas, gfx, draw_x, draw_y, SPRITE_SPAWN, 128, 128, 128, 255);
				} else {
					blitSpriteType(sprite_batch, atlas, gfx, draw_x, draw_y, SPRITE_SPAWN, 255, 255, 255, 255);
				}
			}
		}
	}
}

void TileRenderer::FinishLayer(const RenderView& view, TooltipDrawer* tooltip_drawer) {
	if (!tooltip_drawer || !editor_) {
		return;
	}

	for (auto& zone_entry : zone_tiles_) {
		ZoneFinder finder(zone_entry.second);
		const auto zones = finder.findZones();
		for (const auto& zone : zones) {
			const FinderPosition center = finder.findClosestToCenter(zone);
			QTreeNode* nd = editor_->map.getLeaf(center.x, center.y);
			if (!nd) {
				continue;
			}
			TileLocation* location = nd->getTile(center.x, center.y, center.z);
			if (!location) {
				continue;
			}
			const Tile* tile = location->get();
			if (!tile) {
				continue;
			}

			std::ostringstream tooltip;
			tooltip << "zone id: ";
			size_t remaining = tile->getZoneIds().size();
			for (const auto zone_id : tile->getZoneIds()) {
				tooltip << zone_id;
				if (--remaining > 0) {
					tooltip << "/";
				}
			}

			int screen_x = 0;
			int screen_y = 0;
			view.getScreenPosition(tile->getX(), tile->getY(), tile->getZ(), screen_x, screen_y);
			tooltip_drawer->addTooltip(screen_x, screen_y + 8, tooltip.str(), 255, 255, 128);
		}
	}
}
