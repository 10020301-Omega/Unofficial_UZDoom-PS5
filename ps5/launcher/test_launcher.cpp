/*
** test_launcher.cpp
**
** The launcher on the PC, without a console: builds a folder of files,
** presses buttons from a script, checks the command line that comes out and
** saves each screen as a picture (PPM) and as text.
**
**   test_launcher <work folder>
**
** Copyright 2026 PS5-UZDOOM port contributors
** SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "launcher.h"

#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

using namespace dosui;

static int failures;
static std::string work;

static void Check(bool ok, const char *what)
{
	printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		failures++;
}

static void Touch(const std::string &path, size_t size)
{
	std::ofstream file(path, std::ios::binary);
	file << std::string(size, 'x');
}

static void Snapshot(const Launcher &launcher, const std::string &name)
{
	TextScreen screen;
	launcher.Draw(screen);
	std::vector<uint8_t> rgba(TextScreen::PixelWidth * TextScreen::PixelHeight * 4);
	screen.Rasterize(rgba.data());
	std::ofstream ppm(work + "/" + name + ".ppm", std::ios::binary);
	ppm << "P6\n" << TextScreen::PixelWidth << " " << TextScreen::PixelHeight << "\n255\n";
	for (size_t i = 0; i < rgba.size(); i += 4)
		ppm.write((const char *)&rgba[i], 3);
	std::ofstream text(work + "/" + name + ".txt");
	text << screen.Dump();
}

// One letter a press: u d l r, a(ccept), b(ack), s(quare), t(riangle), o(ptions)
static Launcher::Result Press(Launcher &launcher, const char *keys)
{
	Launcher::Result result = Launcher::Result::Running;
	for (const char *key = keys; *key; key++)
	{
		Input input;
		switch (*key)
		{
		case 'u': input.up = true; break;
		case 'd': input.down = true; break;
		case 'l': input.left = true; break;
		case 'r': input.right = true; break;
		case 'a': input.accept = true; break;
		case 'b': input.back = true; break;
		case 's': input.square = true; break;
		case 't': input.triangle = true; break;
		case 'o': input.start = true; break;
		}
		result = launcher.Update(input);
	}
	return result;
}

static std::string Join(const std::vector<std::string> &args)
{
	std::string out;
	for (const std::string &arg : args)
		out += (out.empty() ? "" : " ") + arg;
	return out;
}

int main(int argc, char **argv)
{
	if (argc < 2)
	{
		fprintf(stderr, "usage: test_launcher <work folder>\n");
		return 2;
	}
	work = argv[1];
	const std::string user = work + "/user", app = work + "/app";
	mkdir(work.c_str(), 0777);
	mkdir(app.c_str(), 0777);

	// An empty console: the folders get made, and there is nothing to start.
	{
		Launcher launcher(user, app);
		launcher.Rescan();
		struct stat info;
		Check(stat((user + "/iwads").c_str(), &info) == 0 && stat((user + "/mods").c_str(), &info) == 0,
			"the user's folders are made");
		Check(launcher.Games().empty(), "no games on an empty console");
		Snapshot(launcher, "01-empty-main");
		Check(Press(launcher, "a") == Launcher::Result::Running, "Start Game with no game does not launch");
		Snapshot(launcher, "02-no-game-message");
		Press(launcher, "a");
		Press(launcher, "da");
		Snapshot(launcher, "03-empty-games");
	}

	Touch(user + "/iwads/DOOM2.WAD", 14604584);
	Touch(user + "/iwads/doom.wad", 12408292);
	Touch(user + "/iwads/HERETIC.WAD", 14189976);
	Touch(user + "/iwads/notes.txt", 10);
	mkdir((user + "/mods/brutal").c_str(), 0777);
	Touch(user + "/mods/brutal/brutalv22.pk3", 60 * 1024 * 1024);
	Touch(user + "/mods/brutal/hud-addon.pk3", 300000);
	Touch(user + "/mods/Sunlust.wad", 30 * 1024 * 1024);
	Touch(user + "/mods/a-very-long-file-name-that-does-not-fit-in-the-column-of-the-list-at-all-really.pk3", 5000);
	Touch(user + "/mods/readme.md", 10);
	for (int i = 0; i < 30; i++)
		Touch(user + "/mods/map" + std::to_string(100 + i) + ".wad", 1000 * (i + 1));

	{
		Launcher launcher(user, app);
		launcher.Rescan();
		Check(launcher.Games().size() == 3, "three games found, the text file ignored");
		Check(launcher.Mods().size() == 34, "34 mods found, subfolder included, the readme ignored");
		Check(BaseName(launcher.Current().game) == "doom.wad", "the first game is chosen by default");
		Snapshot(launcher, "04-main");

		// Select Game: down to DOOM2.WAD
		Press(launcher, "da");
		Snapshot(launcher, "05-games");
		Press(launcher, "da");
		Check(BaseName(launcher.Current().game) == "DOOM2.WAD", "DOOM2.WAD chosen");

		// Select Mods: the list is sorted without regard to case:
		// a-very-long..., brutal/brutalv22.pk3, brutal/hud-addon.pk3, map100..129, Sunlust.wad
		Press(launcher, "da");
		Press(launcher, "d" "a" "d" "a");   // brutalv22 = 1, hud-addon = 2
		Press(launcher, "uuu");             // up past the first wraps to the last, Sunlust.wad
		Snapshot(launcher, "06-mods-scrolled");
		Press(launcher, "ddd");             // wrap to the first, then down to hud-addon
		Check(launcher.Current().mods.size() == 2, "two mods ticked");
		Press(launcher, "l");               // hud-addon moves before brutalv22
		Check(BaseName(launcher.Current().mods[0]) == "hud-addon.pk3", "Left moves a mod earlier in the load order");
		Press(launcher, "r");
		Check(BaseName(launcher.Current().mods[0]) == "brutalv22.pk3", "Right moves it later again");
		Snapshot(launcher, "07-mods");
		Press(launcher, "b");

		// Start Options: a map, MAP07, Ultra-Violence, fast monsters
		Press(launcher, "da");
		Snapshot(launcher, "08-options-title");
		Press(launcher, "r");               // start at a map
		Press(launcher, "dd" "rrrrrr");     // map 7
		Press(launcher, "d" "r");           // skill 4
		Press(launcher, "d" "r");           // fast
		Snapshot(launcher, "09-options-warp");
		Press(launcher, "b");
		Snapshot(launcher, "10-main-ready");

		const std::string line = Join(launcher.BuildArguments());
		const std::string expected =
			"-iwad " + user + "/iwads/DOOM2.WAD"
			" -file " + user + "/mods/brutal/brutalv22.pk3 " + user + "/mods/brutal/hud-addon.pk3"
			" -warp 7 -skill 4 -fast"
			" -config " + user + "/config/uzdoom.ini -savedir " + user + "/saves";
		Check(line == expected, "the command line");
		if (line != expected)
			printf("      got:      %s\n      expected: %s\n", line.c_str(), expected.c_str());

		// Presets: save into the second, then launch
		Press(launcher, "da");
		Press(launcher, "d" "s");
		Snapshot(launcher, "11-presets");
		Press(launcher, "b");
		Press(launcher, "uuuu");
		Check(Press(launcher, "a") == Launcher::Result::Launch, "Start Game launches");
	}

	// A new start reads the selection back; a mod deleted meanwhile is dropped.
	remove((user + "/mods/brutal/hud-addon.pk3").c_str());
	{
		Launcher launcher(user, app);
		launcher.Load();
		launcher.Rescan();
		Check(BaseName(launcher.Current().game) == "DOOM2.WAD", "the game is remembered");
		Check(launcher.Current().mods.size() == 1, "a deleted mod is dropped from the selection");
		Check(launcher.Current().warp && launcher.Current().map == 7 && launcher.Current().skill == 4,
			"the start options are remembered");

		// Episode maps: E2M3 on Heretic
		Press(launcher, "ddd" "a");
		Press(launcher, "d" "rr" "d" "llll");  // episode 2; map 7 -> 3
		Press(launcher, "b");
		const std::string line = Join(launcher.BuildArguments());
		Check(line.find(" -warp 2 3 -skill 4") != std::string::npos, "an episode's map is -warp E M");

		// Load the preset: the deleted mod is gone from it too
		Press(launcher, "d" "a" "d" "a");
		Check(launcher.Current().mods.size() == 1 && launcher.Current().episode == 0 && launcher.Current().map == 7,
			"a preset loads, without the mod that was deleted");

		launcher.ShowMessage("The last run ended with an error",
			"Script error, \"brutalv22.pk3:zscript.txt\" line 4:\nUnknown class name 'Foo'\n\n"
			"Execution could not continue. The full log is in /data/uzdoom/uzdoom.log");
		Snapshot(launcher, "12-error");
		Check(Press(launcher, "a") == Launcher::Result::Running, "the message closes");
		Press(launcher, "uu");
		Check(Press(launcher, "o") == Launcher::Result::Launch, "Options starts the game from the main menu");
	}

	// The user's folder inside the title's folder: the paths shown are the
	// FTP client's, and the games are not listed twice.
	{
		Launcher launcher(user, user, "/data/homebrew/PPSA99666");
		launcher.Rescan();
		Check(launcher.Games().size() == 3, "one folder for both roles lists each game once");
		TextScreen screen;
		launcher.Draw(screen);
		const std::string text = screen.Dump();
		Check(text.find("FTP to /data/homebrew/PPSA99666/iwads") != std::string::npos &&
			text.find(user) == std::string::npos, "the screen names the folder as FTP sees it");
		Snapshot(launcher, "13-title-folder");
	}

	printf("%s\n", failures ? "FAILED" : "all passed");
	return failures ? 1 : 0;
}
