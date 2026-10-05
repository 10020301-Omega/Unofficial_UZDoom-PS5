/*
** gl_sysfb.h (PS5)
**
** The frame buffer base the Vulkan renderer derives from. The name is the
** one the renderer includes on every platform; there is no OpenGL here.
**
**---------------------------------------------------------------------------
**
** Copyright 2026 PS5-UZDOOM port contributors
** Based on posix/sdl/gl_sysfb.h:
** Copyright 2013-2016 Christoph Oelckers
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
*/

#ifndef __POSIX_PS5_GL_SYSFB_H__
#define __POSIX_PS5_GL_SYSFB_H__

#include "v_video.h"

class SystemBaseFrameBuffer : public DFrameBuffer
{
	typedef DFrameBuffer Super;

public:
	// this must have the same parameters as the Windows version, even if they are not used!
	SystemBaseFrameBuffer (void *hMonitor, bool fullscreen);

	bool IsFullscreen() override;

	int GetClientWidth() override;
	int GetClientHeight() override;

	void ToggleFullscreen(bool yes) override;
	void SetWindowSize(int client_w, int client_h) override;

protected:
	SystemBaseFrameBuffer () {}
};

#endif // __POSIX_PS5_GL_SYSFB_H__
