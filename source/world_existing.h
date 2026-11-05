#ifndef RME_WORLD_EXISTING_H_
#define RME_WORLD_EXISTING_H_
#include "world_plan.h"
#include <map>

// Read-only interpretation and bounded interventions on an authored raster.
// Unknown or occupied cells are never treated as empty land.
namespace WorldExisting {
    struct Cell {
        WorldGen::Terrain terrain=WorldGen::Terrain::Void;
        bool exists=false,walkable=false,editable=false,structure=false;
        bool highland=false;
        bool travel=false;
        bool bare=true; // Borders may accept additive nature, but never a replacement path.
    };
    struct Settings {
        int width=0,height=0,protectionRadius=2,maxGap=8,maxConnections=16;
        bool connectPaths=true;
    };
    struct Connection { std::vector<WorldGen::Point> cells; };
    struct Analysis {
        Settings settings;
        std::vector<Cell> cells;
        std::vector<uint8_t> eligible;
        std::vector<int> roadComponents;
        std::vector<Connection> connections;
        size_t recognized=0,unknown=0,structures=0,protectedTiles=0,naturalTiles=0,travelAnchors=0;
        int roadComponentCount=0;
    };
    bool Natural(WorldGen::Terrain terrain);
    bool Road(WorldGen::Terrain terrain);
    bool Analyze(const Settings& settings,const std::vector<Cell>& cells,Analysis& result,std::string& error,const WorldGen::ProgressCallback& progress={});
}
#endif
