/*
** ps5_main.cpp
**
** The title's entry point on the PS5: set the console up, show the
** launcher, then run the engine with what was chosen there.
**
**---------------------------------------------------------------------------
**
** Copyright 2026 PS5-UZDOOM port contributors
** Based on posix/sdl/i_main.cpp:
** Copyright 1998-2016 Marisa Heit
** Copyright 2005-2016 Christoph Oelckers
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
*/

#include <dirent.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "c_console.h"
#include "cmdlib.h"
#include "engineerrors.h"
#include "i_interface.h"
#include "i_system.h"
#include "m_argv.h"
#include "printf.h"
#include "ps5_display.h"
#include "ps5_launcher.h"
#include "ps5_paths.h"
#include "version.h"
#include "zstring.h"

extern "C"
{
#include "console/platform.h"
}

int GameMain();
void I_StartupJoysticks();
void PS5_OpenLog();

extern const char * const BACKEND = "PS5";

FString sys_ostype;

// The command line arguments.
FArgs *Args;

FString I_DetectOS()
{
	sys_ostype = "PlayStation 5";
	return sys_ostype;
}

//==========================================================================
//
// Everything below the user's folder open to every process: the FTP
// server people fetch logs and copy saves with is another one, and what
// the engine creates gets whatever mode it asked for. Done at start (for
// what an older build or an FTP client left) and again at exit (for what
// this run made).
//
//==========================================================================

static void OpenToEveryone(const std::string &folder, int depth)
{
	chmod(folder.c_str(), 0777);
	DIR *dir = opendir(folder.c_str());
	if (dir == nullptr)
		return;
	while (struct dirent *entry = readdir(dir))
	{
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;
		const std::string path = folder + "/" + entry->d_name;
		struct stat info;
		if (stat(path.c_str(), &info) != 0)
			continue;
		if (S_ISDIR(info.st_mode))
		{
			if (depth > 0)
				OpenToEveryone(path, depth - 1);
		}
		else if ((info.st_mode & 0666) != 0666)
		{
			chmod(path.c_str(), (info.st_mode & 07777) | 0666);
		}
	}
	closedir(dir);
}

static void SetUpFolders()
{
	umask(0);
	static const char *const folders[] = { "", "/iwads", "/mods", "/saves", "/config", "/cache", "/data" };
	for (const char *folder : folders)
	{
		const std::string path = std::string(PS5_USER_ROOT) + folder;
		mkdir(path.c_str(), 0777);
	}
	OpenToEveryone(PS5_USER_ROOT, 6);

	// The engine finds its config, cache and data folders through these
	// (posix/unix/i_specialpaths.cpp); a title has no environment of its own.
	setenv("HOME", PS5_USER_ROOT, 1);
	setenv("XDG_CONFIG_HOME", PS5_USER_ROOT "/config", 1);
	setenv("XDG_CACHE_HOME", PS5_USER_ROOT "/cache", 1);
	setenv("XDG_DATA_HOME", PS5_USER_ROOT "/data", 1);
	setenv("XDG_PICTURES_DIR", PS5_USER_ROOT "/screenshots", 1);
}

int main(int, char **)
{
	platform_init(GAMENAME);
	SetUpFolders();
	PS5_OpenLog();

	// Set LC_NUMERIC environment variable in case some library decides to
	// clear the setlocale call at least this will be correct.
	setenv("LC_NUMERIC", "C", 1);
	setlocale(LC_ALL, "C");

	// The pads first: the launcher reads them.
	I_StartupJoysticks();

	std::vector<std::string> arguments = { PS5_APP_ROOT "/eboot.bin" };
	if (!PS5_RunLauncher(arguments))
	{
		PS5_ReleaseSurface();
		return 0;
	}

	std::vector<char *> argv;
	for (std::string &argument : arguments)
		argv.push_back(argument.data());
	argv.push_back(nullptr);
	fprintf(stderr, "launching:");
	for (const std::string &argument : arguments)
		fprintf(stderr, " %s", argument.c_str());
	fprintf(stderr, "\n");

	Args = new FArgs((int)arguments.size(), argv.data());
	progdir = PS5_APP_ROOT "/";

	const int result = GameMain();

	OpenToEveryone(PS5_USER_ROOT, 6);
	PS5_ReleaseSurface();
	// Returning is how a title ends: the start-up code asks the shell to
	// close it. Calling exit() here would be reported as a crash.
	return result;
}
