#include "world_decoration.h"

#include <algorithm>
#include <array>
#include <cstdlib>

namespace WorldGen {
	bool PaintNatureMask(std::vector<uint8_t>& mask,int width,int height,Point from,Point to,int radius,bool replace) {
		if(width<=0 || height<=0 || int64_t(width)*height>8*1024*1024 || mask.size()!=size_t(width)*height || radius<1 || radius>32 || from.x<0 || from.y<0 || from.x>=width || from.y>=height || to.x<0 || to.y<0 || to.x>=width || to.y>=height)return false;
		const int dx=std::abs(to.x-from.x),dy=-std::abs(to.y-from.y),sx=from.x<to.x?1:-1,sy=from.y<to.y?1:-1;
		int error=dx+dy;
		for(;;) {
			for(int y=std::max(0,from.y-radius);y<=std::min(height-1,from.y+radius);++y)for(int x=std::max(0,from.x-radius);x<=std::min(width-1,from.x+radius);++x)if((x-from.x)*(x-from.x)+(y-from.y)*(y-from.y)<=radius*radius)mask[size_t(y)*width+x]=replace?1:0;
			if(from==to)break;
			const int twice=2*error;
			if(twice>=dy){error+=dy;from.x+=sx;}
			if(twice<=dx){error+=dx;from.y+=sy;}
		}
		return true;
	}

	bool BuildDecorationEnvironment(const Layer& layer, int width, int height, DecorationEnvironment& result) {
		if(width<=0 || height<=0 || int64_t(width)*height>8*1024*1024 || layer.cells.size()!=size_t(width)*height)return false;
		DecorationEnvironment next;
		const auto count=layer.cells.size();
		next.water.assign(count,33);next.path.assign(count,33);next.forestEdge.assign(count,33);next.slope.assign(count,0);
		for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
			const size_t i=size_t(y)*width+x;const auto& cell=layer.cells[i];
			if(IsWater(cell.terrain))next.water[i]=0;
			if(cell.terrain==Terrain::Road || cell.terrain==Terrain::Bridge || cell.terrain==Terrain::Plaza)next.path[i]=0;
			for(const auto [dx,dy]:{std::pair{1,0},std::pair{-1,0},std::pair{0,1},std::pair{0,-1}}) {
				if(x+dx<0 || y+dy<0 || x+dx>=width || y+dy>=height)continue;
				const auto& other=layer.cells[size_t(y+dy)*width+x+dx];
				if(other.terrain!=Terrain::Void)next.slope[i]=std::max(next.slope[i],uint16_t(std::abs(int(cell.elevation)-int(other.elevation))));
				if((cell.terrain==Terrain::Forest && other.terrain==Terrain::Grass) || (cell.terrain==Terrain::Grass && other.terrain==Terrain::Forest))next.forestEdge[i]=0;
			}
		}
		// Two raster passes compute exact bounded L1 distance without a large queue.
		for(auto* distances:{&next.water,&next.path,&next.forestEdge}) {
			auto& d=*distances;
			for(int y=0;y<height;++y)for(int x=0;x<width;++x){const size_t i=size_t(y)*width+x;if(x)d[i]=std::min(int(d[i]),int(d[i-1])+1);if(y)d[i]=std::min(int(d[i]),int(d[i-width])+1);}
			for(int y=height-1;y>=0;--y)for(int x=width-1;x>=0;--x){const size_t i=size_t(y)*width+x;if(x+1<width)d[i]=std::min(int(d[i]),int(d[i+1])+1);if(y+1<height)d[i]=std::min(int(d[i]),int(d[i+width])+1);}
		}
		result=std::move(next);return true;
	}

	int TransitionDensity(int distance, int width, int edgeDensity) {
		if(width<=0)return 100;
		return std::clamp(edgeDensity,0,100)+(100-std::clamp(edgeDensity,0,100))*std::clamp(distance,0,width)/width;
	}

	bool CanPlaceDecoration(const DecorationGrid& grid, Point anchor, const std::vector<DecorationCell>& footprint, std::string* reason) {
		auto fail = [&](const char* message) {if(reason){*reason=message;
}return false; };
		if (reason) {
			reason->clear();
		}
		if (grid.width <= 0 || grid.height <= 0 || int64_t(grid.width) * grid.height > 8 * 1024 * 1024 || grid.walkable.size() != size_t(grid.width) * grid.height || grid.reserved.size() != grid.walkable.size() || grid.occupied.size() != grid.walkable.size() || grid.habitat.size() != grid.walkable.size()) {
			return fail("Invalid decoration occupancy grid.");
		}
		if (anchor.x < 0 || anchor.y < 0 || anchor.x >= grid.width || anchor.y >= grid.height || footprint.empty() || footprint.size() > 256) {
			return fail("Invalid decoration anchor or footprint size.");
		}
		int minX = 0, maxX = 0, minY = 0, maxY = 0;
		bool blocking = false;
		for (const auto& cell : footprint) {
			if (cell.offset.x < -32 || cell.offset.x > 32 || cell.offset.y < -32 || cell.offset.y > 32) {
				return fail("Decoration offset exceeds supported footprint bounds.");
			}
			minX = std::min(minX, cell.offset.x);
			maxX = std::max(maxX, cell.offset.x);
			minY = std::min(minY, cell.offset.y);
			maxY = std::max(maxY, cell.offset.y);
			blocking = blocking || cell.blocking;
		}
		if (maxX - minX >= 32 || maxY - minY >= 32) {
			return fail("Decoration footprint must fit within 32 by 32 cells.");
		}
		const int left = anchor.x + minX - 1, top = anchor.y + minY - 1, right = anchor.x + maxX + 1, bottom = anchor.y + maxY + 1;
		if (left < 0 || top < 0 || right >= grid.width || bottom >= grid.height) {
			return fail("Decoration clearance extends outside the region.");
		}
		const uint8_t habitat = grid.habitat[size_t(anchor.y) * grid.width + anchor.x];
		if (!habitat) {
			return fail("Anchor has no decoration habitat.");
		}
		for (int y = top; y <= bottom; ++y) {
			for (int x = left; x <= right; ++x) {
				const size_t index = size_t(y) * grid.width + x;
				if (!grid.walkable[index] || grid.reserved[index] || grid.occupied[index] || grid.habitat[index] != habitat) {
					return fail("Decoration overlaps terrain, a route, a reservation or an occupied footprint.");
				}
			}
		}
		if (!blocking) {
			return true;
		}
		// With a clear halo, every free cell in a one/two-cell-wide footprint
		// touches that halo directly. Such a footprint cannot enclose a pocket.
		if (maxX - minX < 2 || maxY - minY < 2) {
			return true;
		}
		const int width = right - left + 1, height = bottom - top + 1;
		// The validated footprint spans at most 32 cells; its halo fits in 34.
		// Reuse bounded stack storage instead of allocating vectors and a deque
		// for each sampled decoration. Only initialize the active rectangle.
		constexpr size_t capacity = 34 * 34;
		const size_t area = size_t(width) * height;
		std::array<uint8_t, capacity> blocked, seen;
		std::fill_n(blocked.begin(), area, uint8_t(0));
		std::fill_n(seen.begin(), area, uint8_t(0));
		for (const auto& cell : footprint) {
			if (cell.blocking) {
				blocked[size_t(anchor.y + cell.offset.y - top) * width + anchor.x + cell.offset.x - left] = 1;
			}
		}
		std::array<int, capacity> open;
		size_t head = 0, tail = 0;
		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				if (x == 0 || y == 0 || x == width - 1 || y == height - 1) {
					const int index = y * width + x;
					open[tail++] = index;
					seen[index] = 1;
				}
			}
		}
		while (head < tail) {
			const int current = open[head++];
			for (const auto [dx, dy] : { std::pair { 1, 0 }, std::pair { -1, 0 }, std::pair { 0, 1 }, std::pair { 0, -1 } }) {
				const int x = current % width + dx, y = current / width + dy;
				if (x < 0 || y < 0 || x >= width || y >= height) {
					continue;
				}
				const int next = y * width + x;
				if (!blocked[next] && !seen[next]) {
					seen[next] = 1;
					open[tail++] = next;
				}
			}
		}
		for (size_t i = 0; i < area; ++i) {
			if (!blocked[i] && !seen[i]) {
				return fail("Decoration would enclose an unreachable walkable pocket.");
			}
		}
		return true;
	}

	void MarkDecoration(DecorationGrid& grid, Point anchor, const std::vector<DecorationCell>& footprint) {
		if (grid.width <= 0 || grid.height <= 0 || grid.occupied.size() != size_t(grid.width) * grid.height || grid.walkable.size() != grid.occupied.size()) {
			return;
		}
		for (const auto& cell : footprint) {
			const int64_t x = int64_t(anchor.x) + cell.offset.x, y = int64_t(anchor.y) + cell.offset.y;
			if (x < 0 || y < 0 || x >= grid.width || y >= grid.height) {
				continue;
			}
			const size_t index = size_t(y) * grid.width + size_t(x);
			grid.occupied[index] = 1;
			if (cell.blocking) {
				grid.walkable[index] = 0;
			}
		}
	}
}
