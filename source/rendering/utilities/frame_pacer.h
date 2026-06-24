//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_RENDERING_UTILITIES_FRAME_PACER_H_
#define RME_RENDERING_UTILITIES_FRAME_PACER_H_

#include "rendering/utilities/fps_counter.h"
#include <wx/string.h>
#include <functional>

class FramePacer {
public:
	FramePacer();
	~FramePacer();

	void UpdateAndLimit(int limit, bool show_counter);
	void SetStatusCallback(std::function<void(const wxString&)> callback);
	void SetLastFrameMs(double ms);

private:
	FPSCounter fps_counter;
	std::function<void(const wxString&)> status_callback;
};

#endif
