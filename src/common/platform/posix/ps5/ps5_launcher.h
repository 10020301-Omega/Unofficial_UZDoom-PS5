/*
** ps5_launcher.h
**
** Copyright 2026 PS5-UZDOOM port contributors
** SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <string>
#include <vector>

// Show the launcher until the user starts a game or quits. On a start the
// engine's arguments for the selection are appended and true is returned;
// false means quit.
bool PS5_RunLauncher(std::vector<std::string> &arguments);
