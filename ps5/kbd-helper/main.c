/*
 * uzdoom-kbd.elf - a keyboard helper for UZDoom on the PlayStation 5.
 *
 * The game is a title, and the console does not give a title its keyboard
 * library. A payload is not held to that: this one runs beside the game,
 * reads the USB keyboard through libSceKeyboard and sends which keys are
 * down into a small file the game reads, /data/uzdoom/kbd-state.bin
 * (ps5_keyboard.cpp). A file, because a title is refused a loopback socket
 * (bind answers EACCES) and both sides can reach that folder.
 *
 * Send it to the payload loader once per boot, before or after starting the
 * game. What it finds goes to /data/uzdoom/kbd-helper.log, and its first
 * answers pop up as notifications.
 *
 * The file: a 24-byte header, then a ring of the last 64 states.
 *   header: "UZKF", version (uint32, 1), count of states written so far
 *           (uint64), a heartbeat that changes while the helper lives (uint64)
 *   state, 40 bytes: "UZK1", connected (1), HID modifier byte (1),
 *           key count (1), 0, 16 HID usage IDs (uint16 each)
 * State number n is in slot n % 64. A state is written before the count
 * that announces it.
 *
 * Copyright 2026 PS5-UZDOOM port contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define FOLDER "/data/uzdoom"
#define STATE_PATH FOLDER "/kbd-state.bin"
#define LOG_PATH FOLDER "/kbd-helper.log"
#define RING 64

int sceUserServiceInitialize(const void *params);
int sceUserServiceGetInitialUser(int32_t *user);
int sceUserServiceGetForegroundUser(int32_t *user);
int sceKeyboardInit(void);
int sceKeyboardOpen(int32_t user, int32_t type, int32_t index, const void *param);
int sceKeyboardReadState(int32_t handle, void *data);
int sceKeyboardClose(int32_t handle);

typedef struct { char unused[45]; char message[3075]; } notify_request_t;
int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);

/* One record of the keyboard's state as libSceKeyboard writes it, 96 bytes. */
struct keyboard_data
{
	uint64_t timestamp;
	uint64_t intercepted;
	uint8_t connected;
	uint8_t pad[3];
	int32_t length;
	uint32_t led;
	uint32_t modifiers;
	uint16_t key_code[16];
	uint8_t reserved[32];
};
_Static_assert(sizeof(struct keyboard_data) == 96, "record size");

struct packet
{
	char magic[4];
	uint8_t connected, modifiers, count, zero;
	uint16_t keys[16];
};
_Static_assert(sizeof(struct packet) == 40, "packet size");

struct header
{
	char magic[4];
	uint32_t version;
	uint64_t count;
	uint64_t beat;
};
_Static_assert(sizeof(struct header) == 24, "header size");

static FILE *logfile;

static void say(const char *format, ...)
{
	char line[512];
	va_list args;
	va_start(args, format);
	vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	printf("[uzdoom-kbd] %s\n", line);
	if (logfile != NULL)
	{
		fprintf(logfile, "%s\n", line);
		fflush(logfile);
	}
}

static void notify(const char *format, ...)
{
	notify_request_t request;
	memset(&request, 0, sizeof(request));
	va_list args;
	va_start(args, format);
	vsnprintf(request.message, sizeof(request.message), format, args);
	va_end(args);
	say("notify: %s", request.message);
	sceKernelSendNotificationRequest(0, &request, sizeof(request), 0);
}

/* What the console calls its input devices: for the log, in case the library
 * route fails and a device has to be read directly. */
static void list_devices(void)
{
	DIR *dir = opendir("/dev");
	if (dir == NULL)
	{
		say("/dev: cannot list (errno %d)", errno);
		return;
	}
	char found[1024] = "";
	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL)
	{
		const char *n = entry->d_name;
		if (strstr(n, "kbd") || strstr(n, "hid") || strstr(n, "ugen") || strstr(n, "usb") ||
			strstr(n, "ums") || strstr(n, "mouse") || strstr(n, "input"))
		{
			if (strlen(found) + strlen(n) + 2 < sizeof(found))
			{
				strcat(found, n);
				strcat(found, " ");
			}
		}
	}
	closedir(dir);
	say("/dev input-like entries: %s", found[0] ? found : "(none)");
}

static double now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

/* Another copy is running if the state file's heartbeat is moving. */
static int already_running(void)
{
	struct header first, second;
	int fd = open(STATE_PATH, O_RDONLY);
	if (fd < 0) return 0;
	const int got = pread(fd, &first, sizeof(first), 0) == sizeof(first);
	usleep(700000);
	const int again = pread(fd, &second, sizeof(second), 0) == sizeof(second);
	close(fd);
	return got && again && memcmp(first.magic, "UZKF", 4) == 0 && first.beat != second.beat;
}

int main(void)
{
	mkdir(FOLDER, 0777);
	/* Added to, not replaced: a second copy must not wipe what the first found. */
	struct stat info;
	const int big = stat(LOG_PATH, &info) == 0 && info.st_size > 256 * 1024;
	logfile = fopen(LOG_PATH, big ? "w" : "a");
	chmod(LOG_PATH, 0666);
	say("---- uzdoom-kbd helper starting (pid %d) ----", (int)getpid());

	if (already_running())
	{
		notify("UZDoom keyboard helper is already running.");
		return 0;
	}

	list_devices();

	int32_t initial = -1, foreground = -1;
	const int service = sceUserServiceInitialize(NULL);
	const int got_initial = sceUserServiceGetInitialUser(&initial);
	const int got_foreground = sceUserServiceGetForegroundUser(&foreground);
	say("user service 0x%08x; initial user %d (0x%08x), foreground user %d (0x%08x)",
		(unsigned)service, (int)initial, (unsigned)got_initial, (int)foreground, (unsigned)got_foreground);

	const int init = sceKeyboardInit();
	say("sceKeyboardInit 0x%08x", (unsigned)init);

	const int32_t candidates[] = { foreground, initial, 0xFF, 0xFE, 0 };
	int handle = -1;
	for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]) && handle < 0; i++)
	{
		if (candidates[i] == -1) continue;
		handle = sceKeyboardOpen(candidates[i], 0, 0, NULL);
		say("sceKeyboardOpen(user %d) 0x%08x", (int)candidates[i], (unsigned)handle);
	}
	if (handle < 0)
	{
		notify("UZDoom keyboard helper: the console refused the keyboard (0x%08x). Log: " LOG_PATH, (unsigned)handle);
		return 1;
	}

	int out = open(STATE_PATH, O_RDWR | O_CREAT | O_TRUNC, 0666);
	if (out < 0)
	{
		notify("UZDoom keyboard helper: cannot write " STATE_PATH " (errno %d).", errno);
		return 1;
	}
	fchmod(out, 0666);
	struct header header;
	memset(&header, 0, sizeof(header));
	memcpy(header.magic, "UZKF", 4);
	header.version = 1;
	header.beat = 1;
	{
		char zero[RING * sizeof(struct packet)];
		memset(zero, 0, sizeof(zero));
		pwrite(out, &header, sizeof(header), 0);
		pwrite(out, zero, sizeof(zero), sizeof(header));
	}
	notify("UZDoom keyboard helper is running.");

	struct packet last;
	memset(&last, 0, sizeof(last));
	double last_beat = 0, last_report = now();
	int read_errors = 0, reports = 0, changes = 0, first_key_said = 0, written = 0;
	for (;;)
	{
		struct keyboard_data data;
		memset(&data, 0, sizeof(data));
		const int result = sceKeyboardReadState(handle, &data);
		if (result < 0)
		{
			if (read_errors++ == 0)
			{
				say("sceKeyboardReadState 0x%08x", (unsigned)result);
				notify("UZDoom keyboard helper: reading the keyboard failed (0x%08x).", (unsigned)result);
			}
			usleep(500000);
			continue;
		}

		struct packet packet;
		memset(&packet, 0, sizeof(packet));
		memcpy(packet.magic, "UZK1", 4);
		packet.connected = data.connected ? 1 : 0;
		packet.modifiers = (uint8_t)data.modifiers;
		int count = data.length;
		if (count < 0) count = 0;
		if (count > 16) count = 16;
		packet.count = (uint8_t)count;
		for (int i = 0; i < count; i++) packet.keys[i] = data.key_code[i];

		const double t = now();
		const int changed = !written || memcmp(&packet, &last, sizeof(packet)) != 0;
		if (changed)
		{
			changes++;
			if (changes <= 24)
			{
				say("state: connected %d, modifiers 0x%02x, %d keys: 0x%02x 0x%02x 0x%02x 0x%02x",
					packet.connected, packet.modifiers, packet.count,
					packet.keys[0], packet.keys[1], packet.keys[2], packet.keys[3]);
			}
			if (!first_key_said && (packet.count > 0 || packet.modifiers != 0))
			{
				first_key_said = 1;
				notify("UZDoom keyboard helper: keys are being read.");
			}
			/* The state first, then the count that announces it. */
			pwrite(out, &packet, sizeof(packet), sizeof(header) + (header.count % RING) * sizeof(packet));
			header.count++;
			header.beat++;
			pwrite(out, &header, sizeof(header), 0);
			last = packet;
			last_beat = t;
			written = 1;
		}
		else if (t - last_beat >= 0.25)
		{
			header.beat++;
			pwrite(out, &header, sizeof(header), 0);
			last_beat = t;
		}
		if (reports < 6 && t - last_report >= 5.0)
		{
			say("after %ds: connected %d, %d changes so far, raw length %d, modifiers 0x%08x",
				(reports + 1) * 5, packet.connected, changes, (int)data.length, (unsigned)data.modifiers);
			last_report = t;
			reports++;
		}
		usleep(4000); /* 250 times a second */
	}
}
