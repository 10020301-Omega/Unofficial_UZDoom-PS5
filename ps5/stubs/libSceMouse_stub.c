/*
 * PS5-UZDOOM - the names the title imports from the console's libSceMouse.
 * The SDK has no stub library for it. Link-time only: this is never packaged
 * or run; the console's own module answers these at run time.
 *
 * Copyright 2026 PS5-UZDOOM port contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <stdint.h>

int sceMouseInit(void) { return -1; }
int sceMouseOpen(int32_t user, int32_t type, int32_t index, const void *param) { (void)user; (void)type; (void)index; (void)param; return -1; }
int sceMouseRead(int32_t handle, void *data, int32_t count) { (void)handle; (void)data; (void)count; return -1; }
int sceMouseClose(int32_t handle) { (void)handle; return -1; }
