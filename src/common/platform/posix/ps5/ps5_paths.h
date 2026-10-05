/*
** ps5_paths.h
**
** Where things are on the console.
**
** Copyright 2026 PS5-UZDOOM port contributors
** SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

// The title's own folder while it runs: the executable, uzdoom.pk3 and the
// other engine files, licenses/ and _source/. Over FTP the same folder is
// /data/homebrew/<TITLE_ID>.
#ifndef PS5_APP_ROOT
#define PS5_APP_ROOT "/app0"
#endif

// The shared folder on the data partition, used when the title can reach
// it. A title is sandboxed to /app0 unless the console's setup opens /data
// to it, so this is tried at start-up and never assumed.
#ifndef PS5_SHARED_ROOT
#define PS5_SHARED_ROOT "/data/uzdoom"
#endif

// Decide where the user's folders (iwads/, mods/, saves/, config/) are:
// PS5_SHARED_ROOT when a file can be written and read back there, else the
// title's own folder. Call once, first thing.
void PS5_ChooseUserRoot();

// The user's folder as the title opens it ...
const char *PS5_UserRoot();
// ... and the same folder as an FTP client names it.
const char *PS5_UserRootShown();
