/*
** ps5_input.cpp
**
** Input on the PS5: the DualSense, nothing else. Each signed-in user's
** controller is a joystick device; buttons arrive as the engine's gamepad
** keys and the sticks and triggers as its gamepad axes, so the stock
** bindings, menus and joystick options work unchanged.
**
**---------------------------------------------------------------------------
**
** Copyright 2026 PS5-UZDOOM port contributors
** Based on posix/sdl/i_input.cpp and posix/sdl/i_joystick.cpp:
** Copyright 1998-2016 Marisa Heit
** Copyright 2005-2016 Christoph Oelckers
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
*/

#include <cmath>
#include <stdio.h>

#include "basics.h"
#include "c_buttons.h"
#include "c_cvars.h"
#include "cmdlib.h"
#include "d_eventbase.h"
#include "d_gui.h"
#include "i_input.h"
#include "i_interface.h"
#include "keydef.h"
#include "m_haptics.h"
#include "m_joy.h"
#include "printf.h"
#include "ps5_keyboard.h"
#include "tarray.h"
#include "zstring.h"

extern "C"
{
#include "console/platform.h"
}

bool GUICapture;


// There is no mouse; the cvar stays because menus and the config file name it.
CVAR (Bool, use_mouse, false, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)

EXTERN_CVAR(Bool, use_joystick)

extern int WaitingForKey;

//==========================================================================
//
// The pad's buttons as the engine's gamepad keys
//
//==========================================================================

static const struct { uint32_t button; int key; } PadButtons[] =
{
	{ PAD_CROSS,     KEY_PAD_A },
	{ PAD_CIRCLE,    KEY_PAD_B },
	{ PAD_SQUARE,    KEY_PAD_X },
	{ PAD_TRIANGLE,  KEY_PAD_Y },
	{ PAD_TOUCH_PAD, KEY_PAD_BACK },
	{ PAD_OPTIONS,   KEY_PAD_START },
	{ PAD_L3,        KEY_PAD_LTHUMB },
	{ PAD_R3,        KEY_PAD_RTHUMB },
	{ PAD_L1,        KEY_PAD_LSHOULDER },
	{ PAD_R1,        KEY_PAD_RSHOULDER },
	{ PAD_UP,        KEY_PAD_DPAD_UP },
	{ PAD_DOWN,      KEY_PAD_DPAD_DOWN },
	{ PAD_LEFT,      KEY_PAD_DPAD_LEFT },
	{ PAD_RIGHT,     KEY_PAD_DPAD_RIGHT },
};

enum
{
	PADAXIS_LEFTX,
	PADAXIS_LEFTY,
	PADAXIS_RIGHTX,
	PADAXIS_RIGHTY,
	PADAXIS_LTRIGGER,
	PADAXIS_RTRIGGER,
	NUM_PADAXES
};

static const EAxisCodes PadAxisCodes[NUM_PADAXES][2] =
{
	{ AXIS_CODE_PAD_LTHUMB_RIGHT, AXIS_CODE_PAD_LTHUMB_LEFT },
	{ AXIS_CODE_PAD_LTHUMB_DOWN, AXIS_CODE_PAD_LTHUMB_UP },
	{ AXIS_CODE_PAD_RTHUMB_RIGHT, AXIS_CODE_PAD_RTHUMB_LEFT },
	{ AXIS_CODE_PAD_RTHUMB_DOWN, AXIS_CODE_PAD_RTHUMB_UP },
	{ AXIS_CODE_PAD_LTRIGGER, AXIS_CODE_NULL },
	{ AXIS_CODE_PAD_RTRIGGER, AXIS_CODE_NULL }
};

static const char *const PadAxisNames[NUM_PADAXES] =
{
	"Left Stick X", "Left Stick Y", "Right Stick X", "Right Stick Y", "L2", "R2"
};

static const float PadAxisThresholds[NUM_PADAXES] =
{
	JOYTHRESH_STICK_X, JOYTHRESH_STICK_Y, JOYTHRESH_STICK_X, JOYTHRESH_STICK_Y,
	JOYTHRESH_TRIGGER, JOYTHRESH_TRIGGER
};

//==========================================================================
//
// One player's controller
//
//==========================================================================

class PS5Joystick : public IJoystickConfig
{
public:
	PS5Joystick(int player) : Player(player)
	{
		SetDefaultConfig();
		M_LoadJoystickConfig(this);
	}

	~PS5Joystick()
	{
		if (SettingsChanged)
			M_SaveJoystickConfig(this);
	}

	FString GetName() override
	{
		FString name;
		name.Format("Controller %d", Player + 1);
		return name;
	}
	float GetSensitivity() override { return Multiplier; }
	void SetSensitivity(float scale) override { SettingsChanged = true; Multiplier = scale; }

	bool HasHaptics() override { return true; }
	float GetHapticsStrength() override { return HapticsStrength; }
	void SetHapticsStrength(float strength) override
	{
		SettingsChanged = true;
		HapticsStrength = clamp(strength, 0.f, 2.f);
	}

	int GetNumAxes() override { return NUM_PADAXES; }
	float GetAxisDeadZone(int axis) override { return Axes[axis].DeadZone; }
	const char *GetAxisName(int axis) override { return PadAxisNames[axis]; }
	float GetAxisScale(int axis) override { return Axes[axis].Multiplier; }
	float GetAxisDigitalThreshold(int axis) override { return Axes[axis].DigitalThreshold; }
	EJoyCurve GetAxisResponseCurve(int axis) override { return Axes[axis].ResponseCurvePreset; }
	float GetAxisResponseCurvePoint(int axis, int point) override
	{
		return unsigned(point) < 4 ? Axes[axis].ResponseCurve.pts[point] : 0;
	}

	void SetAxisDeadZone(int axis, float zone) override
	{
		SettingsChanged = true;
		Axes[axis].DeadZone = clamp(zone, 0.f, 1.f);
	}
	void SetAxisScale(int axis, float scale) override
	{
		SettingsChanged = true;
		Axes[axis].Multiplier = scale;
	}
	void SetAxisDigitalThreshold(int axis, float threshold) override
	{
		SettingsChanged = true;
		Axes[axis].DigitalThreshold = threshold;
	}
	void SetAxisResponseCurve(int axis, EJoyCurve preset) override
	{
		if (preset >= NUM_JOYCURVE || preset < JOYCURVE_CUSTOM) return;
		SettingsChanged = true;
		Axes[axis].ResponseCurvePreset = preset;
		if (preset == JOYCURVE_CUSTOM) return;
		Axes[axis].ResponseCurve = JOYCURVE[preset];
	}
	void SetAxisResponseCurvePoint(int axis, int point, float value) override
	{
		if (unsigned(point) < 4)
		{
			SettingsChanged = true;
			Axes[axis].ResponseCurvePreset = JOYCURVE_CUSTOM;
			Axes[axis].ResponseCurve.pts[point] = value;
		}
	}

	bool IsSensitivityDefault() override { return Multiplier == JOYSENSITIVITY_DEFAULT; }
	bool IsHapticsStrengthDefault() override { return HapticsStrength == JOYHAPSTRENGTH_DEFAULT; }
	bool IsAxisDeadZoneDefault(int axis) override { return Axes[axis].DeadZone == JOYDEADZONE_DEFAULT; }
	bool IsAxisScaleDefault(int axis) override { return Axes[axis].Multiplier == JOYSENSITIVITY_DEFAULT; }
	bool IsAxisDigitalThresholdDefault(int axis) override { return Axes[axis].DigitalThreshold == PadAxisThresholds[axis]; }
	bool IsAxisResponseCurveDefault(int axis) override { return Axes[axis].ResponseCurvePreset == JOYCURVE_DEFAULT; }

	void SetDefaultConfig() override
	{
		Multiplier = JOYSENSITIVITY_DEFAULT;
		HapticsStrength = JOYHAPSTRENGTH_DEFAULT;
		for (int i = 0; i < NUM_PADAXES; i++)
		{
			Axes[i].DeadZone = JOYDEADZONE_DEFAULT;
			Axes[i].Multiplier = JOYSENSITIVITY_DEFAULT;
			Axes[i].DigitalThreshold = PadAxisThresholds[i];
			Axes[i].ResponseCurvePreset = JOYCURVE_DEFAULT;
			Axes[i].ResponseCurve = JOYCURVE[JOYCURVE_DEFAULT];
			Axes[i].Value = 0.0;
			Axes[i].ButtonValue = 0;
		}
	}

	bool GetEnabled() override { return Enabled; }
	void SetEnabled(bool enabled) override { SettingsChanged = true; Enabled = enabled; }

	bool AllowsEnabledInBackground() override { return false; }
	bool GetEnabledInBackground() override { return false; }
	void SetEnabledInBackground(bool) override {}

	FString GetIdentifier() override
	{
		char id[16];
		snprintf(id, countof(id), "PS5:%d", Player);
		return id;
	}

	void Rumble(float high_freq, float low_freq)
	{
		// The console's two motors: the large one is the low frequency.
		pad_player_vibrate(Player,
			clamp(low_freq * HapticsStrength, 0.f, 1.f),
			clamp(high_freq * HapticsStrength, 0.f, 1.f));
	}

	void AddAxes(float joyaxes[NUM_AXIS_CODES])
	{
		for (int i = 0; i < NUM_PADAXES; ++i)
		{
			const float value = float(Axes[i].Value * Multiplier * Axes[i].Multiplier);
			int code = AXIS_CODE_NULL;
			if (value > 0.0f) code = PadAxisCodes[i][0];
			else if (value < 0.0f) code = PadAxisCodes[i][1];
			if (code != AXIS_CODE_NULL)
				joyaxes[code] += fabsf(value);
		}
	}

	// The buttons that changed since the last reading, as key events
	void ProcessButtons(const struct pad &pad)
	{
		const uint32_t changed = pad.held ^ Held;
		if (changed == 0)
			return;
		for (const auto &entry : PadButtons)
		{
			if (!(changed & entry.button))
				continue;
			event_t event = { 0,0,0,0,0,0,0 };
			event.type = (pad.held & entry.button) ? EV_KeyDown : EV_KeyUp;
			event.data1 = entry.key;
			// Always delivered: the pad is the only input the console has,
			// and a setting that silenced it could not be switched back.
			D_PostEvent(&event);
		}
		Held = pad.held;
	}

	void ProcessAxes(const struct pad &pad)
	{
		// The platform layer has already removed its own small dead zone and
		// rescaled, so these are -1..1 with y down-positive, as SDL gives them.
		Thumbstick(PADAXIS_LEFTX, PADAXIS_LEFTY, pad.left_x, pad.left_y, KEY_PAD_LTHUMB_RIGHT);
		Thumbstick(PADAXIS_RIGHTX, PADAXIS_RIGHTY, pad.right_x, pad.right_y, KEY_PAD_RTHUMB_RIGHT);
		Trigger(PADAXIS_LTRIGGER, pad.l2, KEY_PAD_LTRIGGER);
		Trigger(PADAXIS_RTRIGGER, pad.r2, KEY_PAD_RTRIGGER);
	}

	int Player;
	bool Enabled = true;

protected:
	void Thumbstick(int index1, int index2, double value1, double value2, int base)
	{
		AxisInfo &axis1 = Axes[index1];
		AxisInfo &axis2 = Axes[index2];
		uint8_t buttonstate;
		Joy_ManageThumbstick(
			&value1, &value2,
			axis1.DeadZone, axis2.DeadZone,
			axis1.DigitalThreshold, axis2.DigitalThreshold,
			axis1.ResponseCurve, axis2.ResponseCurve,
			&buttonstate);
		axis1.Value = value1;
		axis2.Value = value2;
		Joy_GenerateButtonEvents(axis1.ButtonValue, buttonstate, 4, base);
		axis1.ButtonValue = buttonstate;
	}

	void Trigger(int index, double value, int base)
	{
		AxisInfo &axis = Axes[index];
		uint8_t buttonstate;
		value = Joy_ManageSingleAxis(value, axis.DeadZone, axis.DigitalThreshold, axis.ResponseCurve, &buttonstate);
		axis.Value = value;
		Joy_GenerateButtonEvents(axis.ButtonValue, buttonstate, 1, base);
		axis.ButtonValue = buttonstate;
	}

	struct AxisInfo
	{
		float DeadZone;
		float Multiplier;
		float DigitalThreshold;
		EJoyCurve ResponseCurvePreset;
		CubicBezier ResponseCurve;
		double Value;
		uint8_t ButtonValue;
	};

	AxisInfo Axes[NUM_PADAXES];
	float Multiplier;
	float HapticsStrength;
	bool SettingsChanged = false;
	uint32_t Held = 0;
};

//==========================================================================
//
// The players
//
//==========================================================================

static PS5Joystick *Joysticks[PAD_PLAYERS];
static uint32_t ConnectedPlayers;
static bool PadsOpen;

static void UpdateDevices()
{
	const uint32_t connected = pad_players();
	for (int player = 0; player < PAD_PLAYERS; player++)
	{
		const bool present = (connected >> player) & 1;
		if (present && Joysticks[player] == nullptr)
		{
			Joysticks[player] = new PS5Joystick(player);
		}
		else if (!present && Joysticks[player] != nullptr)
		{
			delete Joysticks[player];
			Joysticks[player] = nullptr;
		}
	}
	ConnectedPlayers = connected;
}

// Read every controller once and turn the changes into events.
static void PollPads()
{
	if (!PadsOpen)
		return;

	struct pad first;
	memset(&first, 0, sizeof(first));
	pad_poll(&first);

	if (pad_players() != ConnectedPlayers)
	{
		UpdateDevices();
		event_t event = { 0,0,0,0,0,0,0 };
		event.type = EV_DeviceChange;
		D_PostEvent(&event);
	}

	for (int player = 0; player < PAD_PLAYERS; player++)
	{
		PS5Joystick *stick = Joysticks[player];
		if (stick == nullptr)
			continue;
		struct pad pad;
		memset(&pad, 0, sizeof(pad));
		if (!pad_player(player, &pad))
			continue;
		stick->ProcessButtons(pad);
		stick->ProcessAxes(pad);
	}
}

void I_StartupJoysticks()
{
	PadsOpen = pad_open();
	PS5_KeyboardOpen(); // after the pads: opening them starts the user service
	if (!PadsOpen)
	{
		Printf("No controller could be opened.\n");
		return;
	}
	struct pad first;
	memset(&first, 0, sizeof(first));
	pad_poll(&first);
	UpdateDevices();
}

void I_ShutdownInput()
{
	PS5_KeyboardClose();
	for (int player = 0; player < PAD_PLAYERS; player++)
	{
		if (Joysticks[player] != nullptr)
		{
			pad_player_vibrate(player, 0, 0);
			delete Joysticks[player];
			Joysticks[player] = nullptr;
		}
	}
}

void I_GetJoysticks(TArray<IJoystickConfig *> &sticks)
{
	sticks.Clear();
	for (int player = 0; player < PAD_PLAYERS; player++)
	{
		if (Joysticks[player] != nullptr)
		{
			M_LoadJoystickConfig(Joysticks[player]);
			sticks.Push(Joysticks[player]);
		}
	}
}

void I_GetAxes(float axes[NUM_AXIS_CODES])
{
	for (int i = 0; i < NUM_AXIS_CODES; ++i)
		axes[i] = 0.0f;

	// Not gated by use_joystick or the per-device switch, for the same
	// reason the buttons are not: there is no other way to move.
	for (int player = 0; player < PAD_PLAYERS; player++)
	{
		if (Joysticks[player] != nullptr)
			Joysticks[player]->AddAxes(axes);
	}
}

void I_Rumble(double high_freq, double low_freq, double, double)
{
	if (!use_joystick)
		return;
	for (int player = 0; player < PAD_PLAYERS; player++)
	{
		if (Joysticks[player] != nullptr && Joysticks[player]->Enabled)
			Joysticks[player]->Rumble((float)high_freq, (float)low_freq);
	}
}

void I_ProcessJoysticks()
{
	PollPads();
}

void I_JoyConsumeEvent(int, event_t *event)
{
	D_PostEvent(event);
}

IJoystickConfig *I_UpdateDeviceList()
{
	UpdateDevices();
	return NULL;
}

//==========================================================================
//
// The engine's input entry points
//
//==========================================================================

static void I_CheckGUICapture()
{
	bool wantCapt = sysCallbacks.WantGuiCapture && sysCallbacks.WantGuiCapture();

	if (wantCapt != GUICapture)
	{
		GUICapture = wantCapt;
		if (wantCapt)
		{
			buttonMap.ResetButtonStates();
		}
	}
}

void I_SetMouseCapture()
{
}

void I_ReleaseMouseCapture()
{
}

void I_GetEvent()
{
	PollPads();
	PS5_KeyboardPoll();
}

void I_StartTic()
{
	I_CheckGUICapture();
	I_GetEvent();
	Joy_RumbleTick();
}

void I_StartFrame()
{
	I_ProcessJoysticks();
}
