//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "rendering/utilities/frame_pacer.h"

FramePacer::FramePacer() {
}

FramePacer::~FramePacer() {
}

void FramePacer::SetStatusCallback(std::function<void(const wxString&)> callback) {
	status_callback = std::move(callback);
}

void FramePacer::SetLastFrameMs(double ms) {
	fps_counter.SetLastFrameMs(ms);
}

void FramePacer::UpdateAndLimit(int limit, bool show_counter) {
	fps_counter.LimitFPS(limit);
	fps_counter.Update();

	if (show_counter && fps_counter.HasChanged() && status_callback) {
		status_callback(fps_counter.GetStatusString());
	}
}
