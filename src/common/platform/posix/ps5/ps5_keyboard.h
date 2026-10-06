/*
** ps5_keyboard.h - the USB keyboard and mouse (ps5_keyboard.cpp).
**
** Copyright 2026 PS5-UZDOOM port contributors
** SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

void PS5_KeyboardOpen();
void PS5_KeyboardClose();
// Reads the keyboard and mouse and posts the engine's events.
void PS5_KeyboardPoll();

// While the launcher runs there is no engine to post events to: the keyboard
// is then read as the launcher's own buttons.
struct PS5KeyboardNav
{
	bool up, down, left, right;               // held
	bool accept, back, square, triangle;      // pressed since the last call
	bool pageUp, pageDown, start;
};
void PS5_KeyboardLauncherMode(bool on);
PS5KeyboardNav PS5_KeyboardLauncherRead();
