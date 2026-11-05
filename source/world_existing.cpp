#include "world_existing.h"
#include <algorithm>
#include <array>
#include <numeric>
#include <queue>
#include <set>
#include <tuple>

namespace WorldExisting {
    bool Natural(WorldGen::Terrain t) {
        using T=WorldGen::Terrain;
        return t==T::Grass || t==T::Forest || t==T::Desert || t==T::Beach || t==T::Snow || t==T::CaveFloor;
    }
    bool Road(WorldGen::Terrain t) {return t==WorldGen::Terrain::Road || t==WorldGen::Terrain::Plaza;}

    bool Analyze(const Settings& s,const std::vector<Cell>& cells,Analysis& result,std::string& error,const WorldGen::ProgressCallback& progress) {
        error.clear();
        if(s.width<1 || s.height<1 || int64_t(s.width)*s.height>1024*1024 || cells.size()!=size_t(s.width)*s.height || s.protectionRadius<1 || s.protectionRadius>8 || s.maxGap<1 || s.maxGap>12 || s.maxConnections<0 || s.maxConnections>64) {error="Existing-map scope/options exceed their supported bounds.";return false;}
        auto update=[&](const char* stage,int percent){if(progress && !progress(stage,percent)){error="World generation cancelled.";return false;}return true;};
        Analysis a;a.settings=s;a.cells=cells;const auto count=cells.size();a.eligible.assign(count,0);a.roadComponents.assign(count,-1);
        const std::array<WorldGen::Point,4> directions{{{1,0},{0,1},{-1,0},{0,-1}}};
        auto neighbor=[&](size_t i,WorldGen::Point d)->int {const int x=int(i%s.width)+d.x,y=int(i/s.width)+d.y;return x>=0 && y>=0 && x<s.width && y<s.height?y*s.width+x:-1;};
        for(size_t i=0;i<count;++i) {
            const auto& c=cells[i];
            if(c.exists){if(c.terrain==WorldGen::Terrain::Void)++a.unknown;else ++a.recognized;}
            a.travelAnchors+=c.travel;
            a.structures+=c.structure;a.protectedTiles+=c.exists && !c.editable;
            a.eligible[i]=c.exists && c.editable && c.walkable && Natural(c.terrain);
        }
        // Reserve the selection edge too: an uninspected neighbor cannot be assumed safe.
        for(int y=0;y<s.height;++y)for(int x=0;x<s.width;++x) {
            const auto& c=cells[size_t(y)*s.width+x];
            const bool edge=x==0 || y==0 || x==s.width-1 || y==s.height-1;
            if(!edge && !c.structure && c.editable)continue;
            for(int dy=-s.protectionRadius;dy<=s.protectionRadius;++dy)for(int dx=-s.protectionRadius;dx<=s.protectionRadius;++dx) {
                const int nx=x+dx,ny=y+dy;if(nx>=0 && ny>=0 && nx<s.width && ny<s.height)a.eligible[size_t(ny)*s.width+nx]=0;
            }
        }
        a.naturalTiles=std::count(a.eligible.begin(),a.eligible.end(),uint8_t(1));
        std::vector<size_t> queue;queue.reserve(count);
        for(size_t i=0;i<count;++i)if(cells[i].exists && cells[i].walkable && Road(cells[i].terrain) && a.roadComponents[i]<0) {
            const int component=a.roadComponentCount++;queue.clear();queue.push_back(i);a.roadComponents[i]=component;
            for(size_t head=0;head<queue.size();++head)for(const auto d:directions){const int n=neighbor(queue[head],d);if(n>=0 && cells[n].exists && cells[n].walkable && Road(cells[n].terrain) && a.roadComponents[n]<0){a.roadComponents[n]=component;queue.push_back(n);}}
        }
        if(!update("Recognizing existing roads and structures",25))return false;
        if(s.connectPaths && a.roadComponentCount>1 && s.maxConnections>0) {
            // Multi-source wavefront joins different road components through short,
            // empty natural strips. One flood replaces an endpoint-by-endpoint search.
            std::vector<int> owner=a.roadComponents,parent(count,-1),distance(count,-1);
            queue.clear();
            for(size_t i=0;i<count;++i)if(owner[i]>=0){distance[i]=0;queue.push_back(i);}
            struct Candidate {int first,second,length;};std::vector<Candidate> candidates;
            for(size_t head=0;head<queue.size();++head) {
                if((head&4095)==0 && !update("Finding short gaps between existing paths",40))return false;
                const int i=int(queue[head]);
                for(const auto d:directions) {
                    const int n=neighbor(i,d);if(n<0)continue;
                    if(owner[n]>=0) {
                        if(owner[n]!=owner[i] && distance[i]+distance[n]>=1 && distance[i]+distance[n]<=s.maxGap && candidates.size()<count*2)candidates.push_back({i,n,distance[i]+distance[n]});
                        continue;
                    }
                    if(!a.eligible[n] || !cells[n].bare || distance[i]>=s.maxGap)continue;
                    owner[n]=owner[i];parent[n]=i;distance[n]=distance[i]+1;queue.push_back(n);
                }
            }
            std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return std::tie(a.length,a.first,a.second)<std::tie(b.length,b.first,b.second);});
            std::vector<int> roots(a.roadComponentCount);std::iota(roots.begin(),roots.end(),0);
            auto root=[&](int value){while(roots[value]!=value){roots[value]=roots[roots[value]];value=roots[value];}return value;};
            for(const auto& candidate:candidates) {
                const int first=root(owner[candidate.first]),second=root(owner[candidate.second]);if(first==second)continue;
                Connection connection;std::set<int> positions;
                for(int n:{candidate.first,candidate.second})for(int i=n;i>=0 && distance[i]>0;i=parent[i])positions.insert(i);
                if(positions.empty() || positions.size()>size_t(s.maxGap))continue;
                for(int i:positions)connection.cells.push_back({i%s.width,i/s.width});
                a.connections.push_back(std::move(connection));roots[first]=second;
                if(a.connections.size()>=size_t(s.maxConnections))break;
            }
        }
        if(!update("Existing-map analysis complete",50))return false;
        result=std::move(a);return true;
    }
}
