#include "world_decoration.h"

#include <iostream>
#include <stdexcept>
#include <queue>

// Independent whole-map flood: every remaining free cell must reach the map
// boundary. The production algorithm checks only the bounded clearance area.
static bool ReachesBoundary(const WorldGen::DecorationGrid& grid, WorldGen::Point anchor, const std::vector<WorldGen::DecorationCell>& footprint) {
	auto free=grid.walkable;
	for(const auto& cell:footprint)if(cell.blocking)free[size_t(anchor.y+cell.offset.y)*grid.width+anchor.x+cell.offset.x]=0;
	std::vector<uint8_t> seen(free.size());std::queue<int> queue;
	for(int y=0;y<grid.height;++y)for(int x=0;x<grid.width;++x)if((x==0 || y==0 || x==grid.width-1 || y==grid.height-1) && free[size_t(y)*grid.width+x]){const int i=y*grid.width+x;seen[i]=1;queue.push(i);}
	while(!queue.empty()) {
		const int i=queue.front();queue.pop();
		for(const auto [dx,dy]:{std::pair{1,0},std::pair{-1,0},std::pair{0,1},std::pair{0,-1}}) {
			const int x=i%grid.width+dx,y=i/grid.width+dy;if(x<0 || y<0 || x>=grid.width || y>=grid.height)continue;
			const int next=y*grid.width+x;if(free[next] && !seen[next]){seen[next]=1;queue.push(next);}
		}
	}
	for(size_t i=0;i<free.size();++i)if(free[i] && !seen[i])return false;
	return true;
}

static void Check(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

int main() {
	try {

        std::vector<uint8_t> mask(100,0);
        Check(WorldGen::PaintNatureMask(mask,10,10,{0,0},{9,9},1,true),"Interpolated mask stroke clips at corners");
        for(int i=0;i<10;++i)Check(mask[i*10+i]==1,"Fast diagonal stroke leaves no gaps");
        const auto painted=mask;
        Check(!WorldGen::PaintNatureMask(mask,10,10,{-1,0},{5,5},1,true) && mask==painted,"Invalid stroke is atomic");
        Check(WorldGen::PaintNatureMask(mask,10,10,{5,5},{5,5},2,false) && mask[55]==0 && mask[0]==1,"Preservation brush erases only its circle");
        // Compare every bounded transform with independent brute-force Manhattan distance.
        WorldGen::Layer layer;layer.cells.resize(13*11);
        for(auto& c:layer.cells){c.terrain=WorldGen::Terrain::Grass;c.elevation=1000;}
        layer.cells[3*13+4].terrain=WorldGen::Terrain::River;
        layer.cells[8*13+9].terrain=WorldGen::Terrain::Road;
        layer.cells[5*13+6].terrain=WorldGen::Terrain::Forest;
        layer.cells[5*13+6].elevation=7000;
        WorldGen::DecorationEnvironment environment;
        Check(WorldGen::BuildDecorationEnvironment(layer,13,11,environment),"Build immutable ecology distances");
        for(int y=0;y<11;++y)for(int x=0;x<13;++x) {
            const auto i=y*13+x;
            Check(environment.water[i]==std::abs(x-4)+std::abs(y-3),"Water transform equals independent distance");
            Check(environment.path[i]==std::abs(x-9)+std::abs(y-8),"Path transform equals independent distance");
            const int forest=std::max(0,std::abs(x-6)+std::abs(y-5)-1);
            Check(environment.forestEdge[i]==forest,"Both sides of forest margin seed the transform");
            Check(environment.slope[i]==((std::abs(x-6)+std::abs(y-5)<=1)?6000:0),"Slope uses cardinal elevation differences");
        }
        layer.cells[3*13+4].terrain=WorldGen::Terrain::Grass;
        Check(WorldGen::BuildDecorationEnvironment(layer,13,11,environment) && environment.water[0]==33,"Missing source uses bounded sentinel");
        Check(!WorldGen::BuildDecorationEnvironment(layer,0,11,environment) && environment.water[0]==33,"Invalid input leaves output untouched");
        for(int d=0;d<=8;++d)Check(WorldGen::TransitionDensity(d,8,20)==20+10*d,"Density interpolates monotonically");
        Check(WorldGen::TransitionDensity(33,8,20)==100 && WorldGen::TransitionDensity(0,0,20)==100,"Transitions clamp or disable cleanly");
		WorldGen::DecorationGrid grid;
		grid.width = grid.height = 32;
		grid.walkable.assign(1024, 1);
		grid.reserved.assign(1024, 0);
		grid.occupied.assign(1024, 0);
		grid.habitat.assign(1024, 1);
		for(const auto [width,height]:{std::pair{2,4},std::pair{4,2},std::pair{3,3}})for(unsigned mask=0;mask<(1u<<(width*height));++mask) {
			std::vector<WorldGen::DecorationCell> footprint{{{0,0},false}};
			for(int y=0;y<height;++y)for(int x=0;x<width;++x)if(mask&(1u<<(y*width+x)))footprint.push_back({{x,y},true});
			Check(WorldGen::CanPlaceDecoration(grid,{10,10},footprint)==ReachesBoundary(grid,{10,10},footprint),"All narrow and 3x3 blocker configurations must match independent whole-map reachability");
		}
		{
			WorldGen::DecorationGrid large;large.width=large.height=64;
			large.walkable.assign(4096,1);large.reserved.assign(4096,0);large.occupied.assign(4096,0);large.habitat.assign(4096,1);
			std::vector<WorldGen::DecorationCell> corners{{{0,0},true},{{31,0},true},{{0,31},true},{{31,31},true}};
			Check(WorldGen::CanPlaceDecoration(large,{10,10},corners)==ReachesBoundary(large,{10,10},corners),"Maximum 34x34 clearance must fit bounded flood storage");
			for(int y=14;y<=16;++y)for(int x=14;x<=16;++x)if(x!=15 || y!=15)corners.push_back({{x,y},true});
			Check(!WorldGen::CanPlaceDecoration(large,{10,10},corners) && !ReachesBoundary(large,{10,10},corners),"A pocket inside maximum clearance still fails the full connectivity check");
		}
		std::string reason;
		std::vector<WorldGen::DecorationCell> single = { { { 0, 0 }, true } };
		Check(WorldGen::CanPlaceDecoration(grid, { 10, 10 }, single, &reason), "A single tree with clear surrounding floor must fit");
		Check(!WorldGen::CanPlaceDecoration(grid, { 0, 0 }, single, &reason), "Footprint clearance must stay within the region");
		grid.reserved[10 * 32 + 11] = 1;
		Check(!WorldGen::CanPlaceDecoration(grid, { 10, 10 }, single), "Road/entrance clearance must be protected");
		grid.reserved[10 * 32 + 11] = 0;
		grid.walkable[10 * 32 + 11] = 0;
		Check(!WorldGen::CanPlaceDecoration(grid, { 10, 10 }, single), "Decoration cannot choke a narrow cave corridor");
		grid.walkable[10 * 32 + 11] = 1;
		grid.habitat[10 * 32 + 11] = 2;
		Check(!WorldGen::CanPlaceDecoration(grid, { 10, 10 }, single), "Composite clearance cannot spill into an incompatible biome");
		grid.habitat[10 * 32 + 11] = 1;
		WorldGen::MarkDecoration(grid, { 10, 10 }, single);
		Check(!grid.walkable[10 * 32 + 10] && grid.occupied[10 * 32 + 10], "Accepted blocking footprint must update occupancy");
		Check(!WorldGen::CanPlaceDecoration(grid, { 11, 10 }, single), "Subsequent objects must respect existing props");
		std::vector<WorldGen::DecorationCell> composite;
		for (int y = 0; y < 3; ++y) {
			for (int x = 0; x < 3; ++x) {
				if (x == 0 || y == 0 || x == 2 || y == 2) {
					composite.push_back({ { x, y }, true });
				}
			}
		}
		Check(!WorldGen::CanPlaceDecoration(grid, { 20, 20 }, composite, &reason), "A blocking ring cannot trap a walkable pocket");
		composite.push_back({ { 1, 1 }, true });
		Check(WorldGen::CanPlaceDecoration(grid, { 20, 20 }, composite), "A solid multi-tile rock with an open perimeter must fit");
		Check(!WorldGen::CanPlaceDecoration(grid, { 12, 12 }, { { { 32767, 0 }, true } }), "Oversized composite offsets must fail before indexing");
		grid.occupied.clear();
		Check(!WorldGen::CanPlaceDecoration(grid, { 10, 10 }, single), "Malformed occupancy data must fail safely");
		std::cout << "PASS: decoration bounds, multi-tile occupancy, biome clearance, route protection and local connectivity\n";
		return 0;
	} catch (const std::exception& e) {
		std::cerr << "FAIL: " << e.what() << '\n';
		return 1;
	}
}
