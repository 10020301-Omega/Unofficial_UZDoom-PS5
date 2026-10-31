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
		case '<': input.pageUp = true; break;
		case '>': input.pageDown = true; break;
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


static std::string ReadFile(const std::string &path)
{
	std::ifstream file(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

static std::string ScreenText(const Launcher &launcher)
{
	TextScreen screen;
	launcher.Draw(screen);
	return screen.Dump();
}

// Recover visible right-panel text across all pages, checking the page cycle.
static std::string AllDetails(Launcher &launcher)
{
	const std::string first = ScreenText(launcher);
	std::string text;
	for (int page = 0; page < 200; ++page)
	{
		TextScreen screen;
		launcher.Draw(screen);
		for (int y = 3; y <= 18; ++y)
			for (int x = 29; x <= 75; ++x)
				if (screen.At(x, y).ch != ' ') text += (char)screen.At(x, y).ch;
		Press(launcher, ">");
		if (ScreenText(launcher) == first) return text;
	}
	Check(false, "detail pagination terminates");
	return text;
}

static std::string NoSpaces(std::string text)
{
	std::string result;
	for (char c : text) if (c != ' ') result += c;
	return result;
}

static void StressPresets()
{
	const std::string user = work + "/stress", app = work + "/app";
	Launcher setup(user, app);
	setup.Rescan();
	Touch(user + "/iwads/DOOM2.WAD", 16);
	Touch(user + "/iwads/HERETIC.WAD", 16);
	std::vector<std::string> mods;
	for (int i = 0; i < 36; ++i)
	{
		const std::string name = std::to_string(i + 1) + "-" +
			(i % 2 ? std::string(180, 'x') : "Map pack with spaces and = signs") + "-end.pk3";
		mods.push_back(user + "/mods/" + name);
		Touch(mods.back(), 16);
	}
	const std::string cfg = user + "/config/launcher.cfg";
	{
		std::ofstream f(cfg);
		auto selection = [&](int slot, bool many) {
			f << "game=" << user << "/iwads/DOOM2.WAD\n";
			if (many) for (const auto &mod : mods) f << "mod=" << mod << "\n";
			f << "warp=1\nepisode=0\nmap=" << slot << "\nskill=4\nmonsters=1\n";
		};
		selection(1, true);
		for (int i = 1; i <= 8; ++i)
		{
			f << "[preset " << i << "]\nname=Slot " << i << " " << std::string(90, 'N') << "\n";
			selection(i, i != 3); // slot 3 is used but deliberately has no mods
		}
	}
	Launcher launcher(user, app);
	launcher.Load(); launcher.Rescan();
	const std::string originalCfg = ReadFile(cfg);
	const auto originalArgs = launcher.BuildArguments();
	launcher.Save();
	Check(ReadFile(cfg) == originalCfg, "legacy format and all eight presets round-trip byte-for-byte");
	const std::string main = ScreenText(launcher);
	const std::string all = AllDetails(launcher);
	for (const auto &mod : mods)
		Check(all.find(NoSpaces(BaseName(mod))) != std::string::npos, "main pages expose complete long filename");
	Check(ScreenText(launcher) == main, "R1 wraps back to first main page");
	Press(launcher, "<>");
	Check(ScreenText(launcher) == main, "L1 and R1 are inverse on main");
	Snapshot(launcher, "14-many-main-first");
	Press(launcher, ">"); Snapshot(launcher, "15-many-main-next");
	Press(launcher, "dddd" "a");
	for (int slot = 1; slot <= 8; ++slot)
	{
		const auto text = ScreenText(launcher);
		Check(text.find("Slot " + std::to_string(slot)) != std::string::npos,
			"highlight previews that preset without loading");
		Check(text.find("1/") != std::string::npos, "changing highlighted preset resets detail page");
		const auto details = AllDetails(launcher);
		Check(details.find("MAP0" + std::to_string(slot)) != std::string::npos,
			"preview shows highlighted preset start options");
		Check(details.find("Ultra-Violence") != std::string::npos && details.find("Fast") != std::string::npos,
			"preview includes skill and monster mode");
		if (slot != 3)
			for (const auto &mod : mods)
				Check(details.find(NoSpaces(BaseName(mod))) != std::string::npos,
					"preset pages expose every filename without truncation");
		else Check(details.find("(none)") != std::string::npos, "used zero-mod preset displays none");
		Check(launcher.BuildArguments() == originalArgs, "preview and paging leave active selection unchanged");
		Check(ReadFile(cfg) == originalCfg, "preview and paging never write launcher.cfg");
		if (slot == 1) Snapshot(launcher, "16-preset-first");
		Press(launcher, ">");
		if (slot == 1) Snapshot(launcher, "17-preset-long-name");
		Press(launcher, "d");
	}
	Check(ScreenText(launcher).find("Slot 1") != std::string::npos, "down wraps through all eight slots");
	Press(launcher, "u");
	Check(ScreenText(launcher).find("Slot 8") != std::string::npos, "up wraps to slot eight");
	Press(launcher, "a");
	Check(launcher.Current().map == 8 && launcher.Current().mods == mods,
		"Cross loads eighth preset with exact mod order");
	const auto args = launcher.BuildArguments();
	Check(args.size() > mods.size() + 3 && args[3] == mods.front() && args[38] == mods.back(),
		"launch arguments retain first and last mod order");
	// Main cursor remains Presets, so Cross opens it again at slot 1.
	Press(launcher, "a" "u" "s");
	Snapshot(launcher, "18-overwrite-eight");
	Check(ScreenText(launcher).find("Slot 8") == std::string::npos,
		"Square overwrites only the highlighted slot with generated name");
	Press(launcher, "t");
	Snapshot(launcher, "19-erased-eight");
	const auto erased = ReadFile(cfg);
	Check(erased.find("[preset 8]") == std::string::npos && erased.find("[preset 7]") != std::string::npos,
		"Triangle erases slot eight and preserves slot seven");
	Check(launcher.Current().mods == mods, "erasing preset leaves active mods unchanged");
	Check(ScreenText(launcher).find("Empty slot") != std::string::npos,
		"erased preset preview immediately becomes empty");
	Press(launcher, "a");
	Check(ScreenText(launcher).find("Empty slot") != std::string::npos, "Cross on empty slot does not load");
	Press(launcher, "b");
	Launcher reloaded(user, app); reloaded.Load(); reloaded.Rescan();
	Check(reloaded.Current().map == 8 && reloaded.Current().mods == mods,
		"selection survives save and reload after erase");
	Press(reloaded, "dddd" "a" "uu"); // slot seven
	Check(ScreenText(reloaded).find("Slot 7") != std::string::npos,
		"another preset survives overwrite erase and reload");
	Press(reloaded, "a");
	Check(reloaded.Current().map == 7 && reloaded.Current().mods == mods,
		"surviving preset loads all mods in order");
	Check(Press(reloaded, "o") == Launcher::Result::Launch, "Options launches after preset reload");
}


static void DemoScreens()
{
	const std::string user = work + "/demo";
	Launcher setup(user, user, "/data/uzdoom"); setup.Rescan();
	Touch(user + "/iwads/DOOM2.WAD", 16);
	const std::vector<std::string> names = {"arrival.wad", "GunBonsai-0.10.6.pk3", "LegenDoom_3.0.pk3"};
	for (const auto &name : names) Touch(user + "/mods/" + name, 16);
	{
		std::ofstream cfg(user + "/config/launcher.cfg");
		auto selection = [&]() {
			cfg << "game=" << user << "/iwads/DOOM2.WAD\n";
			for (const auto &name : names) cfg << "mod=" << user << "/mods/" << name << "\n";
			cfg << "warp=1\nepisode=0\nmap=1\nskill=4\nmonsters=0\n";
		};
		selection();
		cfg << "[preset 1]\nname=Arrival + Gun Bonsai + LegenDoom\n"; selection();
	}
	Launcher demo(user, user, "/data/uzdoom"); demo.Load(); demo.Rescan();
	Snapshot(demo, "20-demo-main");
	Check(ScreenText(demo).find("LegenDoom_3.0.pk3") != std::string::npos,
		"three typical mods are visible together on main");
	Press(demo, "dddd" "a"); Snapshot(demo, "21-demo-presets");
	Check(AllDetails(demo).find("LegenDoom_3.0.pk3") != std::string::npos,
		"typical preset includes all three files");
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

	StressPresets();
	DemoScreens();
	printf("%s\n", failures ? "FAILED" : "all passed");
	return failures ? 1 : 0;
}
