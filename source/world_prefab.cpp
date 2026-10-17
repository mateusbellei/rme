#include "main.h"
#include "world_prefab.h"
#include "iomap_otbm.h"
#include "items.h"
#include "graphics.h"
#include "complexitem.h"
#include "town.h"
#include <array>

namespace WorldPrefab {
namespace {
    class Reader : public IOMapOTBM {
    public:
        Reader() : IOMapOTBM(MapVersion(MAP_OTBM_2, CLIENT_VERSION_NONE)) {}
        bool Decode(Map& map, NodeFileReadHandle& nodes) { return loadMap(map, nodes); }
    };
    // Bound the legacy reader before it can recurse or allocate map/item nodes.
    bool Structure(const std::string& bytes, wxString& error) {
        struct Node { unsigned char type; size_t payload = 0; std::array<unsigned char, 5> prefix {}; std::vector<unsigned char> tileData; };
        std::vector<Node> stack;
        auto validNode = [&](size_t index) {
            const auto& node = stack[index];
            if (node.type <= 1 && node.payload != 16) return false;
            if (node.type == OTBM_TILE_AREA && (node.payload != 5 || node.prefix[4] > 15)) return false;
            if (node.type == OTBM_TILE) {
                if (node.payload < 2 || !index || stack[index - 1].type != OTBM_TILE_AREA) return false;
                const auto& area = stack[index - 1].prefix;
                if ((unsigned(area[0]) | unsigned(area[1]) << 8) + node.prefix[0] > 65535 || (unsigned(area[2]) | unsigned(area[3]) << 8) + node.prefix[1] > 65535) return false;
                // Do not expose malformed zone lists to the legacy reader, whose
                // zone loop expects a terminating zero. Prefabs forbid map flags.
                const auto& root = stack.front().prefix;
                const unsigned format = unsigned(root[0]) | unsigned(root[1]) << 8 | unsigned(root[2]) << 16 | unsigned(root[3]) << 24;
                for (size_t cursor = 2; cursor < node.tileData.size();) {
                    const auto attribute = node.tileData[cursor++];
                    if (attribute == OTBM_ATTR_TILE_FLAGS) {
                        if (node.tileData.size() - cursor < 4) return false;
                        for (int i = 0; i < 4; ++i) if (node.tileData[cursor++] != 0) return false;
                    } else if (attribute == OTBM_ATTR_ITEM) {
                        if (node.tileData.size() - cursor < 2) return false;
                        const unsigned id = unsigned(node.tileData[cursor]) | unsigned(node.tileData[cursor + 1]) << 8;
                        cursor += 2;
                        if (!g_items.typeExists(id)) return false;
                        const auto& type = g_items[id];
                        if (format == MAP_OTBM_1 && (type.stackable || type.isSplash() || type.isFluidContainer())) {
                            if (cursor == node.tileData.size()) return false;
                            ++cursor;
                        }
                    } else return false;
                }
            }
            if (node.type == OTBM_ITEM && (node.payload < 2 || !g_items.typeExists(unsigned(node.prefix[0]) | unsigned(node.prefix[1]) << 8))) return false;
            return true;
        };
        size_t nodes = 0, tiles = 0, headers = 0;
        bool complete = false;
        for (size_t i = 4; i < bytes.size(); ++i) {
            auto value = static_cast<unsigned char>(bytes[i]);
            if (complete) { error = "Trailing bytes after the OTBM prefab root."; return false; }
            if (value == 0xfe) {
                if (++nodes > 32768 || stack.size() >= 24 || ++i == bytes.size()) { error = "OTBM prefab exceeds its node/depth budget or is truncated."; return false; }
                const auto type = static_cast<unsigned char>(bytes[i]);
                bool valid = false;
                if (stack.empty()) valid = nodes == 1 && type <= 1;
                else {
                    const auto& parent = stack.back();
                    if (!validNode(stack.size() - 1)) { error = "OTBM prefab has invalid node headers or tile coordinates."; return false; }
                    if (parent.type <= 1) valid = type == OTBM_MAP_DATA && ++headers == 1;
                    else if (parent.type == OTBM_MAP_DATA) valid = type == OTBM_TILE_AREA || type == OTBM_TOWNS || type == OTBM_WAYPOINTS;
                    else if (parent.type == OTBM_TILE_AREA) valid = type == OTBM_TILE;
                    else if (parent.type == OTBM_TILE) valid = type == OTBM_ITEM;
                    else if (parent.type == OTBM_ITEM) valid = type == OTBM_ITEM && parent.payload >= 2 && g_items[unsigned(parent.prefix[0]) | unsigned(parent.prefix[1]) << 8].isContainer();
                }
                if (!valid) { error = "OTBM prefab contains misplaced nodes or unsupported house/town/waypoint/spawn metadata."; return false; }
                if (type == OTBM_TILE && ++tiles > 4096) { error = "OTBM prefab exceeds 4096 source tiles."; return false; }
                stack.push_back({type});
            } else if (value == 0xff) {
                if (stack.empty()) { error = "Unbalanced OTBM prefab nodes."; return false; }
                if (!validNode(stack.size() - 1)) { error = "OTBM prefab has invalid node headers or tile coordinates."; return false; }
                stack.pop_back(); complete = stack.empty();
            } else {
                if (stack.empty() || stack.back().payload >= 4096) { error = "Invalid or oversized OTBM prefab node payload."; return false; }
                if (value == 0xfd) {
                    if (++i == bytes.size()) { error = "Truncated OTBM prefab escape."; return false; }
                    value = static_cast<unsigned char>(bytes[i]);
                }
                auto& node = stack.back();
                if (node.payload < node.prefix.size()) node.prefix[node.payload] = value;
                if (node.type == OTBM_TILE) node.tileData.push_back(value);
                ++node.payload;
            }
        }
        if (!complete || !tiles || headers != 1) { error = "OTBM prefab needs a complete map tree with source tiles."; return false; }
        return true;
    }
    bool Header(const std::string& bytes, wxString& error) {
        MemoryNodeFileReadHandle input(reinterpret_cast<const uint8_t*>(bytes.data() + 4), bytes.size() - 4);
        auto* root = input.getRootNode();
        uint32_t format = 0, major = 0, minor = 0;
        uint16_t width = 0, height = 0;
        if (!root || !root->skip(1) || !root->getU32(format) || !root->getU16(width) || !root->getU16(height) || !root->getU32(major) || !root->getU32(minor) || format > MAP_OTBM_4 || !width || !height || major != g_items.MajorVersion || minor != g_items.MinorVersion) {
            error = "OTBM prefab header must match the loaded item registry and a supported map format."; return false;
        }
        return true;
    }
    bool ItemSafe(const Item* item, size_t& total, wxString& error, int depth = 0) {
        if (!item || ++total > 16384 || depth > 16 || !g_items.typeExists(item->getID())) { error = "Invalid or oversized OTBM prefab item tree."; return false; }
        const auto& type = g_items[item->getID()];
        const auto* door = dynamic_cast<const Door*>(item);
        if (type.isMetaItem() || type.isTeleport() || type.isFloorChange() || type.isDepot() || item->getUniqueID() || item->getActionID() || (door && door->getDoorID())) {
            error = "OTBM prefab items cannot contain editor-only objects, script IDs, house door IDs, depots or travel."; return false;
        }
        if (auto* container = dynamic_cast<const Container*>(item)) {
            if (container->getItemCount() > 64) { error = "OTBM prefab containers are limited to 64 items."; return false; }
            for (size_t i = 0; i < container->getItemCount(); ++i) if (!ItemSafe(container->getItem(i), total, error, depth + 1)) return false;
        }
        return true;
    }
    bool Validate(Map& map, const WorldGen::Blueprint& blueprint, std::array<size_t, 4>& counts, wxString& error) {
        const int width = int(blueprint.rows.front().size()), height = int(blueprint.rows.size());
        size_t items = 0;
        for (auto it = map.begin(); it != map.end(); ++it) {
            const Tile* tile = (*it)->get();
            if (!tile || tile->empty()) continue;
            const auto pos = tile->getPosition();
            const int x = pos.x - blueprint.prefabX, y = pos.y - blueprint.prefabY, level = blueprint.prefabZ - pos.z;
            if (x < 0 || y < 0 || x >= width || y >= height || level < 0 || level >= blueprint.maxLevels || WorldGen::BlueprintRowsForFloor(blueprint, level)[y][x] == ' ') {
                error = "OTBM prefab contains tiles outside the declared footprint/floors. Export a standalone chunk."; return false;
            }
            if (!tile->ground || tile->house_id || tile->spawn || tile->creature || tile->getMapFlags() || !tile->getZoneIds().empty() || tile->items.size() > 64) {
                error = "OTBM prefab tiles need ground, bounded item stacks and no map/server metadata."; return false;
            }
            const size_t before = items;
            bool blocked = false;
            auto inspect = [&](const Item* item) {
                if (!ItemSafe(item, items, error)) return false;
                const auto& type = g_items[item->getID()];
                blocked = blocked || type.unpassable;
                if (type.sprite) {
                    if (type.sprite->width > 32 || type.sprite->height > 32) { error = "OTBM prefab sprite exceeds its footprint budget."; return false; }
                    for (int dy = 0; dy < type.sprite->height; ++dy) for (int dx = 0; dx < type.sprite->width; ++dx) {
                        if (x - dx < 0 || y - dy < 0 || WorldGen::BlueprintRowsForFloor(blueprint, level)[y - dy][x - dx] == ' ') { error = "OTBM prefab sprite extends outside its declared footprint."; return false; }
                    }
                }
                return true;
            };
            if (!inspect(tile->ground)) return false;
            for (const auto* item : tile->items) if (!inspect(item)) return false;
            counts[size_t(level)] += items - before;
            const char symbol = WorldGen::BlueprintRowsForFloor(blueprint, level)[y][x];
            if ((symbol == '#' && !blocked) || ((symbol == '+' || symbol == 'a' || symbol == 'b') && blocked)) {
                error = "OTBM prefab blocking disagrees with its declared walls, entrance or vertical landings."; return false;
            }
        }
        for (int level = 0; level < blueprint.maxLevels; ++level) {
            const auto& rows = WorldGen::BlueprintRowsForFloor(blueprint, level);
            for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
                const auto* tile = map.getTile(blueprint.prefabX + x, blueprint.prefabY + y, blueprint.prefabZ - level);
                if (rows[y][x] != ' ' && (!tile || !tile->ground)) { error = "OTBM prefab is missing ground on a declared footprint tile."; return false; }
            }
        }
        return true;
    }
}

bool Library::Covers(const WorldGen::Plan& plan, int x, int y, int offsetZ) const {
    if (offsetZ > 0) return false;
    const int index = owners[size_t(y) * plan.settings.width + x];
    if (index < 0) return false;
    const auto& building = plan.buildings[index];
    const auto& blueprint = plan.catalog[building.blueprint];
    return !blueprint.prefabPath.empty() && -offsetZ < building.levels && WorldGen::BlueprintRowsForFloor(blueprint, -offsetZ)[y - building.origin.y][x - building.origin.x] != ' ';
}
Tile* Library::TileAt(const WorldGen::Plan& plan, int x, int y, int offsetZ) const {
    if (!Covers(plan, x, y, offsetZ)) return nullptr;
    const auto& building = plan.buildings[owners[size_t(y) * plan.settings.width + x]];
    const auto& blueprint = plan.catalog[building.blueprint];
    return maps[building.blueprint]->getTile(blueprint.prefabX + x - building.origin.x, blueprint.prefabY + y - building.origin.y, blueprint.prefabZ + offsetZ);
}

bool Load(const WorldGen::Plan& plan, Library& library, wxString& error, const WorldGen::ProgressCallback& progress) {
    library.maps.resize(plan.catalog.size());
    std::vector<std::array<size_t, 4>> counts(plan.catalog.size());
    size_t sourceBytes = 0;
    library.owners.assign(size_t(plan.settings.width) * plan.settings.height, -1);
    for (size_t i = 0; i < plan.buildings.size(); ++i) {
        const auto& building = plan.buildings[i];
        for (int y = 0; y < building.height; ++y) for (int x = 0; x < building.width; ++x) library.owners[size_t(building.origin.y + y) * plan.settings.width + building.origin.x + x] = int(i);
        const auto& blueprint = plan.catalog[building.blueprint];
        if (blueprint.prefabPath.empty() || library.maps[building.blueprint]) continue;
        if (progress && !progress("Loading OTBM construction prefabs", 3)) { error = "World generation cancelled."; return false; }
        std::string bytes, readError;
        uint64_t fingerprint = 0;
        if (!WorldGen::ReadPrefabFile(blueprint.prefabPath, bytes, fingerprint, readError)) { error = wxString::FromUTF8(readError); return false; }
        sourceBytes += bytes.size();
        if (sourceBytes > 16 * 1024 * 1024) { error = "Used OTBM prefab files exceed the combined 16 MiB source budget."; return false; }
        if (fingerprint != blueprint.prefabFingerprint) { error = "OTBM prefab changed since preview. Rebuild the world plan before applying."; return false; }
        if (!Structure(bytes, error) || !Header(bytes, error)) return false;
        auto map = std::make_unique<Map>();
        MemoryNodeFileReadHandle input(reinterpret_cast<const uint8_t*>(bytes.data() + 4), bytes.size() - 4);
        Reader reader;
        if (!reader.Decode(*map, input) || !reader.getWarnings().empty()) { error = "Could not load OTBM prefab without data loss: " + reader.getError(); if (!reader.getWarnings().empty()) error += " " + reader.getWarnings().front(); return false; }
        if (!Validate(*map, blueprint, counts[building.blueprint], error)) return false;
        library.maps[building.blueprint] = std::move(map);
    }
    size_t expandedItems = 0;
    for (const auto& building : plan.buildings) for (int level = 0; level < building.levels; ++level) {
        expandedItems += counts[building.blueprint][size_t(level)];
        if (expandedItems > 2 * 1024 * 1024) { error = "OTBM prefab placements exceed two million expanded ground/items."; return false; }
    }
    return true;
}
}
