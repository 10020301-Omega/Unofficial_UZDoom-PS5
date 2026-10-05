/*
** ps5_paths.h
**
** Where things are on the console.
**
** Copyright 2026 PS5-UZDOOM port contributors
** SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

// The title's own folder: the executable, uzdoom.pk3 and the other engine
// files, licenses/ and _source/. Read-only as far as the port is concerned.
#ifndef PS5_APP_ROOT
#define PS5_APP_ROOT "/app0"
#endif

// The user's folder on the data partition, reachable over FTP and kept
// across updates of the title: iwads/, mods/, saves/, config/.
#ifndef PS5_USER_ROOT
#define PS5_USER_ROOT "/data/uzdoom"
#endif
