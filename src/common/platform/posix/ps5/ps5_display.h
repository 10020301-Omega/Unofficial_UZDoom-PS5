/*
** ps5_display.h
**
** The console's one display, shared by the launcher and the engine.
**
** Copyright 2026 PS5-UZDOOM port contributors
** SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <memory>

class VulkanSurface;

// The instance and the display plane surface, made on first use and kept
// until PS5_ReleaseSurface. Throws CVulkanError.
std::shared_ptr<VulkanSurface> PS5_AcquireSurface();
void PS5_ReleaseSurface();
void PS5_GetDisplaySize(int *width, int *height);
