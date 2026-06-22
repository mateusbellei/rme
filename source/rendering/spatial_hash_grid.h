#ifndef RME_RENDERING_SPATIAL_HASH_GRID_H_
#define RME_RENDERING_SPATIAL_HASH_GRID_H_

// Viewport-aligned quadtree node iterator adapted from RME-Redux SpatialHashGrid.
// Works with the existing QTreeNode layout (4x4 tiles per node) without replacing Map storage.

class SpatialHashGrid {
public:
	static constexpr int NODE_SHIFT = 2;
	static constexpr int NODE_SIZE = 1 << NODE_SHIFT;

	struct NodeBounds {
		int start_x = 0;
		int start_y = 0;
		int end_x = 0;
		int end_y = 0;
	};

	static NodeBounds computeNodeBounds(int tile_start_x, int tile_start_y, int tile_end_x, int tile_end_y) {
		NodeBounds bounds;
		bounds.start_x = tile_start_x & ~(NODE_SIZE - 1);
		bounds.start_y = tile_start_y & ~(NODE_SIZE - 1);
		bounds.end_x = (tile_end_x & ~(NODE_SIZE - 1)) + NODE_SIZE;
		bounds.end_y = (tile_end_y & ~(NODE_SIZE - 1)) + NODE_SIZE;
		return bounds;
	}

	template <typename Func>
	static void visitNodesInBounds(const NodeBounds& bounds, Func&& func) {
		if (bounds.end_x < bounds.start_x || bounds.end_y < bounds.start_y) {
			return;
		}
		for (int nd_x = bounds.start_x; nd_x <= bounds.end_x; nd_x += NODE_SIZE) {
			for (int nd_y = bounds.start_y; nd_y <= bounds.end_y; nd_y += NODE_SIZE) {
				func(nd_x, nd_y);
			}
		}
	}
};

#endif
