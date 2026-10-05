/*
** hardware.cpp (PS5: posix/sdl/hardware.cpp without SDL)
**
** Somewhat OS-independent interface to the screen, mouse, keyboard, and stick
**
**---------------------------------------------------------------------------
**
** Copyright 1998-2016 Marisa Heit
** Copyright 2008-2016 Christoph Oelckers
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Code written prior to 2026 is also licensed under:
**
** SPDX-License-Identifier: BSD-3-Clause
**
**---------------------------------------------------------------------------
**
*/

#include <signal.h>

#include "c_console.h"
#include "c_dispatch.h"
#include "hardware.h"
#include "i_system.h"
#include "m_argv.h"
#include "printf.h"
#include "v_text.h"

IVideo *Video;

void I_RestartRenderer();

void I_ShutdownGraphics ()
{
	if (screen)
	{
		DFrameBuffer *s = screen;
		screen = NULL;
		delete s;
	}
	if (Video)
		delete Video, Video = NULL;
}

void I_InitGraphics ()
{
	extern IVideo *gl_CreateVideo();
	Video = gl_CreateVideo();

	if (Video == NULL)
		I_FatalError ("Failed to initialize display");
}
