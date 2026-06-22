//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_PROCEDURAL_RECIPE_H_
#define RME_PROCEDURAL_RECIPE_H_

#include "main.h"
#include "procedural_generator.h"

namespace ProceduralRecipe {
	bool Save(const wxString& path, const GenerationSpec& spec, wxString& error);
	bool Load(const wxString& path, GenerationSpec& spec, wxString& error);
}

#endif
