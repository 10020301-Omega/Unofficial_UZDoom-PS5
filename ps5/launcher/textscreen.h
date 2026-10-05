/*
** textscreen.h
**
** An 80 x 25 text mode screen, as a DOS program had it: a character from
** code page 437 and a colour attribute in every cell, drawn with an 8 x 16
** font in the VGA's sixteen colours.
**
** Copyright 2026 PS5-UZDOOM port contributors
** SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <cstdint>
#include <string>

namespace dosui
{

enum Color : uint8_t
{
	Black, Blue, Green, Cyan, Red, Magenta, Brown, LightGray,
	DarkGray, LightBlue, LightGreen, LightCyan, LightRed, LightMagenta, Yellow, White
};

// An attribute byte: foreground in the low four bits, background in the high.
constexpr uint8_t Attr(Color foreground, Color background)
{
	return (uint8_t)(foreground | (background << 4));
}

// Code page 437's box characters
enum : uint8_t
{
	CH_SHADE_LIGHT = 0xB0, CH_SHADE_MEDIUM = 0xB1, CH_SHADE_DARK = 0xB2,
	CH_VLINE = 0xB3, CH_HLINE = 0xC4,
	CH_TOP_LEFT = 0xDA, CH_TOP_RIGHT = 0xBF, CH_BOTTOM_LEFT = 0xC0, CH_BOTTOM_RIGHT = 0xD9,
	CH_DVLINE = 0xBA, CH_DHLINE = 0xCD,
	CH_DTOP_LEFT = 0xC9, CH_DTOP_RIGHT = 0xBB, CH_DBOTTOM_LEFT = 0xC8, CH_DBOTTOM_RIGHT = 0xBC,
	CH_BLOCK = 0xDB, CH_ARROW_UP = 0x18, CH_ARROW_DOWN = 0x19,
	CH_ARROW_RIGHT = 0x10, CH_ARROW_LEFT = 0x11, CH_CHECK = 0xFB, CH_BULLET = 0x07,
};

class TextScreen
{
public:
	static constexpr int Columns = 80;
	static constexpr int Rows = 25;
	static constexpr int CellWidth = 8;
	static constexpr int CellHeight = 16;
	static constexpr int PixelWidth = Columns * CellWidth;   // 640
	static constexpr int PixelHeight = Rows * CellHeight;    // 400

	struct Cell
	{
		uint8_t ch = ' ';
		uint8_t attr = 0x07;
	};

	void Clear(uint8_t attr, uint8_t ch = ' ');
	void Put(int x, int y, uint8_t ch, uint8_t attr);
	void Fill(int x, int y, int width, int height, uint8_t ch, uint8_t attr);
	// Text in code page 437; returns the column after the last character.
	// What does not fit in maxWidth cells is cut and ends in "..".
	int Print(int x, int y, const std::string &text, uint8_t attr, int maxWidth = Columns);
	void PrintCentered(int x, int y, int width, const std::string &text, uint8_t attr);
	// Only the colours of a run of cells: a menu's highlight bar.
	void Recolor(int x, int y, int width, uint8_t attr);
	// A framed, filled box with an optional title in its top edge.
	void Box(int x, int y, int width, int height, uint8_t attr, bool doubleLine, const std::string &title = "");
	// The drop shadow DOS dialogs had: the cells right of and below a box go dark.
	void Shadow(int x, int y, int width, int height);

	const Cell &At(int x, int y) const { return cells[y * Columns + x]; }

	// 640 x 400 pixels, four bytes each: red, green, blue, alpha.
	void Rasterize(uint8_t *rgba) const;
	// The screen as lines of text, for tests: box characters become ASCII.
	std::string Dump() const;

private:
	Cell cells[Columns * Rows];
};

} // namespace dosui
