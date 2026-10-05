/*
** ps5_glstubs.cpp
**
** The few things the shared renderer code takes from the OpenGL backend,
** which is not built for the console. The console variables keep their
** names and defaults (common/rendering/gl/gl_renderbuffers.cpp and
** gl_postprocess.cpp), because menus and the config file name them and
** the Vulkan renderer reads them.
**
** Copyright 2026 PS5-UZDOOM port contributors
** SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "c_cvars.h"
#include "zstring.h"

CVAR(Int, gl_multisample, 1, CVAR_ARCHIVE|CVAR_GLOBALCONFIG);
CVAR(Int, gl_dither_bpc, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)

// "stat vram": OpenGL vendor extensions that report video memory
void PrintVRAM_NV(FString &)
{
}

void PrintVRAM_ATI(FString &)
{
}
