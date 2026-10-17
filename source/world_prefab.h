#ifndef RME_WORLD_PREFAB_H_
#define RME_WORLD_PREFAB_H_
#include "main.h"
#include "world_plan.h"
#include "map.h"
#include <memory>
namespace WorldPrefab {
    struct Library {
        std::vector<std::unique_ptr<Map>> maps;
        std::vector<int> owners; // Building index for each surface coordinate.
        Tile* TileAt(const WorldGen::Plan& plan, int x, int y, int offsetZ) const;
        bool Covers(const WorldGen::Plan& plan, int x, int y, int offsetZ) const;
    };
    bool Load(const WorldGen::Plan& plan, Library& library, wxString& error, const WorldGen::ProgressCallback& progress);
}
#endif
