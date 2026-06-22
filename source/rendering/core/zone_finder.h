#ifndef RME_RENDERING_CORE_ZONE_FINDER_H_
#define RME_RENDERING_CORE_ZONE_FINDER_H_

#include <cmath>
#include <limits>
#include <unordered_set>
#include <vector>

struct FinderPosition {
	FinderPosition() = default;
	FinderPosition(int _x, int _y, int _z) :
		x(_x), y(_y), z(_z) { }

	int x = 0;
	int y = 0;
	int z = 0;

	bool operator==(const FinderPosition& other) const {
		return x == other.x && y == other.y && z == other.z;
	}

	double distance(const FinderPosition& b) const {
		return std::sqrt(std::pow(x - b.x, 2) + std::pow(y - b.y, 2));
	}

	struct Hash {
		size_t operator()(const FinderPosition& p) const {
			return static_cast<size_t>(p.x ^ p.y ^ p.z);
		}
	};
};

class ZoneFinder {
public:
	explicit ZoneFinder(const std::vector<FinderPosition>& input_positions) :
		positions(input_positions.begin(), input_positions.end()) { }

	std::vector<std::vector<FinderPosition>> findZones() {
		zones.clear();
		visited.clear();

		for (const auto& pos : positions) {
			if (visited.find(pos) == visited.end()) {
				std::vector<FinderPosition> zone;
				dfs(pos, zone);
				zones.push_back(zone);
			}
		}

		return zones;
	}

	FinderPosition findClosestToCenter(const std::vector<FinderPosition>& zone) const {
		FinderPosition centroid { 0, 0, 0 };
		for (const auto& pos : zone) {
			centroid.x += pos.x;
			centroid.y += pos.y;
			centroid.z += pos.z;
		}

		centroid.x /= static_cast<int>(zone.size());
		centroid.y /= static_cast<int>(zone.size());
		centroid.z /= static_cast<int>(zone.size());

		double min_distance = std::numeric_limits<double>::max();
		FinderPosition closest_position;
		for (const auto& pos : zone) {
			const double dist = pos.distance(centroid);
			if (dist < min_distance) {
				min_distance = dist;
				closest_position = pos;
			}
		}

		return closest_position;
	}

private:
	bool isValid(const FinderPosition& pos) const {
		return positions.find(pos) != positions.end() && visited.find(pos) == visited.end();
	}

	void dfs(const FinderPosition& pos, std::vector<FinderPosition>& zone) {
		if (visited.find(pos) != visited.end()) {
			return;
		}

		visited.insert(pos);
		zone.push_back(pos);

		const std::vector<FinderPosition> neighbors = {
			{ pos.x + 1, pos.y, pos.z },
			{ pos.x - 1, pos.y, pos.z },
			{ pos.x, pos.y + 1, pos.z },
			{ pos.x, pos.y - 1, pos.z }
		};

		for (const auto& next : neighbors) {
			if (isValid(next)) {
				dfs(next, zone);
			}
		}
	}

	std::unordered_set<FinderPosition, FinderPosition::Hash> positions;
	std::vector<std::vector<FinderPosition>> zones;
	std::unordered_set<FinderPosition, FinderPosition::Hash> visited;
};

#endif
