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

#include <cerrno>
#include <cstdint>
#include <cstring>

#include <fcntl.h>
#include <unistd.h>

#include <string>

#include "c_buttons.h"
#include "c_cvars.h"
#include "d_eventbase.h"
#include "d_gui.h"
#include "i_time.h"
#include "keydef.h"
#include "printf.h"

#include "ps5_keymap.h"
#include "ps5_paths.h"

extern "C"
{
int sceUserServiceGetInitialUser(int32_t *user_id);
int sceKeyboardInit(void);
int sceKeyboardOpen(int32_t user_id, int32_t type, int32_t index, const void *param);
int sceKeyboardReadState(int32_t handle, void *data);
int sceKeyboardRead(int32_t handle, void *data, int32_t count);
int sceKeyboardClose(int32_t handle);
int sceSysmoduleLoadModuleInternal(uint32_t id);
int sceSysmoduleLoadModule(uint32_t id);
int sceKernelLoadStartModule(const char *name, size_t argc, const void *argv, uint32_t flags, void *option, int *result);
int sceKernelDlsym(int handle, const char *symbol, void **address);
struct KernelModuleInfo
{
	size_t size;
	char name[256];
	struct { void *address; uint32_t size; int32_t protection; } segments[4];
	uint32_t segmentCount;
	uint8_t fingerprint[20];
};
int sceKernelGetModuleInfo(int handle, KernelModuleInfo *info);

// The text-input library's keyboard: events instead of a state to poll.
struct ImeEvent;
typedef void (*ImeEventHandler)(void *arg, const ImeEvent *event);
struct ImeKeyboardParam
{
	uint32_t option;
	int8_t reserved1[4];
	void *arg;
	ImeEventHandler handler;
	int8_t reserved2[8];
};
int sceImeKeyboardOpen(int32_t user_id, const ImeKeyboardParam *param);
int sceImeKeyboardClose(int32_t user_id);
int sceImeUpdate(ImeEventHandler handler);
}

struct ImeEvent
{
	int32_t id;
	int32_t pad;
	// The part of the event's union a key event fills in.
	uint16_t keycode;     // HID usage
	uint16_t character;
	uint32_t status;      // bit 0: keycode is valid
	uint32_t type;
	int32_t userId;
	uint32_t resourceId;
	uint32_t pad2;
	uint64_t timestamp;
};
enum
{
	ImeKeyboardEventOpen = 256, ImeKeyboardEventKeyDown = 257, ImeKeyboardEventKeyUp = 258,
	ImeKeyboardEventRepeat = 259, ImeKeyboardEventConnection = 260,
	ImeKeyboardEventDisconnection = 261, ImeKeyboardEventAbort = 262,
};
constexpr uint32_t SysmoduleLibIme = 0x0095;

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

namespace
{

enum class Route { None, Keyboard, Ime };
Route Active = Route::None;
int32_t ImeUser = -1;
int ImeEvents;       // events of any kind seen, for the log
int ImeUpdateErrors;

bool KeyboardBound()
{
	return Imported(&sceKeyboardInit) != 0 && Imported(&sceKeyboardOpen) != 0 &&
		Imported(&sceKeyboardReadState) != 0 && Imported(&sceKeyboardRead) != 0 &&
		Imported(&sceKeyboardClose) != 0;
}

bool ImeBound()
{
	return Imported(&sceImeKeyboardOpen) != 0 && Imported(&sceImeKeyboardClose) != 0 &&
		Imported(&sceImeUpdate) != 0;
}

void OnImeEvent(void *, const ImeEvent *event)
{
	if (event == nullptr) return;
	if (ImeEvents < 8)
	{
		// The first few, whatever they are: enough to see what the console sends.
		Printf("Keyboard: event %d, key 0x%02x, status 0x%x\n", (int)event->id, (unsigned)event->keycode, (unsigned)event->status);
	}
	ImeEvents++;
	switch (event->id)
	{
	case ImeKeyboardEventKeyDown:
	case ImeKeyboardEventKeyUp:
		if ((event->status & 1) && use_keyboard) KeyChanged(event->keycode, event->id == ImeKeyboardEventKeyDown);
		break;
	case ImeKeyboardEventDisconnection:
	case ImeKeyboardEventAbort:
		ReleaseAll();
		break;
	}
}

bool OpenKeyboardRoute(int32_t user)
{
	const int init = sceKeyboardInit();
	int open = sceKeyboardOpen(user, 0, 0, nullptr);
	int tried = user;
	if (open < 0)
	{
		tried = 0xFF; // the system's own user
		open = sceKeyboardOpen(tried, 0, 0, nullptr);
	}
	Printf("Keyboard: libSceKeyboard init 0x%08x, open(user %d) 0x%08x\n", (unsigned)init, tried, (unsigned)open);
	if (open < 0) return false;
	Handle = open;
	return true;
}

bool OpenImeRoute(int32_t user)
{
	ImeKeyboardParam param;
	memset(&param, 0, sizeof(param));
	param.handler = OnImeEvent;
	int open = sceImeKeyboardOpen(user, &param);
	int tried = user;
	if (open < 0)
	{
		tried = 254; // every user
		open = sceImeKeyboardOpen(tried, &param);
	}
	Printf("Keyboard: libSceIme open(user %d) 0x%08x\n", tried, (unsigned)open);
	if (open < 0) return false;
	ImeUser = tried;
	return true;
}

} // namespace

//==========================================================================
//
// The helper payload (ps5/kbd-helper): it runs outside the title, where the
// keyboard library can be had, and writes the keyboard's states into a file
// in the user's folder, which this reads. A file, because the console
// refuses a title a loopback socket (bind answers EACCES).
//
//==========================================================================

namespace
{

constexpr int HelperRing = 64;
constexpr uint64_t HelperTimeout = 2000; // ms without a heartbeat: it is gone

struct HelperState
{
	char magic[4]; // "UZK1"
	uint8_t connected, modifiers, count, zero;
	uint16_t keys[16];
};
static_assert(sizeof(HelperState) == 40, "the helper writes 40 bytes a state");

struct HelperFile
{
	char magic[4]; // "UZKF"
	uint32_t version;
	uint64_t count; // states written so far; state n is in slot n % 64
	uint64_t beat;  // changes while the helper lives
	HelperState ring[HelperRing];
};
static_assert(offsetof(HelperFile, ring) == 24, "the file's header is 24 bytes");

int HelperFd = -1;
bool HelperAlive, HelperEverSeen;
uint64_t HelperBeat, HelperBeatAt, HelperCount, HelperRetryAt;

// True while the helper is the one to believe.
bool PollHelper()
{
	const uint64_t now = I_msTime();
	if (HelperFd < 0)
	{
		if (now < HelperRetryAt) return false;
		HelperRetryAt = now + 1000;
		const std::string path = std::string(PS5_UserRoot()) + "/kbd-state.bin";
		HelperFd = open(path.c_str(), O_RDONLY);
		if (HelperFd < 0) return false;
		HelperBeat = 0;
		HelperBeatAt = 0;
	}

	HelperFile file;
	const ssize_t size = pread(HelperFd, &file, sizeof(file), 0);
	if (size != (ssize_t)sizeof(file) || memcmp(file.magic, "UZKF", 4) != 0 || file.version != 1)
	{
		// Not there yet, or being made again by a helper just started.
		if (size < 0 || (HelperAlive && now - HelperBeatAt > HelperTimeout))
		{
			close(HelperFd);
			HelperFd = -1;
		}
	}
	else if (file.beat != HelperBeat)
	{
		const bool first = HelperBeatAt == 0;
		HelperBeat = file.beat;
		HelperBeatAt = now;
		if (first)
		{
			// The file may be left from before the console was restarted:
			// believe it only once its heartbeat has been seen to move.
			HelperCount = file.count;
		}
		else
		{
			if (!HelperAlive)
			{
				Printf("Keyboard: the helper payload is running.\n");
				HelperAlive = true;
				HelperEverSeen = true;
				// Start from the newest state only.
				HelperCount = file.count > 0 ? file.count - 1 : 0;
			}
			if (file.count < HelperCount) HelperCount = 0; // a new helper began again
			if (file.count - HelperCount > HelperRing) HelperCount = file.count - HelperRing;
			for (; HelperCount < file.count; HelperCount++)
			{
				const HelperState &state = file.ring[HelperCount % HelperRing];
				if (memcmp(state.magic, "UZK1", 4) != 0 || !use_keyboard) continue;
				KeyboardData data;
				memset(&data, 0, sizeof(data));
				data.connected = state.connected;
				data.modifiers = state.modifiers;
				data.length = state.count > 16 ? 16 : state.count;
				memcpy(data.keyCode, state.keys, sizeof(data.keyCode));
				if (data.connected) Apply(data); else ReleaseAll();
			}
		}
	}

	if (HelperAlive && now - HelperBeatAt > HelperTimeout)
	{
		Printf("Keyboard: the helper payload went quiet.\n");
		HelperAlive = false;
		HelperBeatAt = 0;
		ReleaseAll();
		// It may come back with a new file under the same name.
		close(HelperFd);
		HelperFd = -1;
	}
	return HelperAlive;
}

} // namespace

void PS5_KeyboardOpen()
{
	Printf("Keyboard: watching for the helper payload's file, %s/kbd-state.bin\n", PS5_UserRoot());
	if (Active != Route::None) return;
	if (Imported(&sceSysmoduleLoadModuleInternal) == 0 || Imported(&sceSysmoduleLoadModule) == 0 ||
		Imported(&sceKernelLoadStartModule) == 0 || Imported(&sceUserServiceGetInitialUser) == 0)
	{
		Printf("Keyboard: the console's module loader is not available to the title; only the controller will work.\n");
		return;
	}
	int32_t user = -1;
	const int userResult = sceUserServiceGetInitialUser(&user);
	Printf("Keyboard: user %d (0x%08x); at start libSceKeyboard %s, libSceIme %s\n", (int)user, (unsigned)userResult,
		KeyboardBound() ? "bound" : "not bound", ImeBound() ? "bound" : "not bound");

	// Neither library is among the modules a title starts with. Ask for each
	// in the ways there are, and say what each answered and whether the
	// functions appeared: the log of one run then shows which way works.
	if (!ImeBound())
	{
		const int load = sceSysmoduleLoadModule(SysmoduleLibIme);
		Printf("Keyboard: libSceIme by id 0x%08x -> %s\n", (unsigned)load, ImeBound() ? "bound" : "not bound");
	}
	if (!ImeBound())
	{
		const int load = sceKernelLoadStartModule("libSceIme.sprx", 0, nullptr, 0, nullptr, nullptr);
		Printf("Keyboard: libSceIme by name 0x%08x -> %s\n", (unsigned)load, ImeBound() ? "bound" : "not bound");
		// The console loaded it and did not connect it. Ask where it is and
		// whether a function in it can be looked up by hand: facts for the
		// log, nothing is called through what this finds.
		if (load > 0 && Imported(&sceKernelGetModuleInfo) != 0 && Imported(&sceKernelDlsym) != 0)
		{
			KernelModuleInfo info;
			memset(&info, 0, sizeof(info));
			info.size = sizeof(info);
			const int got = sceKernelGetModuleInfo(load, &info);
			Printf("Keyboard: module info 0x%08x, name '%.32s', %u segments\n", (unsigned)got, info.name, (unsigned)info.segmentCount);
			for (uint32_t i = 0; got == 0 && i < info.segmentCount && i < 4; i++)
			{
				Printf("Keyboard:   segment %u at %p, %u bytes, protection %d\n", (unsigned)i,
					info.segments[i].address, (unsigned)info.segments[i].size, (int)info.segments[i].protection);
			}
			static const char *const names[] = { "sceImeKeyboardOpen", "sceImeUpdate", "sceImeKeyboardClose",
				"module_start" };
			for (const char *name : names)
			{
				void *address = nullptr;
				const int found = sceKernelDlsym(load, name, &address);
				Printf("Keyboard:   lookup '%s' 0x%08x -> %p\n", name, (unsigned)found, address);
			}
		}
	}
	if (!KeyboardBound())
	{
		const int load = sceSysmoduleLoadModuleInternal(SysmoduleInternalKeyboard);
		Printf("Keyboard: libSceKeyboard by id 0x%08x -> %s\n", (unsigned)load, KeyboardBound() ? "bound" : "not bound");
	}
	if (!KeyboardBound())
	{
		const int load = sceKernelLoadStartModule("libSceKeyboard.sprx", 0, nullptr, 0, nullptr, nullptr);
		Printf("Keyboard: libSceKeyboard by name 0x%08x -> %s\n", (unsigned)load, KeyboardBound() ? "bound" : "not bound");
	}

	// The text-input library first: it is the one games are meant to use.
	if (ImeBound() && OpenImeRoute(user)) Active = Route::Ime;
	else if (KeyboardBound() && OpenKeyboardRoute(user)) Active = Route::Keyboard;

	if (Active == Route::None)
	{
		Printf("Keyboard: no way to read one on this console yet; only the controller will work.\n");
	}
	else
	{
		Printf("Keyboard: reading through %s.\n", Active == Route::Ime ? "libSceIme" : "libSceKeyboard");
	}
}

void PS5_KeyboardClose()
{
	if (Active == Route::Keyboard && Handle >= 0) sceKeyboardClose(Handle);
	if (Active == Route::Ime) sceImeKeyboardClose(ImeUser);
	Handle = -1;
	Active = Route::None;
	if (HelperFd >= 0) close(HelperFd);
	HelperFd = -1;
}

static void Repeat()
{
	if (RepeatKey == 0) return;
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

void PS5_KeyboardPoll()
{
	if (PollHelper())
	{
		if (!use_keyboard) ReleaseAll();
		Repeat();
		return;
	}
	if (Active == Route::None) return;
	if (!use_keyboard)
	{
		ReleaseAll();
		if (Active == Route::Ime) sceImeUpdate(OnImeEvent); // keep its queue empty
		return;
	}
	if (Active == Route::Ime)
	{
		const int result = sceImeUpdate(OnImeEvent);
		if (result < 0 && ++ImeUpdateErrors == 1)
		{
			Printf("Keyboard: libSceIme update failed, 0x%08x\n", (unsigned)result);
		}
		Repeat();
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
	Repeat();
}
