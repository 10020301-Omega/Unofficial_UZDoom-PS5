// PS5-UZDOOM - checks ps5_keymap.h on a PC.
//   c++ -std=c++17 -I src/common/platform/posix/ps5 ps5/tools/test_keymap.cpp -o test_keymap && ./test_keymap
// Copyright 2026 PS5-UZDOOM port contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include "ps5_keymap.h"
using namespace ps5key;
static int failed;
#define CHECK(x) do { if (!(x)) { printf("FAIL %s\n", #x); failed++; } } while (0)
int main()
{
	CHECK(ToDIK(0x1A) == 0x11);  // W
	CHECK(ToDIK(0x04) == 0x1E);  // A
	CHECK(ToDIK(0x16) == 0x1F);  // S
	CHECK(ToDIK(0x07) == 0x20);  // D
	CHECK(ToDIK(0x1D) == 0x2C);  // Z
	CHECK(ToDIK(0x1E) == 0x02 && ToDIK(0x27) == 0x0B); // 1, 0
	CHECK(ToDIK(HID_ENTER) == 0x1C && ToDIK(HID_ESCAPE) == 0x01 && ToDIK(HID_SPACE) == 0x39);
	CHECK(ToDIK(0x35) == 0x29);  // ` : the console key
	CHECK(ToDIK(0x38) == 0x35);  // /
	CHECK(ToDIK(HID_CAPSLOCK) == 0x3A);
	CHECK(ToDIK(HID_F1) == 0x3B && ToDIK(0x43) == 0x44 && ToDIK(0x44) == 0x57 && ToDIK(HID_F12) == 0x58);
	CHECK(ToDIK(0x46) == 0xB7 && ToDIK(0x48) == 0xC5);
	CHECK(ToDIK(0x49) == 0xD2 && ToDIK(0x4E) == 0xD1);
	CHECK(ToDIK(0x4F) == 0xCD && ToDIK(0x50) == 0xCB && ToDIK(0x51) == 0xD0 && ToDIK(0x52) == 0xC8);
	CHECK(ToDIK(0x53) == 0x45 && ToDIK(HID_KP_ENTER) == 0x9C);
	CHECK(ToDIK(HID_KP_1) == 0x4F && ToDIK(0x61) == 0x49 && ToDIK(HID_KP_0) == 0x52 && ToDIK(HID_KP_PERIOD) == 0x53);
	CHECK(ToDIK(0x64) == 0x56 && ToDIK(0x65) == 0xDD && ToDIK(0x66) == 0);
	CHECK(ToDIK(HID_LCTRL) == 0x1D && ToDIK(HID_RCTRL) == 0x1D);
	CHECK(ToDIK(HID_LSHIFT) == 0x2A && ToDIK(HID_RSHIFT) == 0x2A);
	CHECK(ToDIK(HID_LALT) == 0x38 && ToDIK(HID_RALT) == 0x38);
	CHECK(ToDIK(0x1FF) == 0 && ToDIK(0) == 0);
	CHECK(ToChar(0x04, false, false) == 'a' && ToChar(0x04, true, false) == 'A');
	CHECK(ToChar(0x04, false, true) == 'A' && ToChar(0x04, true, true) == 'a');
	CHECK(ToChar(0x1E, false, false) == '1' && ToChar(0x1E, true, false) == '!');
	CHECK(ToChar(0x27, false, true) == '0' && ToChar(0x27, true, false) == ')');
	CHECK(ToChar(0x35, false, false) == '`' && ToChar(0x38, true, false) == '?');
	CHECK(ToChar(HID_SPACE, false, false) == ' ' && ToChar(HID_KP_0, false, false) == '0');
	CHECK(ToChar(HID_ENTER, false, false) == 0 && ToChar(HID_F1, false, false) == 0);
	CHECK(ToGUIKey(HID_ENTER) == 13 && ToGUIKey(HID_ESCAPE) == 27 && ToGUIKey(HID_BACKSPACE) == 8);
	CHECK(ToGUIKey(0x52) == 11 && ToGUIKey(0x51) == 10 && ToGUIKey(0x50) == 5 && ToGUIKey(0x4F) == 6);
	CHECK(ToGUIKey(HID_F1) == 14 && ToGUIKey(HID_F12) == 25);
	CHECK(ToGUIKey(0x04) == 'A' && ToGUIKey(0x35) == '`' && ToGUIKey(0x1E) == '1');
	CHECK(ToGUIKey(HID_LSHIFT) == 0);
	printf(failed ? "%d failed\n" : "keymap: all checks pass\n", failed);
	return failed != 0;
}
