/*
** launcher.h
**
** The launcher: a DOS-style setup program shown before the engine starts,
** where the game, the mods and the start options are chosen with the pad.
** It knows nothing of the console or of Vulkan: it takes button presses,
** draws on a TextScreen and hands back the engine's command line, so the
** same code runs in the host test (ps5/launcher/test_launcher.cpp).
**
** Copyright 2026 PS5-UZDOOM port contributors
** SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "textscreen.h"

namespace dosui
{

// The buttons pressed since the last update (held directions repeat).
struct Input
{
	bool up = false, down = false, left = false, right = false;
	bool accept = false;   // Cross
	bool back = false;     // Circle
	bool square = false;
	bool triangle = false;
	bool pageUp = false;   // L1
	bool pageDown = false; // R1
	bool start = false;    // Options
};

struct FileEntry
{
	std::string path;  // where it is
	std::string name;  // what the list shows: the path below its folder
	uint64_t size = 0;
};

enum class Monsters { Normal, Fast, Respawn, None, Count };

// What gets launched; also what a preset holds.
struct Selection
{
	std::string game;               // path of the game file
	std::vector<std::string> mods;  // paths, in load order
	bool warp = false;              // start in a map instead of at the title screen
	int episode = 0;                // 0: maps are numbered alone (MAP07); else ExMy
	int map = 1;
	int skill = 3;                  // 1..5
	Monsters monsters = Monsters::Normal;
};

struct Preset
{
	std::string name;
	Selection selection;
	bool used = false;
};

class Launcher
{
public:
	enum class Result { Running, Launch, Quit };
	static constexpr int PresetCount = 8;

	// userRoot: the user's folder (iwads/, mods/, saves/, config/ below it).
	// appRoot: the title's folder; games shipped with the title are in its iwads/.
	Launcher(const std::string &userRoot, const std::string &appRoot);

	// Make the user's folders if they are missing and read them again.
	void Rescan();
	// A message to show first, such as the last run's fatal error.
	void ShowMessage(const std::string &title, const std::string &text);

	Result Update(const Input &input);
	void Draw(TextScreen &screen) const;

	// The engine's command line for the selection, without the program name.
	std::vector<std::string> BuildArguments() const;

	void Load();
	void Save() const;

	const Selection &Current() const { return selection; }
	const std::vector<FileEntry> &Games() const { return games; }
	const std::vector<FileEntry> &Mods() const { return mods; }

private:
	enum class Screen { Main, Games, Mods, Options, Presets, Message };

	void UpdateMain(const Input &input);
	void UpdateGames(const Input &input);
	void UpdateMods(const Input &input);
	void UpdateOptions(const Input &input);
	void UpdatePresets(const Input &input);

	void DrawFrame(TextScreen &screen, const std::string &help) const;
	void DrawMain(TextScreen &screen) const;
	void DrawGames(TextScreen &screen) const;
	void DrawMods(TextScreen &screen) const;
	void DrawOptions(TextScreen &screen) const;
	void DrawPresets(TextScreen &screen) const;
	void DrawMessage(TextScreen &screen) const;

	void Open(Screen next);
	int ModOrder(const std::string &path) const;
	void DropMissing(Selection &sel) const;
	std::string StartSummary(const Selection &sel) const;
	std::string PresetName(const Selection &sel) const;

	std::string userRoot, appRoot;
	std::vector<FileEntry> games, mods;
	Selection selection;
	Preset presets[PresetCount];

	Screen screen = Screen::Main;
	Result result = Result::Running;
	int cursor = 0;      // the item in focus on the current screen
	int top = 0;         // first list row shown
	int mainCursor = 0;  // remembered while a sub-screen is open
	std::string messageTitle, messageText;
};

// A game file's proper name ("DOOM II: Hell on Earth"), or its file name.
std::string GameTitle(const std::string &path);
// The last part of a path.
std::string BaseName(const std::string &path);
// "512 K", "12.3 M"
std::string SizeText(uint64_t bytes);

} // namespace dosui
