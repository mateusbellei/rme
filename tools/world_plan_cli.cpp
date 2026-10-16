#include "world_plan.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <string>

// Client-independent diagnostic: rme_world_plan [output prefix] [seed] [size].
// Outputs PPM masks per floor and a JSON summary, not a playable OTBM.
int main(int argc,char** argv){
	try{
		const std::string prefix=argc>1?argv[1]:"world";
		WorldGen::Settings settings;
		settings.seed=argc>2?uint32_t(std::stoul(argv[2])):1337;
		settings.width=settings.height=argc>3?std::stoi(argv[3]):256;
		settings.caveDepth=2;settings.buildingLevels=2;
		WorldGen::Plan plan;std::string error;
		const auto start=std::chrono::steady_clock::now();
		if(!WorldGen::Build(settings,plan,error)){std::cerr<<error<<'\n';return 1;}
		const auto report=WorldGen::Validate(plan);
		for(const auto& layer:plan.layers){
			std::ofstream output(prefix+"_z"+std::to_string(layer.offsetZ)+".ppm",std::ios::binary);
			if(!output){std::cerr<<"Cannot write preview\n";return 1;}
			output<<"P6\n"<<settings.width<<' '<<settings.height<<"\n255\n";
			for(const auto& cell:layer.cells){
				uint32_t color=WorldGen::TerrainColor(cell.terrain);
				if(cell.flags&WorldGen::Wall)color=0x404040;
				if(cell.flags&WorldGen::Entrance)color=0xFF40FF;
				if(cell.flags&WorldGen::Connection)color=0xFFFF00;
				char rgb[]={char(color>>16),char(color>>8),char(color)};output.write(rgb,3);
			}
		}
		const auto milliseconds=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
		std::ofstream summary(prefix+".json");
		summary<<"{\n  \"seed\": "<<settings.seed<<",\n  \"size\": "<<settings.width<<",\n  \"fingerprint\": \""<<WorldGen::Fingerprint(plan)<<"\",\n  \"valid\": "<<(report.valid?"true":"false")<<",\n  \"settlements\": "<<plan.settlements.size()<<",\n  \"buildings\": "<<plan.buildings.size()<<",\n  \"verticalLinks\": "<<plan.links.size()<<",\n  \"landTiles\": "<<report.landTiles<<",\n  \"waterTiles\": "<<report.waterTiles<<",\n  \"milliseconds\": "<<milliseconds<<"\n}\n";
		std::cout<<"seed="<<settings.seed<<" size="<<settings.width<<" settlements="<<plan.settlements.size()<<" buildings="<<plan.buildings.size()<<" links="<<plan.links.size()<<" valid="<<report.valid<<" ms="<<milliseconds<<'\n';
		for(const auto& warning:plan.warnings)std::cout<<"Warning: "<<warning<<'\n';
		for(const auto& e:report.errors)std::cerr<<e<<'\n';
		return report.valid?0:1;
	}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
