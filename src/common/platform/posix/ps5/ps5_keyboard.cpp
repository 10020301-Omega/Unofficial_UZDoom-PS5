/*
** ps5_keyboard.cpp
**
** A USB keyboard, through the console's keyboard library (libSceKeyboard).
** It reports which keys are down as USB HID usage IDs; this turns the changes
** into the engine's key events: game keys while the game has the keyboard,
** menu/console keys and typed characters while a menu or the console does.
**
** The library has no public documentation. The layout of its state record
** follows what PS4/PS5 emulators implement for it, and every answer it gives
** at start-up is printed to the log, so a console that disagrees can be told
** from the log alone. Nothing here is needed to play: with no keyboard, or
** with a library that refuses, the pad works as before.
**
** Copyright 2026 PS5-UZDOOM port contributors
** SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <cstdint>
#include <cstring>

#include "c_buttons.h"
#include "c_cvars.h"
#include "d_eventbase.h"
#include "d_gui.h"
#include "i_time.h"
#include "keydef.h"
#include "printf.h"

#include "ps5_keymap.h"

extern "C"
{
int sceUserServiceGetInitialUser(int32_t *user_id);
int sceKeyboardInit(void);
int sceKeyboardOpen(int32_t user_id, int32_t type, int32_t index, const void *param);
int sceKeyboardReadState(int32_t handle, void *data);
int sceKeyboardRead(int32_t handle, void *data, int32_t count);
int sceKeyboardClose(int32_t handle);
int sceSysmoduleLoadModuleInternal(uint32_t id);
}

// The keyboard library is not among the modules a title starts with. Until it
// is loaded its functions are null addresses in this program's import table,
// and calling one is a crash (the first build with a keyboard did just that).
constexpr uint32_t SysmoduleInternalKeyboard = 0x80000008;

// The address the import table holds for a function, read where the compiler
// cannot assume it is set. This file is compiled with -fno-plt, so the calls
// go through the same table entry this reads.
template<class F> static uintptr_t Imported(F *function)
{
	volatile uintptr_t address = reinterpret_cast<uintptr_t>(function);
	return address;
}

extern bool GUICapture;

CVAR(Bool, use_keyboard, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

namespace
{

// One record of the keyboard's state, 96 bytes.
struct KeyboardData
{
	uint64_t timestamp;
	uint64_t intercepted;
	uint8_t connected;
	uint8_t pad[3];
	int32_t length;       // how many of keyCode are set
	uint32_t led;
	uint32_t modifiers;   // the HID modifier byte: bit n is usage 0xE0 + n
	uint16_t keyCode[16];
	uint8_t reserved[32];
};
static_assert(sizeof(KeyboardData) == 96, "the library writes 96 bytes a record");
static_assert(offsetof(KeyboardData, length) == 20 && offsetof(KeyboardData, keyCode) == 32, "record layout");

constexpr int MaxRecords = 16;
constexpr uint64_t RepeatDelay = 400, RepeatRate = 40; // ms, while a menu or the console has the keyboard

int Handle = -1;
bool Down[ps5key::HID_COUNT];
bool CapsLock;
bool SeenConnected;
int ReadErrors;
bool UseHistory;         // ReadState refused: take the newest record from Read
uint16_t RepeatKey;
uint64_t RepeatAt;

bool IsDown(uint16_t a, uint16_t b) { return Down[a] || Down[b]; }

int Modifiers()
{
	using namespace ps5key;
	return (IsDown(HID_LSHIFT, HID_RSHIFT) ? GKM_SHIFT : 0) |
		(IsDown(HID_LCTRL, HID_RCTRL) ? GKM_CTRL : 0) |
		(IsDown(HID_LALT, HID_RALT) ? GKM_ALT : 0);
}

void PostGUIKey(uint16_t hid, int subtype)
{
	using namespace ps5key;
	const bool shift = IsDown(HID_LSHIFT, HID_RSHIFT);
	const int key = ToGUIKey(hid);
	if (key != 0)
	{
		event_t event = {};
		event.type = EV_GUI_Event;
		event.subtype = subtype;
		event.data1 = key;
		event.data3 = Modifiers();
		D_PostEvent(&event);
	}
	if (subtype == EV_GUI_KeyUp || IsDown(HID_LCTRL, HID_RCTRL)) return;
	const char c = ToChar(hid, shift, CapsLock);
	if (c != 0)
	{
		event_t event = {};
		event.type = EV_GUI_Event;
		event.subtype = EV_GUI_Char;
		event.data1 = c;
		event.data2 = IsDown(HID_LALT, HID_RALT);
		D_PostEvent(&event);
	}
}

void KeyChanged(uint16_t hid, bool down)
{
	using namespace ps5key;
	if (hid >= HID_COUNT || Down[hid] == down) return;
	Down[hid] = down;
	if (hid == HID_CAPSLOCK && down) CapsLock = !CapsLock;

	if (GUICapture)
	{
		PostGUIKey(hid, down ? EV_GUI_KeyDown : EV_GUI_KeyUp);
		if (down && ToDIK(hid) != 0 && !(hid >= HID_LCTRL && hid <= HID_RGUI))
		{
			RepeatKey = hid;
			RepeatAt = I_msTime() + RepeatDelay;
		}
		else if (!down && hid == RepeatKey)
		{
			RepeatKey = 0;
		}
		return;
	}

	RepeatKey = 0;
	const int dik = ToDIK(hid);
	if (dik == 0) return;
	// Both Shifts (and so on) are one key to the engine: it stays down until
	// the last of the pair is let go.
	if (hid >= HID_LCTRL && hid <= HID_RGUI && hid != HID_LGUI && hid != HID_RGUI)
	{
		const uint16_t other = hid < HID_RCTRL ? hid + 4 : hid - 4;
		if (Down[other]) return;
	}
	event_t event = {};
	event.type = down ? EV_KeyDown : EV_KeyUp;
	event.data1 = dik;
	event.data2 = ToChar(hid, false, false);
	event.data3 = Modifiers();
	D_PostEvent(&event);
}

void ReleaseAll()
{
	for (uint16_t hid = 0; hid < ps5key::HID_COUNT; hid++)
	{
		if (Down[hid]) KeyChanged(hid, false);
	}
	RepeatKey = 0;
}

void Apply(const KeyboardData &data)
{
	bool now[ps5key::HID_COUNT] = {};
	int count = data.length;
	if (count < 0) count = 0;
	if (count > 16) count = 16;
	for (int i = 0; i < count; i++)
	{
		if (data.keyCode[i] >= 4 && data.keyCode[i] < ps5key::HID_LCTRL) now[data.keyCode[i]] = true;
	}
	for (int bit = 0; bit < 8; bit++)
	{
		if (data.modifiers & (1u << bit)) now[ps5key::HID_LCTRL + bit] = true;
	}
	// Modifiers first when going down and last when coming up, so that a
	// shifted key is seen with its Shift.
	for (uint16_t hid = ps5key::HID_LCTRL; hid < ps5key::HID_COUNT; hid++)
	{
		if (now[hid] && !Down[hid]) KeyChanged(hid, true);
	}
	for (uint16_t hid = 0; hid < ps5key::HID_LCTRL; hid++)
	{
		if (now[hid] != Down[hid]) KeyChanged(hid, now[hid]);
	}
	for (uint16_t hid = ps5key::HID_LCTRL; hid < ps5key::HID_COUNT; hid++)
	{
		if (!now[hid] && Down[hid]) KeyChanged(hid, false);
	}
}

} // namespace

void PS5_KeyboardOpen()
{
	if (Handle >= 0) return;
	int32_t user = -1;
	if (Imported(&sceSysmoduleLoadModuleInternal) == 0 || Imported(&sceUserServiceGetInitialUser) == 0)
	{
		Printf("Keyboard: the console's module loader is not available to the title; only the controller will work.\n");
		return;
	}
	const int userResult = sceUserServiceGetInitialUser(&user);
	const int load = sceSysmoduleLoadModuleInternal(SysmoduleInternalKeyboard);
	const uintptr_t imports[] = { Imported(&sceKeyboardInit), Imported(&sceKeyboardOpen),
		Imported(&sceKeyboardReadState), Imported(&sceKeyboardRead), Imported(&sceKeyboardClose) };
	bool bound = true;
	for (uintptr_t address : imports) bound = bound && address != 0;
	Printf("Keyboard: module load 0x%08x, functions %s (init at %p)\n",
		(unsigned)load, bound ? "bound" : "NOT bound", (void *)imports[0]);
	if (!bound)
	{
		Printf("Keyboard: the console did not provide its keyboard library; only the controller will work.\n");
		return;
	}
	const int init = sceKeyboardInit();
	int open = sceKeyboardOpen(user, 0, 0, nullptr);
	int tried = user;
	if (open < 0)
	{
		// Some input libraries only open for the system's own user.
		tried = 0xFF;
		open = sceKeyboardOpen(tried, 0, 0, nullptr);
	}
	Printf("Keyboard: user %d (0x%08x), init 0x%08x, open(user %d) 0x%08x\n",
		(int)user, (unsigned)userResult, (unsigned)init, tried, (unsigned)open);
	if (open < 0)
	{
		Printf("Keyboard: the console refused it; only the controller will work.\n");
		return;
	}
	Handle = open;
}

void PS5_KeyboardClose()
{
	if (Handle >= 0)
	{
		sceKeyboardClose(Handle);
		Handle = -1;
	}
}

void PS5_KeyboardPoll()
{
	if (Handle < 0) return;
	if (!use_keyboard)
	{
		ReleaseAll();
		return;
	}

	KeyboardData records[MaxRecords];
	memset(records, 0, sizeof(records));
	const KeyboardData *data = &records[0];
	int result;
	if (!UseHistory)
	{
		result = sceKeyboardReadState(Handle, &records[0]);
		if (result < 0 && ++ReadErrors == 1)
		{
			Printf("Keyboard: reading its state failed, 0x%08x; trying its history instead.\n", (unsigned)result);
			UseHistory = true;
			return;
		}
	}
	else
	{
		result = sceKeyboardRead(Handle, records, MaxRecords);
		if (result < 0)
		{
			if (++ReadErrors == 2)
			{
				Printf("Keyboard: reading its history failed too, 0x%08x; giving up on it.\n", (unsigned)result);
				ReleaseAll();
				PS5_KeyboardClose();
			}
			return;
		}
		if (result == 0) goto repeat; // nothing new
		for (int i = 1; i < result && i < MaxRecords; i++)
		{
			if (records[i].timestamp >= data->timestamp) data = &records[i];
		}
	}

	if (!data->connected)
	{
		if (SeenConnected)
		{
			Printf("Keyboard: unplugged.\n");
			SeenConnected = false;
		}
		ReleaseAll();
		return;
	}
	if (!SeenConnected)
	{
		Printf("Keyboard: connected.\n");
		SeenConnected = true;
	}
	Apply(*data);

repeat:
	if (RepeatKey != 0)
	{
		if (!GUICapture || !Down[RepeatKey])
		{
			RepeatKey = 0;
		}
		else if (I_msTime() >= RepeatAt)
		{
			PostGUIKey(RepeatKey, EV_GUI_KeyRepeat);
			RepeatAt = I_msTime() + RepeatRate;
		}
	}
}
