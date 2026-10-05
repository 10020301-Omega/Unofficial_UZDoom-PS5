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

//==========================================================================
//
// Where the user's folders are. A title sees only its own folder unless the
// console's setup opens /data to it, so the shared folder is used only when
// a file can really be written there and read back.
//
//==========================================================================

static std::string UserRoot = PS5_APP_ROOT;
static std::string UserRootShown = "/data/homebrew/" PS5_TITLE_ID;

static bool CanUse(const std::string &folder)
{
	mkdir(folder.c_str(), 0777);
	const std::string probe = folder + "/.probe";
	FILE *file = fopen(probe.c_str(), "w");
	if (file == nullptr)
		return false;
	const bool written = fputs("uzdoom", file) >= 0;
	const bool closed = fclose(file) == 0;
	char text[8] = {};
	file = fopen(probe.c_str(), "r");
	const bool read = file != nullptr && fgets(text, sizeof(text), file) != nullptr && strcmp(text, "uzdoom") == 0;
	if (file != nullptr)
		fclose(file);
	remove(probe.c_str());
	return written && closed && read;
}

void PS5_ChooseUserRoot()
{
	if (CanUse(PS5_SHARED_ROOT))
	{
		UserRoot = UserRootShown = PS5_SHARED_ROOT;
	}
	say("user folder: %s (over FTP: %s)", UserRoot.c_str(), UserRootShown.c_str());
}

const char *PS5_UserRoot()
{
	return UserRoot.c_str();
}

const char *PS5_UserRootShown()
{
	return UserRootShown.c_str();
}

static void SetUpFolders()
{
	PS5_ChooseUserRoot();
	static const char *const folders[] = { "/iwads", "/mods", "/saves", "/config", "/cache", "/data", "/screenshots" };
	for (const char *folder : folders)
	{
		const std::string path = UserRoot + folder;
		mkdir(path.c_str(), 0777);
		// Whatever an older build or an FTP client left
		OpenToEveryone(path, 5);
	}

	// The engine finds its config, cache and data folders through these
	// (posix/unix/i_specialpaths.cpp); a title has no environment of its own.
	setenv("HOME", UserRoot.c_str(), 1);
	setenv("XDG_CONFIG_HOME", (UserRoot + "/config").c_str(), 1);
	setenv("XDG_CACHE_HOME", (UserRoot + "/cache").c_str(), 1);
	setenv("XDG_DATA_HOME", (UserRoot + "/data").c_str(), 1);
	setenv("XDG_PICTURES_DIR", (UserRoot + "/screenshots").c_str(), 1);
}

// The folders the port makes, open to the FTP server again after a run
static void OpenUserFolders()
{
	static const char *const folders[] = { "/saves", "/config", "/cache", "/data", "/screenshots" };
	for (const char *folder : folders)
		OpenToEveryone(UserRoot + folder, 5);
	chmod((UserRoot + "/uzdoom.log").c_str(), 0666);
}

//==========================================================================
//
// exit() kills a title in a way the console reports as a crash; the only
// correct ending is to ask the shell to close it, which the start-up code
// does when main returns. The engine itself always returns, but a library
// may call exit(): the link sends those calls here (--wrap=exit in
// ps5/tools/link-title.sh).
//
//==========================================================================

extern "C" void catchReturnFromMain(int status);

extern "C" [[noreturn]] void __wrap_exit(int status)
{
	fprintf(stderr, "exit(%d) called: closing through the shell\n", status);
	OpenUserFolders();
	catchReturnFromMain(status);
	for (;;)
		sleep(1);
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

	OpenUserFolders();
	PS5_ReleaseSurface();
	// Returning is how a title ends: the start-up code asks the shell to
	// close it. Calling exit() here would be reported as a crash.
	return result;
}
