/*
 * PS5-UZDOOM - the names the title imports from the console's libSceIme.
 * The SDK has no stub library for it. Link-time only: this is never packaged
 * or run; the console's own module answers these at run time.
 *
 * Copyright 2026 PS5-UZDOOM port contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <stdint.h>

int sceImeKeyboardOpen(int32_t user, const void *param) { (void)user; (void)param; return -1; }
int sceImeKeyboardClose(int32_t user) { (void)user; return -1; }
int sceImeUpdate(void *handler) { (void)handler; return -1; }
