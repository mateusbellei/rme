#include "world_existing.h"
#include <iostream>
#include <stdexcept>
#include <algorithm>
using namespace WorldExisting;
static void Check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
int main(){try {
    Settings s;s.width=s.height=32;s.protectionRadius=1;s.maxGap=4;
    std::vector<Cell> cells(1024,{WorldGen::Terrain::Grass,true,true,true,false});
    for(int x=4;x<=10;++x)cells[16*32+x].terrain=WorldGen::Terrain::Road;
    for(int x=15;x<=22;++x)cells[16*32+x].terrain=WorldGen::Terrain::Road;
    Analysis a,replay;std::string error;
    Check(Analyze(s,cells,a,error),"Analyze an authored map without a procedural plan");
    Check(a.roadComponentCount==2 && a.connections.size()==1 && a.connections[0].cells.size()==4,"Repair exactly the bounded gap between independent roads");
    for(const auto p:a.connections[0].cells)Check(p.y==16 && p.x>=11 && p.x<=14 && a.eligible[p.y*32+p.x],"Only known empty natural cells may become path");
    Check(Analyze(s,cells,replay,error) && replay.connections[0].cells==a.connections[0].cells,"Analysis has stable tie-breaking");
    s.maxGap=3;Check(Analyze(s,cells,a,error) && a.connections.empty(),"Longer gaps are left for author review");s.maxGap=4;
    auto blocked=cells;for(int y=0;y<32;++y)blocked[y*32+12]={WorldGen::Terrain::Ocean,true,false,false,false};
    Check(Analyze(s,blocked,a,error) && a.connections.empty(),"Never bridge water or cut an unknown obstacle");
    blocked=cells;blocked[16*32+12].editable=false;blocked[16*32+12].structure=true;
    Check(Analyze(s,blocked,a,error) && a.connections.empty() && !a.eligible[15*32+12],"Protect authored objects and their halo");
    blocked=cells;blocked[16*32+12]={WorldGen::Terrain::Void,true,true,false,false};
    Check(Analyze(s,blocked,a,error) && a.unknown==1 && a.connections.empty(),"Unknown material is not empty land");
    blocked=cells;blocked[16*32+12].bare=false;
    Check(Analyze(s,blocked,a,error) && a.connections.empty() && a.eligible[16*32+12],"An existing border may accept nature but not replacement road ground");
    auto network=std::vector<Cell>(1024,{WorldGen::Terrain::Grass,true,true,true,false});
    for(int start:{4,12,20})for(int x=start;x<start+5;++x)network[16*32+x].terrain=WorldGen::Terrain::Road;
    s.maxGap=3;s.maxConnections=1;Check(Analyze(s,network,a,error) && a.connections.size()==1,"Connection budget caps a fragmented network");
    s.maxConnections=2;Check(Analyze(s,network,a,error) && a.connections.size()==2,"Join three components without redundant cycles");s.maxGap=4;
    s.connectPaths=false;Check(Analyze(s,cells,a,error) && a.connections.empty() && a.naturalTiles>0,"Nature-only analysis retains structural information");s.connectPaths=true;
    s.maxConnections=0;Check(Analyze(s,cells,a,error) && a.connections.empty(),"Zero connection budget preserves routes");s.maxConnections=16;
    a.naturalTiles=12345;
    Check(!Analyze(s,cells,a,error,[](const char*,int){return false;}) && a.naturalTiles==12345,"Cancellation does not publish partial analysis");
    s.width=0;Check(!Analyze(s,cells,a,error) && a.naturalTiles==12345,"Invalid scope is rejected atomically");
    std::cout<<"PASS: existing terrain, bounded road repairs, protection halos, water/unknown refusal, replay and cancellation\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
