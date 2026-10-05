/*
** textscreen.cpp
**
** Copyright 2026 PS5-UZDOOM port contributors
** SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "textscreen.h"

#include <cstring>

namespace dosui
{

namespace
{

const uint8_t Font[256][16] = {
#include "font_cp437.inc"
};

// The VGA's default sixteen colours
const uint8_t Palette[16][3] = {
	{0x00, 0x00, 0x00}, {0x00, 0x00, 0xAA}, {0x00, 0xAA, 0x00}, {0x00, 0xAA, 0xAA},
	{0xAA, 0x00, 0x00}, {0xAA, 0x00, 0xAA}, {0xAA, 0x55, 0x00}, {0xAA, 0xAA, 0xAA},
	{0x55, 0x55, 0x55}, {0x55, 0x55, 0xFF}, {0x55, 0xFF, 0x55}, {0x55, 0xFF, 0xFF},
	{0xFF, 0x55, 0x55}, {0xFF, 0x55, 0xFF}, {0xFF, 0xFF, 0x55}, {0xFF, 0xFF, 0xFF},
};

bool Inside(int x, int y)
{
	return x >= 0 && x < TextScreen::Columns && y >= 0 && y < TextScreen::Rows;
}

} // namespace

void TextScreen::Clear(uint8_t attr, uint8_t ch)
{
	for (Cell &cell : cells)
	{
		cell.ch = ch;
		cell.attr = attr;
	}
}

void TextScreen::Put(int x, int y, uint8_t ch, uint8_t attr)
{
	if (Inside(x, y))
	{
		cells[y * Columns + x].ch = ch;
		cells[y * Columns + x].attr = attr;
	}
}

void TextScreen::Fill(int x, int y, int width, int height, uint8_t ch, uint8_t attr)
{
	for (int row = y; row < y + height; row++)
		for (int column = x; column < x + width; column++)
			Put(column, row, ch, attr);
}

int TextScreen::Print(int x, int y, const std::string &text, uint8_t attr, int maxWidth)
{
	int length = (int)text.size();
	const bool cut = length > maxWidth;
	if (cut)
		length = maxWidth;
	for (int i = 0; i < length; i++)
	{
		uint8_t ch = (uint8_t)text[i];
		if (cut && i >= length - 2)
			ch = '.';
		Put(x + i, y, ch, attr);
	}
	return x + length;
}

void TextScreen::PrintCentered(int x, int y, int width, const std::string &text, uint8_t attr)
{
	int length = (int)text.size();
	if (length > width)
		length = width;
	Print(x + (width - length) / 2, y, text, attr, width);
}

void TextScreen::Recolor(int x, int y, int width, uint8_t attr)
{
	for (int column = x; column < x + width; column++)
		if (Inside(column, y))
			cells[y * Columns + column].attr = attr;
}

void TextScreen::Box(int x, int y, int width, int height, uint8_t attr, bool doubleLine, const std::string &title)
{
	if (width < 2 || height < 2)
		return;
	const uint8_t horizontal = doubleLine ? CH_DHLINE : CH_HLINE;
	const uint8_t vertical = doubleLine ? CH_DVLINE : CH_VLINE;
	Fill(x, y, width, height, ' ', attr);
	for (int column = x + 1; column < x + width - 1; column++)
	{
		Put(column, y, horizontal, attr);
		Put(column, y + height - 1, horizontal, attr);
	}
	for (int row = y + 1; row < y + height - 1; row++)
	{
		Put(x, row, vertical, attr);
		Put(x + width - 1, row, vertical, attr);
	}
	Put(x, y, doubleLine ? CH_DTOP_LEFT : CH_TOP_LEFT, attr);
	Put(x + width - 1, y, doubleLine ? CH_DTOP_RIGHT : CH_TOP_RIGHT, attr);
	Put(x, y + height - 1, doubleLine ? CH_DBOTTOM_LEFT : CH_BOTTOM_LEFT, attr);
	Put(x + width - 1, y + height - 1, doubleLine ? CH_DBOTTOM_RIGHT : CH_BOTTOM_RIGHT, attr);
	if (!title.empty() && width > 6)
		PrintCentered(x + 2, y, width - 4, " " + title + " ", attr);
}

void TextScreen::Shadow(int x, int y, int width, int height)
{
	const uint8_t dark = Attr(DarkGray, Black);
	for (int row = y + 1; row < y + height + 1; row++)
	{
		Recolor(x + width, row, 2, dark);
	}
	Recolor(x + 2, y + height, width, dark);
}

void TextScreen::Rasterize(uint8_t *rgba) const
{
	for (int row = 0; row < Rows; row++)
	{
		for (int column = 0; column < Columns; column++)
		{
			const Cell &cell = cells[row * Columns + column];
			const uint8_t *foreground = Palette[cell.attr & 15];
			const uint8_t *background = Palette[cell.attr >> 4];
			const uint8_t *glyph = Font[cell.ch];
			for (int line = 0; line < CellHeight; line++)
			{
				uint8_t *out = rgba + ((row * CellHeight + line) * PixelWidth + column * CellWidth) * 4;
				const uint8_t bits = glyph[line];
				for (int pixel = 0; pixel < CellWidth; pixel++)
				{
					const uint8_t *color = (bits & (0x80 >> pixel)) ? foreground : background;
					out[0] = color[0];
					out[1] = color[1];
					out[2] = color[2];
					out[3] = 0xFF;
					out += 4;
				}
			}
		}
	}
}

std::string TextScreen::Dump() const
{
	std::string out;
	for (int row = 0; row < Rows; row++)
	{
		std::string line;
		for (int column = 0; column < Columns; column++)
		{
			const uint8_t ch = cells[row * Columns + column].ch;
			char c = (char)ch;
			switch (ch)
			{
			case CH_HLINE: case CH_DHLINE: c = '-'; break;
			case CH_VLINE: case CH_DVLINE: c = '|'; break;
			case CH_TOP_LEFT: case CH_TOP_RIGHT: case CH_BOTTOM_LEFT: case CH_BOTTOM_RIGHT:
			case CH_DTOP_LEFT: case CH_DTOP_RIGHT: case CH_DBOTTOM_LEFT: case CH_DBOTTOM_RIGHT: c = '+'; break;
			case CH_SHADE_LIGHT: case CH_SHADE_MEDIUM: case CH_SHADE_DARK: c = ':'; break;
			case CH_BLOCK: c = '#'; break;
			case CH_ARROW_UP: c = '^'; break;
			case CH_ARROW_DOWN: c = 'v'; break;
			case CH_ARROW_RIGHT: c = '>'; break;
			case CH_ARROW_LEFT: c = '<'; break;
			case CH_CHECK: c = 'x'; break;
			case CH_BULLET: c = '*'; break;
			default: if (ch < 0x20 || ch > 0x7E) c = '?'; break;
			}
			line += c;
		}
		while (!line.empty() && line.back() == ' ')
			line.pop_back();
		out += line + "\n";
	}
	return out;
}

} // namespace dosui
