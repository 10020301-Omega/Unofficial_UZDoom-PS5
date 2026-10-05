/*
** ps5_system.cpp
**
** The system services of the PS5 backend: console output, fatal errors,
** the clipboard and the odds and ends the engine asks its platform for.
**
**---------------------------------------------------------------------------
**
** Copyright 2026 PS5-UZDOOM port contributors
** Based on posix/sdl/i_system.cpp:
** Copyright 1993-1996 id Software
** Copyright 1999-2016 Marisa Heit
** Copyright 2005-2016 Christoph Oelckers
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
*/

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "c_cvars.h"
#include "i_interface.h"
#include "i_system.h"
#include "printf.h"
#include "ps5_paths.h"
#include "st_start.h"
#include "v_font.h"
#include "vm.h"

double PerfToSec, PerfToMillisec;
bool PerfAvailable;

// Named by menus and the config file on every platform.
CVAR(String, queryiwad_key, "shift", CVAR_GLOBALCONFIG | CVAR_ARCHIVE);

static FILE *LogFile;
static FString Clipboard;

void I_SetIWADInfo()
{
}

extern "C" int I_FileAvailable(const char *)
{
	// Asks whether a program is installed; there are none to run.
	return 0;
}

//==========================================================================
//
// Console output: klog (through standard error) and a log file in the
// user's folder, flushed line by line so a crash keeps what came before.
//
//==========================================================================

void PS5_OpenLog()
{
	if (LogFile != nullptr)
		return;
	const FString path = FString(PS5_USER_ROOT) + "/uzdoom.log";
	LogFile = fopen(path.GetChars(), "w");
	if (LogFile != nullptr)
		chmod(path.GetChars(), 0666);
}

void I_PrintStr(const char *cp)
{
	// Strip the engine's colour escapes.
	FString text;
	const char *srcp = cp;
	while (*srcp != 0)
	{
		if (*srcp == 0x1c)
		{
			srcp += 1;
			const uint8_t *scratch = (const uint8_t *)srcp;
			V_ParseFontColor(scratch, CR_UNTRANSLATED, CR_YELLOW);
			srcp = (const char *)scratch;
		}
		else if (*srcp != 0x1d && *srcp != 0x1e && *srcp != 0x1f)
		{
			text += *srcp++;
		}
		else
		{
			if (srcp[1] != 0) srcp += 2;
			else break;
		}
	}

	fputs(text.GetChars(), stderr);
	fflush(stderr);
	if (LogFile != nullptr)
	{
		fputs(text.GetChars(), LogFile);
		fflush(LogFile);
	}
}

void RedrawProgressBar(int, int)
{
}

void CleanProgressBar()
{
}

//==========================================================================
//
// Fatal errors. There is nothing to show a dialog with, so the message
// goes to klog, the log and a file of its own that the launcher shows on
// the next start.
//
//==========================================================================

void I_ShowFatalError(const char *message)
{
	if (CVMAbortException::stacktrace.IsNotEmpty())
	{
		Printf("%s", CVMAbortException::stacktrace.GetChars());
	}
	fprintf(stderr, "\nFATAL: %s\n", message);
	fflush(stderr);
	if (LogFile != nullptr)
	{
		fprintf(LogFile, "\nFATAL: %s\n", message);
		fflush(LogFile);
	}

	const FString path = FString(PS5_USER_ROOT) + "/last-error.txt";
	if (FILE *file = fopen(path.GetChars(), "w"))
	{
		fputs(message, file);
		fputc('\n', file);
		fclose(file);
		chmod(path.GetChars(), 0666);
	}
}

void CalculateCPUSpeed()
{
	PerfAvailable = false;
	PerfToMillisec = PerfToSec = 0.;
}

bool HoldingQueryKey(const char *)
{
	return false;
}

//==========================================================================
//
// The IWAD is chosen in the launcher, which passes -iwad. If the engine
// still asks (a launch without the launcher's choice), take its default.
//
//==========================================================================

bool I_PickIWad(bool, FStartupSelectionInfo &)
{
	return true;
}

void I_PutInClipboard(const char *str)
{
	Clipboard = str;
}

FString I_GetFromClipboard(bool)
{
	return Clipboard;
}

FString I_GetCWD()
{
	// getcwd is not provided on the console; paths are built explicitly.
	return PS5_APP_ROOT;
}

bool I_ChDir(const char *)
{
	return false;
}

unsigned int I_MakeRNGSeed()
{
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	return (unsigned int)now.tv_sec ^ (unsigned int)now.tv_nsec;
}

void I_OpenShellFolder(const char *)
{
}

class FGameTexture;

bool I_SetCursor(FGameTexture *)
{
	// No pointer on the console.
	return false;
}
