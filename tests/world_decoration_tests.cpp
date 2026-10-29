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
