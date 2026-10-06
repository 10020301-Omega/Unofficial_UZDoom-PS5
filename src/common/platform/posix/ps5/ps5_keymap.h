/*
** ps5_keymap.h
**
** A USB keyboard's keys (HID usage IDs, which is what the console's keyboard
** library reports) as the engine's key codes, its menu and console keys, and
** the characters they type on a US layout. No engine or console headers, so
** ps5/tools/test_keymap.cpp checks it on a PC.
**
** Copyright 2026 PS5-UZDOOM port contributors
** SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <cstdint>

namespace ps5key
{

enum : uint16_t
{
	HID_A = 0x04, HID_Z = 0x1D, HID_1 = 0x1E, HID_0 = 0x27,
	HID_ENTER = 0x28, HID_ESCAPE = 0x29, HID_BACKSPACE = 0x2A, HID_TAB = 0x2B,
	HID_SPACE = 0x2C, HID_CAPSLOCK = 0x39, HID_F1 = 0x3A, HID_F12 = 0x45,
	HID_KP_ENTER = 0x58, HID_KP_1 = 0x59, HID_KP_0 = 0x62, HID_KP_PERIOD = 0x63,
	HID_LCTRL = 0xE0, HID_LSHIFT = 0xE1, HID_LALT = 0xE2, HID_LGUI = 0xE3,
	HID_RCTRL = 0xE4, HID_RSHIFT = 0xE5, HID_RALT = 0xE6, HID_RGUI = 0xE7,
	HID_COUNT = 0xE8
};

// The engine's key code (a DirectInput scan code) for a key; 0 for none.
// The right-hand Shift, Ctrl and Alt are the left-hand ones, as in the
// engine's SDL backend, so one binding covers both.
inline uint8_t ToDIK(uint16_t hid)
{
	static const uint8_t table[HID_COUNT] =
	{
		/* 00 */ 0, 0, 0, 0,
		/* 04 a-z */ 0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
		             0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C,
		/* 1E 1-0 */ 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
		/* 28 */ 0x1C, 0x01, 0x0E, 0x0F, 0x39,       // Enter Esc Backspace Tab Space
		/* 2D */ 0x0C, 0x0D, 0x1A, 0x1B, 0x2B, 0x2B, // - = [ ] backslash, non-US #
		/* 33 */ 0x27, 0x28, 0x29, 0x33, 0x34, 0x35, // ; ' ` , . /
		/* 39 */ 0x3A,                               // Caps Lock
		/* 3A F1-F12 */ 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x57, 0x58,
		/* 46 */ 0xB7, 0x46, 0xC5,                   // Print Screen, Scroll Lock, Pause
		/* 49 */ 0xD2, 0xC7, 0xC9, 0xD3, 0xCF, 0xD1, // Insert Home PgUp Delete End PgDn
		/* 4F */ 0xCD, 0xCB, 0xD0, 0xC8,             // Right Left Down Up
		/* 53 */ 0x45, 0xB5, 0x37, 0x4A, 0x4E, 0x9C, // Num Lock, keypad / * - + Enter
		/* 59 keypad 1-9, 0, . */ 0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D, 0x47, 0x48, 0x49, 0x52, 0x53,
		/* 64 */ 0x56, 0xDD,                         // non-US backslash, Menu
		/* 66..DF: nothing the engine names */
	};
	static const uint8_t modifiers[8] = { 0x1D, 0x2A, 0x38, 0xDB, 0x1D, 0x2A, 0x38, 0xDC };
	if (hid >= HID_LCTRL && hid <= HID_RGUI) return modifiers[hid - HID_LCTRL];
	return hid < HID_COUNT ? table[hid] : 0;
}

// The character a key types on a US layout; 0 for none.
inline char ToChar(uint16_t hid, bool shift, bool capsLock)
{
	if (hid >= HID_A && hid <= HID_Z)
	{
		const char c = char('a' + (hid - HID_A));
		return (shift != capsLock) ? char(c - 32) : c;
	}
	if (hid >= HID_1 && hid <= HID_0)
	{
		return (shift ? "!@#$%^&*()" : "1234567890")[hid - HID_1];
	}
	if (hid >= HID_KP_1 && hid <= HID_KP_0) return "1234567890"[hid - HID_KP_1];
	switch (hid)
	{
	case HID_SPACE: return ' ';
	case 0x2D: return shift ? '_' : '-';
	case 0x2E: return shift ? '+' : '=';
	case 0x2F: return shift ? '{' : '[';
	case 0x30: return shift ? '}' : ']';
	case 0x31: return shift ? '|' : '\\';
	case 0x32: return shift ? '~' : '#';
	case 0x33: return shift ? ':' : ';';
	case 0x34: return shift ? '"' : '\'';
	case 0x35: return shift ? '~' : '`';
	case 0x36: return shift ? '<' : ',';
	case 0x37: return shift ? '>' : '.';
	case 0x38: return shift ? '?' : '/';
	case 0x54: return '/';
	case 0x55: return '*';
	case 0x56: return '-';
	case 0x57: return '+';
	case HID_KP_PERIOD: return '.';
	case 0x64: return shift ? '|' : '\\';
	}
	return 0;
}

// The key as menus and the console name it while they have the keyboard
// (the engine's GK_ codes, or the upper-case character); 0 for none.
inline int ToGUIKey(uint16_t hid)
{
	if (hid >= HID_F1 && hid <= HID_F12) return 14 + (hid - HID_F1); // GK_F1..GK_F12
	switch (hid)
	{
	case HID_ENTER: case HID_KP_ENTER: return 13; // GK_RETURN
	case HID_ESCAPE:    return 27; // GK_ESCAPE
	case HID_BACKSPACE: return 8;  // GK_BACKSPACE
	case HID_TAB:       return 9;  // GK_TAB
	case 0x4A: return 3;  // GK_HOME
	case 0x4B: return 2;  // GK_PGUP
	case 0x4C: return 26; // GK_DEL
	case 0x4D: return 4;  // GK_END
	case 0x4E: return 1;  // GK_PGDN
	case 0x4F: return 6;  // GK_RIGHT
	case 0x50: return 5;  // GK_LEFT
	case 0x51: return 10; // GK_DOWN
	case 0x52: return 11; // GK_UP
	}
	const char c = ToChar(hid, false, false);
	return (c >= 'a' && c <= 'z') ? c - 32 : c;
}

} // namespace ps5key
