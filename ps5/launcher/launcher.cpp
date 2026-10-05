/*
** launcher.cpp
**
** Copyright 2026 PS5-UZDOOM port contributors
** SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "launcher.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace dosui
{

namespace
{

// ------------------------------------------------------------------ colours

const uint8_t BackgroundAttr = Attr(LightBlue, Blue);
const uint8_t BarAttr = Attr(Black, LightGray);
const uint8_t BarKeyAttr = Attr(Red, LightGray);
const uint8_t BoxAttr = Attr(Black, LightGray);
const uint8_t BoxDimAttr = Attr(DarkGray, LightGray);
const uint8_t BoxValueAttr = Attr(Blue, LightGray);
const uint8_t FocusAttr = Attr(White, Red);
const uint8_t InfoAttr = Attr(White, Blue);
const uint8_t InfoLabelAttr = Attr(Yellow, Blue);
const uint8_t MessageAttr = Attr(White, Red);

// ------------------------------------------------------------------ strings

std::string Lower(std::string text)
{
	for (char &c : text)
		c = (char)tolower((unsigned char)c);
	return text;
}

std::string Extension(const std::string &name)
{
	const size_t dot = name.rfind('.');
	return dot == std::string::npos ? "" : Lower(name.substr(dot + 1));
}

// File names are UTF-8 and the screen is code page 437: what is not ASCII
// is shown as a question mark, never passed on changed.
std::string Display(const std::string &text)
{
	std::string out;
	for (size_t i = 0; i < text.size(); i++)
	{
		const unsigned char c = (unsigned char)text[i];
		if (c < 0x80)
		{
			out += (c < 0x20) ? '?' : (char)c;
			continue;
		}
		// one question mark for a whole UTF-8 sequence
		out += '?';
		while (i + 1 < text.size() && ((unsigned char)text[i + 1] & 0xC0) == 0x80)
			i++;
	}
	return out;
}

bool IsGameFile(const std::string &name)
{
	const std::string ext = Extension(name);
	return ext == "wad" || ext == "iwad" || ext == "ipk3" || ext == "ipk7" || ext == "pk3" || ext == "pk7";
}

bool IsModFile(const std::string &name)
{
	const std::string ext = Extension(name);
	return ext == "wad" || ext == "pk3" || ext == "pk7" || ext == "pke" || ext == "zip" ||
		ext == "deh" || ext == "bex" || ext == "ipk3" || ext == "ipk7";
}

// The files of a folder that pass the filter, with those of its subfolders
// down to a few levels, sorted without regard to case.
void Scan(const std::string &root, const std::string &below, int depth, bool (*filter)(const std::string &),
	std::vector<FileEntry> &out)
{
	const std::string folder = below.empty() ? root : root + "/" + below;
	DIR *dir = opendir(folder.c_str());
	if (dir == nullptr)
		return;
	while (struct dirent *entry = readdir(dir))
	{
		const std::string name = entry->d_name;
		if (name.empty() || name[0] == '.')
			continue;
		const std::string relative = below.empty() ? name : below + "/" + name;
		const std::string path = root + "/" + relative;
		struct stat info;
		if (stat(path.c_str(), &info) != 0)
			continue;
		if (S_ISDIR(info.st_mode))
		{
			if (depth > 0)
				Scan(root, relative, depth - 1, filter, out);
		}
		else if (S_ISREG(info.st_mode) && filter(name))
		{
			FileEntry file;
			file.path = path;
			file.name = relative;
			file.size = (uint64_t)info.st_size;
			out.push_back(file);
		}
	}
	closedir(dir);
}

void SortFiles(std::vector<FileEntry> &files)
{
	std::sort(files.begin(), files.end(), [](const FileEntry &a, const FileEntry &b) {
		const std::string la = Lower(a.name), lb = Lower(b.name);
		return la != lb ? la < lb : a.name < b.name;
	});
}

// Folders the user fills over FTP: the FTP server is another process, so
// they are opened to everyone explicitly, whatever the umask gave.
void MakeFolder(const std::string &path)
{
	mkdir(path.c_str(), 0777);
	chmod(path.c_str(), 0777);
}

const struct { const char *file; const char *title; bool doomSkills; } KnownGames[] = {
	{"doom.wad", "The Ultimate DOOM", true},
	{"doomu.wad", "The Ultimate DOOM", true},
	{"doom1.wad", "DOOM Shareware", true},
	{"doom2.wad", "DOOM II: Hell on Earth", true},
	{"doom2f.wad", "DOOM II: Hell on Earth (French)", true},
	{"tnt.wad", "Final DOOM: TNT - Evilution", true},
	{"plutonia.wad", "Final DOOM: The Plutonia Experiment", true},
	{"bfgdoom.wad", "DOOM (BFG Edition)", true},
	{"bfgdoom2.wad", "DOOM II (BFG Edition)", true},
	{"doomunity.wad", "DOOM (Unity)", true},
	{"doom2unity.wad", "DOOM II (Unity)", true},
	{"freedoom1.wad", "Freedoom: Phase 1", true},
	{"freedoom2.wad", "Freedoom: Phase 2", true},
	{"freedm.wad", "FreeDM", true},
	{"heretic.wad", "Heretic: Shadow of the Serpent Riders", false},
	{"heretic1.wad", "Heretic Shareware", false},
	{"hexen.wad", "Hexen: Beyond Heretic", false},
	{"hexdd.wad", "Hexen: Deathkings of the Dark Citadel", false},
	{"strife1.wad", "Strife: Quest for the Sigil", false},
	{"strife0.wad", "Strife Teaser", false},
	{"chex.wad", "Chex Quest", true},
	{"chex3.wad", "Chex Quest 3", true},
	{"hacx.wad", "HacX", true},
	{"square1.pk3", "The Adventures of Square", false},
	{"harm1.wad", "Harmony", true},
};

const char *const DoomSkills[5] = {
	"I'm Too Young To Die", "Hey, Not Too Rough", "Hurt Me Plenty", "Ultra-Violence", "Nightmare!"
};

bool HasDoomSkills(const std::string &path)
{
	const std::string name = Lower(BaseName(path));
	for (const auto &known : KnownGames)
		if (name == known.file)
			return known.doomSkills;
	return false;
}

const char *MonstersText(Monsters monsters)
{
	switch (monsters)
	{
	case Monsters::Fast: return "Fast";
	case Monsters::Respawn: return "Respawning";
	case Monsters::None: return "None";
	default: return "Normal";
	}
}

std::string MapText(const Selection &sel)
{
	char text[32];
	if (sel.episode > 0)
		snprintf(text, sizeof(text), "E%dM%d", sel.episode, sel.map);
	else
		snprintf(text, sizeof(text), "MAP%02d", sel.map);
	return text;
}

std::string SkillText(const Selection &sel)
{
	if (HasDoomSkills(sel.game))
		return DoomSkills[sel.skill - 1];
	char text[32];
	snprintf(text, sizeof(text), "%d of 5", sel.skill);
	return text;
}

int Clamp(int value, int low, int high)
{
	return value < low ? low : (value > high ? high : value);
}

// A list's cursor and scroll position after a move.
void MoveList(const Input &input, int count, int rows, int &cursor, int &top)
{
	if (count <= 0)
	{
		cursor = top = 0;
		return;
	}
	if (input.up) cursor = cursor > 0 ? cursor - 1 : count - 1;
	if (input.down) cursor = cursor < count - 1 ? cursor + 1 : 0;
	if (input.pageUp) cursor = Clamp(cursor - rows, 0, count - 1);
	if (input.pageDown) cursor = Clamp(cursor + rows, 0, count - 1);
	cursor = Clamp(cursor, 0, count - 1);
	if (cursor < top) top = cursor;
	if (cursor >= top + rows) top = cursor - rows + 1;
	top = Clamp(top, 0, count > rows ? count - rows : 0);
}

// The bottom bar: "Key=What|Key=What", each key in red before what it does.
void DrawHelp(TextScreen &screen, const std::string &help)
{
	const int y = TextScreen::Rows - 1;
	screen.Fill(0, y, TextScreen::Columns, 1, ' ', BarAttr);
	int x = 1;
	size_t start = 0;
	while (start < help.size())
	{
		size_t end = help.find('|', start);
		if (end == std::string::npos)
			end = help.size();
		const std::string entry = help.substr(start, end - start);
		const size_t equals = entry.find('=');
		const std::string key = entry.substr(0, equals);
		const std::string what = equals == std::string::npos ? "" : entry.substr(equals + 1);
		x = screen.Print(x, y, key, BarKeyAttr, TextScreen::Columns - 1 - x) + 1;
		x = screen.Print(x, y, what, BarAttr, TextScreen::Columns - 1 - x) + 3;
		if (x >= TextScreen::Columns - 1)
			break;
		start = end + 1;
	}
}

const int ListX = 5, ListY = 2, ListWidth = 70, ListHeight = 20;
const int ListRows = ListHeight - 2;

// Lines of at most `width` characters, broken at spaces and at newlines.
std::vector<std::string> Wrap(const std::string &text, int width)
{
	std::vector<std::string> lines;
	std::string line, word;
	auto flushWord = [&]() {
		while ((int)word.size() > width)
		{
			if (!line.empty()) { lines.push_back(line); line.clear(); }
			lines.push_back(word.substr(0, width));
			word.erase(0, width);
		}
		if (word.empty())
			return;
		if (!line.empty() && (int)(line.size() + 1 + word.size()) > width)
		{
			lines.push_back(line);
			line.clear();
		}
		if (!line.empty())
			line += ' ';
		line += word;
		word.clear();
	};
	for (const char c : text)
	{
		if (c == '\n')
		{
			flushWord();
			lines.push_back(line);
			line.clear();
		}
		else if (c == ' ' || c == '\t' || c == '\r')
		{
			flushWord();
		}
		else
		{
			word += c;
		}
	}
	flushWord();
	if (!line.empty())
		lines.push_back(line);
	return lines;
}

} // namespace

// ------------------------------------------------------------------ helpers

std::string BaseName(const std::string &path)
{
	const size_t slash = path.rfind('/');
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string GameTitle(const std::string &path)
{
	const std::string name = BaseName(path);
	const std::string lower = Lower(name);
	for (const auto &known : KnownGames)
		if (lower == known.file)
			return known.title;
	return Display(name);
}

std::string SizeText(uint64_t bytes)
{
	char text[32];
	if (bytes < 1024 * 1024)
		snprintf(text, sizeof(text), "%u K", (unsigned)((bytes + 1023) / 1024));
	else if (bytes < 1024ull * 1024 * 1024)
		snprintf(text, sizeof(text), "%.1f M", (double)bytes / (1024.0 * 1024.0));
	else
		snprintf(text, sizeof(text), "%.2f G", (double)bytes / (1024.0 * 1024.0 * 1024.0));
	return text;
}

// ----------------------------------------------------------------- Launcher

Launcher::Launcher(const std::string &userRoot, const std::string &appRoot)
	: userRoot(userRoot), appRoot(appRoot)
{
}

void Launcher::Rescan()
{
	MakeFolder(userRoot);
	MakeFolder(userRoot + "/iwads");
	MakeFolder(userRoot + "/mods");
	MakeFolder(userRoot + "/saves");
	MakeFolder(userRoot + "/config");

	games.clear();
	Scan(userRoot + "/iwads", "", 1, IsGameFile, games);
	SortFiles(games);
	// Games shipped in the title's folder come after the user's own.
	std::vector<FileEntry> shipped;
	Scan(appRoot + "/iwads", "", 0, IsGameFile, shipped);
	SortFiles(shipped);
	games.insert(games.end(), shipped.begin(), shipped.end());

	mods.clear();
	Scan(userRoot + "/mods", "", 3, IsModFile, mods);
	SortFiles(mods);

	DropMissing(selection);
	if (selection.game.empty() && !games.empty())
		selection.game = games[0].path;
}

// A selection keeps only what is still on the disk.
void Launcher::DropMissing(Selection &sel) const
{
	bool found = false;
	for (const FileEntry &game : games)
		found |= game.path == sel.game;
	if (!found)
		sel.game.clear();
	std::vector<std::string> kept;
	for (const std::string &mod : sel.mods)
		for (const FileEntry &file : mods)
			if (file.path == mod)
			{
				kept.push_back(mod);
				break;
			}
	sel.mods = kept;
}

void Launcher::ShowMessage(const std::string &title, const std::string &text)
{
	messageTitle = title;
	messageText = text;
	screen = Screen::Message;
}

void Launcher::Open(Screen next)
{
	if (screen == Screen::Main)
		mainCursor = cursor;
	screen = next;
	cursor = top = 0;
	if (next == Screen::Main)
		cursor = mainCursor;
	if (next == Screen::Games)
	{
		for (size_t i = 0; i < games.size(); i++)
			if (games[i].path == selection.game)
				cursor = (int)i;
		Input none;
		MoveList(none, (int)games.size(), ListRows, cursor, top);
	}
}

int Launcher::ModOrder(const std::string &path) const
{
	for (size_t i = 0; i < selection.mods.size(); i++)
		if (selection.mods[i] == path)
			return (int)i + 1;
	return 0;
}

std::string Launcher::StartSummary(const Selection &sel) const
{
	if (!sel.warp)
		return "Title screen";
	std::string text = "Warp to " + MapText(sel) + ", " + SkillText(sel);
	if (sel.monsters != Monsters::Normal)
		text += std::string(", monsters: ") + MonstersText(sel.monsters);
	return text;
}

std::string Launcher::PresetName(const Selection &sel) const
{
	std::string name = GameTitle(sel.game);
	if (!sel.mods.empty())
	{
		name += " + " + Display(BaseName(sel.mods[0]));
		if (sel.mods.size() > 1)
		{
			char more[24];
			snprintf(more, sizeof(more), " +%d", (int)sel.mods.size() - 1);
			name += more;
		}
	}
	return name;
}

// ------------------------------------------------------------------- update

Launcher::Result Launcher::Update(const Input &input)
{
	switch (screen)
	{
	case Screen::Main: UpdateMain(input); break;
	case Screen::Games: UpdateGames(input); break;
	case Screen::Mods: UpdateMods(input); break;
	case Screen::Options: UpdateOptions(input); break;
	case Screen::Presets: UpdatePresets(input); break;
	case Screen::Message:
		if (input.accept || input.back || input.start)
		{
			screen = Screen::Main;
			cursor = mainCursor;
		}
		break;
	}
	return result;
}

enum { MAIN_START, MAIN_GAME, MAIN_MODS, MAIN_OPTIONS, MAIN_PRESETS, MAIN_RESCAN, MAIN_QUIT, MAIN_COUNT };

void Launcher::UpdateMain(const Input &input)
{
	if (input.up) cursor = cursor > 0 ? cursor - 1 : MAIN_COUNT - 1;
	if (input.down) cursor = cursor < MAIN_COUNT - 1 ? cursor + 1 : 0;

	const bool start = input.start || (input.accept && cursor == MAIN_START);
	if (start)
	{
		if (selection.game.empty())
		{
			mainCursor = cursor;
			ShowMessage("No game", "There is no game to start.\n\nCopy a game file such as DOOM2.WAD into\n" +
				userRoot + "/iwads\nover FTP, then choose Rescan Folders.");
		}
		else
		{
			Save();
			result = Result::Launch;
		}
		return;
	}
	if (!input.accept)
		return;
	switch (cursor)
	{
	case MAIN_GAME: Open(Screen::Games); break;
	case MAIN_MODS: Open(Screen::Mods); break;
	case MAIN_OPTIONS: Open(Screen::Options); break;
	case MAIN_PRESETS: Open(Screen::Presets); break;
	case MAIN_RESCAN: Rescan(); break;
	case MAIN_QUIT: Save(); result = Result::Quit; break;
	}
}

void Launcher::UpdateGames(const Input &input)
{
	MoveList(input, (int)games.size(), ListRows, cursor, top);
	if (input.accept && !games.empty())
	{
		selection.game = games[cursor].path;
		Open(Screen::Main);
	}
	else if (input.back)
	{
		Open(Screen::Main);
	}
}

void Launcher::UpdateMods(const Input &input)
{
	MoveList(input, (int)mods.size(), ListRows, cursor, top);
	if (input.back || input.start)
	{
		Open(Screen::Main);
		return;
	}
	if (mods.empty())
		return;
	const std::string &path = mods[cursor].path;
	const int order = ModOrder(path);
	auto &chosen = selection.mods;
	if (input.accept)
	{
		// ticking adds at the end of the load order; unticking takes it out
		if (order > 0)
			chosen.erase(chosen.begin() + (order - 1));
		else
			chosen.push_back(path);
	}
	else if (input.left && order > 1)
	{
		std::swap(chosen[order - 1], chosen[order - 2]);
	}
	else if (input.right && order > 0 && order < (int)chosen.size())
	{
		std::swap(chosen[order - 1], chosen[order]);
	}
	else if (input.triangle)
	{
		chosen.clear();
	}
}

enum { OPT_START, OPT_EPISODE, OPT_MAP, OPT_SKILL, OPT_MONSTERS, OPT_COUNT };

void Launcher::UpdateOptions(const Input &input)
{
	if (input.back || input.start)
	{
		Open(Screen::Main);
		return;
	}
	const int count = selection.warp ? OPT_COUNT : 1;
	if (input.up) cursor = cursor > 0 ? cursor - 1 : count - 1;
	if (input.down) cursor = cursor < count - 1 ? cursor + 1 : 0;
	cursor = Clamp(cursor, 0, count - 1);

	int step = 0;
	if (input.right || input.accept) step = 1;
	if (input.left) step = -1;
	if (step == 0)
		return;
	switch (cursor)
	{
	case OPT_START:
		selection.warp = !selection.warp;
		break;
	case OPT_EPISODE:
		selection.episode = (selection.episode + step + 10) % 10;
		// an episode has nine maps at most
		if (selection.episode > 0 && selection.map > 9)
			selection.map = 1;
		break;
	case OPT_MAP:
	{
		const int last = selection.episode > 0 ? 9 : 99;
		selection.map += step;
		if (selection.map < 1) selection.map = last;
		if (selection.map > last) selection.map = 1;
		break;
	}
	case OPT_SKILL:
		selection.skill += step;
		if (selection.skill < 1) selection.skill = 5;
		if (selection.skill > 5) selection.skill = 1;
		break;
	case OPT_MONSTERS:
	{
		const int total = (int)Monsters::Count;
		selection.monsters = (Monsters)(((int)selection.monsters + step + total) % total);
		break;
	}
	}
}

void Launcher::UpdatePresets(const Input &input)
{
	if (input.up) cursor = cursor > 0 ? cursor - 1 : PresetCount - 1;
	if (input.down) cursor = cursor < PresetCount - 1 ? cursor + 1 : 0;
	Preset &preset = presets[cursor];
	if (input.back || input.start)
	{
		Open(Screen::Main);
	}
	else if (input.accept && preset.used)
	{
		selection = preset.selection;
		DropMissing(selection);
		if (selection.game.empty() && !games.empty())
			selection.game = games[0].path;
		Save();
		Open(Screen::Main);
	}
	else if (input.square && !selection.game.empty())
	{
		preset.selection = selection;
		preset.name = PresetName(selection);
		preset.used = true;
		Save();
	}
	else if (input.triangle && preset.used)
	{
		preset = Preset();
		Save();
	}
}

// --------------------------------------------------------------------- draw

void Launcher::DrawFrame(TextScreen &out, const std::string &help) const
{
	out.Clear(BackgroundAttr, CH_SHADE_LIGHT);
	out.Fill(0, 0, TextScreen::Columns, 1, ' ', BarAttr);
	out.PrintCentered(0, 0, TextScreen::Columns, "UZDoom Setup  -  PlayStation 5", BarAttr);
	DrawHelp(out, help);
}

void Launcher::Draw(TextScreen &out) const
{
	switch (screen)
	{
	case Screen::Main: DrawMain(out); break;
	case Screen::Games: DrawGames(out); break;
	case Screen::Mods: DrawMods(out); break;
	case Screen::Options: DrawOptions(out); break;
	case Screen::Presets: DrawPresets(out); break;
	case Screen::Message: DrawMessage(out); break;
	}
}

void Launcher::DrawMain(TextScreen &out) const
{
	DrawFrame(out, "Up/Down=Move|Cross=Select|Options=Start Game");

	static const char *const items[MAIN_COUNT] = {
		"Start Game", "Select Game", "Select Mods", "Start Options", "Presets", "Rescan Folders", "Quit"
	};
	const int width = 34, height = MAIN_COUNT + 4;
	const int x = (TextScreen::Columns - width) / 2, y = 3;
	out.Box(x, y, width, height, BoxAttr, true, "Main Menu");
	out.Shadow(x, y, width, height);
	for (int i = 0; i < MAIN_COUNT; i++)
	{
		const bool disabled = i == MAIN_START && selection.game.empty();
		out.Print(x + 4, y + 2 + i, items[i], disabled ? BoxDimAttr : BoxAttr);
		if (i == cursor)
			out.Recolor(x + 2, y + 2 + i, width - 4, FocusAttr);
	}

	// What will start
	const int infoX = 3, infoY = 16, infoWidth = 74, infoHeight = 6;
	out.Box(infoX, infoY, infoWidth, infoHeight, InfoAttr, false, "Ready to start");
	const int valueX = infoX + 10, valueWidth = infoWidth - 12;
	out.Print(infoX + 2, infoY + 1, "Game:", InfoLabelAttr);
	out.Print(infoX + 2, infoY + 2, "Mods:", InfoLabelAttr);
	out.Print(infoX + 2, infoY + 3, "Start:", InfoLabelAttr);
	out.Print(infoX + 2, infoY + 4, "Files:", InfoLabelAttr);
	if (selection.game.empty())
	{
		out.Print(valueX, infoY + 1, "none found - copy one to " + userRoot + "/iwads", InfoAttr, valueWidth);
	}
	else
	{
		out.Print(valueX, infoY + 1, GameTitle(selection.game) + "  (" + Display(BaseName(selection.game)) + ")",
			InfoAttr, valueWidth);
	}
	std::string modText = "none";
	if (!selection.mods.empty())
	{
		modText.clear();
		for (size_t i = 0; i < selection.mods.size(); i++)
			modText += (i ? ", " : "") + Display(BaseName(selection.mods[i]));
	}
	out.Print(valueX, infoY + 2, modText, InfoAttr, valueWidth);
	out.Print(valueX, infoY + 3, StartSummary(selection), InfoAttr, valueWidth);
	out.Print(valueX, infoY + 4, "FTP to " + userRoot + "/iwads and " + userRoot + "/mods", InfoAttr, valueWidth);
}

void Launcher::DrawGames(TextScreen &out) const
{
	DrawFrame(out, "Up/Down=Move|L1/R1=Page|Cross=Choose|Circle=Back");
	out.Box(ListX, ListY, ListWidth, ListHeight, BoxAttr, true, "Select Game");
	out.Shadow(ListX, ListY, ListWidth, ListHeight);
	if (games.empty())
	{
		out.PrintCentered(ListX + 1, ListY + 7, ListWidth - 2, "No game files found.", BoxAttr);
		out.PrintCentered(ListX + 1, ListY + 9, ListWidth - 2, "Copy DOOM.WAD, DOOM2.WAD or another game file to", BoxAttr);
		out.PrintCentered(ListX + 1, ListY + 10, ListWidth - 2, userRoot + "/iwads", BoxValueAttr);
		out.PrintCentered(ListX + 1, ListY + 11, ListWidth - 2, "over FTP, then choose Rescan Folders.", BoxAttr);
		return;
	}
	for (int row = 0; row < ListRows && top + row < (int)games.size(); row++)
	{
		const FileEntry &game = games[top + row];
		const int y = ListY + 1 + row;
		const bool current = game.path == selection.game;
		out.Put(ListX + 2, y, current ? (uint8_t)CH_ARROW_RIGHT : ' ', BoxAttr);
		out.Print(ListX + 4, y, GameTitle(game.path), BoxAttr, 38);
		out.Print(ListX + 43, y, Display(game.name), BoxDimAttr, 16);
		const std::string size = SizeText(game.size);
		out.Print(ListX + ListWidth - 2 - (int)size.size(), y, size, BoxDimAttr);
		if (top + row == cursor)
			out.Recolor(ListX + 1, y, ListWidth - 2, FocusAttr);
	}
	if (top > 0)
		out.Put(ListX + ListWidth - 1, ListY + 1, CH_ARROW_UP, BoxAttr);
	if (top + ListRows < (int)games.size())
		out.Put(ListX + ListWidth - 1, ListY + ListHeight - 2, CH_ARROW_DOWN, BoxAttr);
}

void Launcher::DrawMods(TextScreen &out) const
{
	DrawFrame(out, "Cross=Tick|Left/Right=Load Order|Triangle=Clear All|Circle=Done");
	char title[64];
	snprintf(title, sizeof(title), "Select Mods - %d of %d ticked", (int)selection.mods.size(), (int)mods.size());
	out.Box(ListX, ListY, ListWidth, ListHeight, BoxAttr, true, title);
	out.Shadow(ListX, ListY, ListWidth, ListHeight);
	if (mods.empty())
	{
		out.PrintCentered(ListX + 1, ListY + 7, ListWidth - 2, "No mods found.", BoxAttr);
		out.PrintCentered(ListX + 1, ListY + 9, ListWidth - 2, "Copy .pk3 and .wad files to", BoxAttr);
		out.PrintCentered(ListX + 1, ListY + 10, ListWidth - 2, userRoot + "/mods", BoxValueAttr);
		out.PrintCentered(ListX + 1, ListY + 11, ListWidth - 2, "over FTP, then choose Rescan Folders.", BoxAttr);
		return;
	}
	for (int row = 0; row < ListRows && top + row < (int)mods.size(); row++)
	{
		const FileEntry &mod = mods[top + row];
		const int y = ListY + 1 + row;
		const int order = ModOrder(mod.path);
		char box[16];
		if (order > 0)
			snprintf(box, sizeof(box), "[%2d]", order);
		else
			snprintf(box, sizeof(box), "[  ]");
		out.Print(ListX + 2, y, box, order > 0 ? BoxValueAttr : BoxAttr);
		out.Print(ListX + 7, y, Display(mod.name), BoxAttr, ListWidth - 20);
		const std::string size = SizeText(mod.size);
		out.Print(ListX + ListWidth - 2 - (int)size.size(), y, size, BoxDimAttr);
		if (top + row == cursor)
			out.Recolor(ListX + 1, y, ListWidth - 2, FocusAttr);
	}
	if (top > 0)
		out.Put(ListX + ListWidth - 1, ListY + 1, CH_ARROW_UP, BoxAttr);
	if (top + ListRows < (int)mods.size())
		out.Put(ListX + ListWidth - 1, ListY + ListHeight - 2, CH_ARROW_DOWN, BoxAttr);
	out.PrintCentered(0, ListY + ListHeight + 1, TextScreen::Columns,
		"The number is the load order: later files override earlier ones.", InfoAttr);
}

void Launcher::DrawOptions(TextScreen &out) const
{
	DrawFrame(out, "Up/Down=Move|Left/Right=Change|Circle=Done");
	const int width = 56, height = 11;
	const int x = (TextScreen::Columns - width) / 2, y = 5;
	out.Box(x, y, width, height, BoxAttr, true, "Start Options");
	out.Shadow(x, y, width, height);

	static const char *const labels[OPT_COUNT] = { "Start at", "Episode", "Map", "Skill", "Monsters" };
	std::string values[OPT_COUNT];
	values[OPT_START] = selection.warp ? "A map" : "Title screen";
	if (selection.episode > 0)
	{
		char text[16];
		snprintf(text, sizeof(text), "%d", selection.episode);
		values[OPT_EPISODE] = text;
	}
	else
	{
		values[OPT_EPISODE] = "None (MAPxx)";
	}
	values[OPT_MAP] = MapText(selection);
	values[OPT_SKILL] = SkillText(selection);
	values[OPT_MONSTERS] = MonstersText(selection.monsters);

	for (int i = 0; i < OPT_COUNT; i++)
	{
		const bool off = i > 0 && !selection.warp;
		const int row = y + 2 + i + (i > 0 ? 1 : 0);
		out.Print(x + 4, row, labels[i], off ? BoxDimAttr : BoxAttr);
		out.Put(x + 18, row, CH_ARROW_LEFT, off ? BoxDimAttr : BoxAttr);
		out.Print(x + 20, row, off ? "-" : values[i], off ? BoxDimAttr : BoxValueAttr, width - 26);
		out.Put(x + width - 4, row, CH_ARROW_RIGHT, off ? BoxDimAttr : BoxAttr);
		if (i == cursor)
			out.Recolor(x + 2, row, width - 4, FocusAttr);
	}
	out.PrintCentered(0, y + height + 2, TextScreen::Columns,
		"Doom II and its like number maps MAP01 to MAP32; Doom and Heretic use episodes.", InfoAttr);
}

void Launcher::DrawPresets(TextScreen &out) const
{
	DrawFrame(out, "Cross=Load|Square=Save Here|Triangle=Erase|Circle=Back");
	const int width = 70, height = PresetCount + 4;
	const int x = (TextScreen::Columns - width) / 2, y = 4;
	out.Box(x, y, width, height, BoxAttr, true, "Presets");
	out.Shadow(x, y, width, height);
	for (int i = 0; i < PresetCount; i++)
	{
		char number[16];
		snprintf(number, sizeof(number), "%d.", i + 1);
		out.Print(x + 3, y + 2 + i, number, BoxAttr);
		if (presets[i].used)
			out.Print(x + 6, y + 2 + i, presets[i].name, BoxAttr, width - 9);
		else
			out.Print(x + 6, y + 2 + i, "(empty)", BoxDimAttr);
		if (i == cursor)
			out.Recolor(x + 2, y + 2 + i, width - 4, FocusAttr);
	}
	out.PrintCentered(0, y + height + 2, TextScreen::Columns,
		"A preset keeps the game, the mods in their order and the start options.", InfoAttr);
}

void Launcher::DrawMessage(TextScreen &out) const
{
	DrawFrame(out, "Cross=Continue");
	const int width = 68;
	std::vector<std::string> lines = Wrap(Display(messageText), width - 6);
	const int maxLines = 16;
	if ((int)lines.size() > maxLines)
	{
		lines.resize(maxLines);
		lines.back() = "...";
	}
	const int height = (int)lines.size() + 4;
	const int x = (TextScreen::Columns - width) / 2, y = (TextScreen::Rows - height) / 2;
	out.Box(x, y, width, height, MessageAttr, true, messageTitle);
	out.Shadow(x, y, width, height);
	for (size_t i = 0; i < lines.size(); i++)
		out.Print(x + 3, y + 2 + (int)i, lines[i], MessageAttr, width - 6);
}

// ---------------------------------------------------------------- arguments

std::vector<std::string> Launcher::BuildArguments() const
{
	std::vector<std::string> args;
	if (!selection.game.empty())
	{
		args.push_back("-iwad");
		args.push_back(selection.game);
	}
	if (!selection.mods.empty())
	{
		args.push_back("-file");
		for (const std::string &mod : selection.mods)
			args.push_back(mod);
	}
	if (selection.warp)
	{
		char number[16];
		args.push_back("-warp");
		if (selection.episode > 0)
		{
			snprintf(number, sizeof(number), "%d", selection.episode);
			args.push_back(number);
		}
		snprintf(number, sizeof(number), "%d", selection.map);
		args.push_back(number);
		snprintf(number, sizeof(number), "%d", selection.skill);
		args.push_back("-skill");
		args.push_back(number);
		switch (selection.monsters)
		{
		case Monsters::Fast: args.push_back("-fast"); break;
		case Monsters::Respawn: args.push_back("-respawn"); break;
		case Monsters::None: args.push_back("-nomonsters"); break;
		default: break;
		}
	}
	args.push_back("-config");
	args.push_back(userRoot + "/config/uzdoom.ini");
	args.push_back("-savedir");
	args.push_back(userRoot + "/saves");
	return args;
}

// ------------------------------------------------------------------- config

// launcher.cfg: "key=value" lines; "[preset N]" starts a preset's lines.

namespace
{

void WriteSelection(std::ofstream &file, const Selection &sel)
{
	file << "game=" << sel.game << "\n";
	for (const std::string &mod : sel.mods)
		file << "mod=" << mod << "\n";
	file << "warp=" << (sel.warp ? 1 : 0) << "\n";
	file << "episode=" << sel.episode << "\n";
	file << "map=" << sel.map << "\n";
	file << "skill=" << sel.skill << "\n";
	file << "monsters=" << (int)sel.monsters << "\n";
}

void ReadSelection(Selection &sel, const std::string &key, const std::string &value)
{
	const int number = atoi(value.c_str());
	if (key == "game") sel.game = value;
	else if (key == "mod") sel.mods.push_back(value);
	else if (key == "warp") sel.warp = number != 0;
	else if (key == "episode") sel.episode = Clamp(number, 0, 9);
	else if (key == "map") sel.map = Clamp(number, 1, 99);
	else if (key == "skill") sel.skill = Clamp(number, 1, 5);
	else if (key == "monsters") sel.monsters = (Monsters)Clamp(number, 0, (int)Monsters::Count - 1);
}

} // namespace

void Launcher::Save() const
{
	const std::string path = userRoot + "/config/launcher.cfg";
	std::ofstream file(path, std::ios::trunc);
	if (!file)
		return;
	WriteSelection(file, selection);
	for (int i = 0; i < PresetCount; i++)
	{
		if (!presets[i].used)
			continue;
		file << "[preset " << (i + 1) << "]\n";
		file << "name=" << presets[i].name << "\n";
		WriteSelection(file, presets[i].selection);
	}
	file.close();
	chmod(path.c_str(), 0666);
}

void Launcher::Load()
{
	std::ifstream file(userRoot + "/config/launcher.cfg");
	if (!file)
		return;
	selection = Selection();
	for (Preset &preset : presets)
		preset = Preset();
	Preset *preset = nullptr;
	std::string line;
	while (std::getline(file, line))
	{
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (line.rfind("[preset ", 0) == 0)
		{
			const int index = atoi(line.c_str() + 8) - 1;
			preset = (index >= 0 && index < PresetCount) ? &presets[index] : nullptr;
			if (preset)
				preset->used = true;
			continue;
		}
		const size_t equals = line.find('=');
		if (equals == std::string::npos)
			continue;
		const std::string key = line.substr(0, equals), value = line.substr(equals + 1);
		if (preset == nullptr)
			ReadSelection(selection, key, value);
		else if (key == "name")
			preset->name = value;
		else
			ReadSelection(preset->selection, key, value);
	}
	// episodes have nine maps
	if (selection.episode > 0 && selection.map > 9)
		selection.map = 1;
}

} // namespace dosui
