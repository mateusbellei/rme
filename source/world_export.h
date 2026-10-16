#ifndef RME_WORLD_EXPORT_H_
#define RME_WORLD_EXPORT_H_
#include "world_plan.h"
namespace WorldGen {
	// UTF-8 output prefix; writes terrain PPMs and a versioned JSON diagnostic manifest.
	bool ExportDiagnostics(const Plan& plan, const std::string& prefix, std::string& error);
}
#endif
