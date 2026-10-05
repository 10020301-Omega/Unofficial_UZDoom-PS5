/*
** test_libc.cpp
**
** The port's own C library functions, checked on a PC against the PC's.
**
**   g++ -std=c++17 -D_GNU_SOURCE -o test_libc ps5/tools/test_libc.cpp \
**       src/common/platform/posix/ps5/ps5_libc.cpp && ./test_libc <a folder>
**
** Copyright 2026 PS5-UZDOOM port contributors
** SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <dirent.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C"
{
int uzps5_fnmatch(const char *pattern, const char *string, int flags);
int uzps5_scandir(const char *dirname, struct dirent ***namelist, int (*select)(const struct dirent *),
	int (*compare)(const struct dirent **, const struct dirent **));
int uzps5_alphasort(const struct dirent **a, const struct dirent **b);
}

static int failures;

static int OnlyW(const struct dirent *entry)
{
	return fnmatch("*.[wW][aA][dD]", entry->d_name, 0) == 0;
}

int main(int argc, char **argv)
{
	static const char *const patterns[] = {
		"*", "*.wad", "*.WAD", "*.[wW][aA][dD]", "doom?.wad", "doom[12].wad", "doom[!12].wad", "doom[^12].wad",
		"[a-c]*", "[!a-c]*", "*.pk[37]", "a*b*c", "*a*", "", "?", "??", "*/*", "*/?*.wad", ".*", "*.*", "a\\*b",
		"a\\?", "[]]x", "[]-]x", "[a", "x[", "[a-", "*[", "\\", "a\\", "[\\]]", "[a\\-c]", "**", "*?*", "?*?",
		"dir/*", "dir/*/*", "*.d", "[.]hidden", "?hidden", "*hidden", "/a/*", "a//b", "*.wad*", "[A-Z]*", "[z-a]",
		"savegame.*", "*", "auto??.zds",
	};
	static const char *const strings[] = {
		"", "a", "ab", "abc", "a.wad", "A.WAD", "doom1.wad", "doom2.wad", "doom3.wad", "doom.wad", "DOOM2.WAD",
		"mod.pk3", "mod.pk7", "mod.pk4", "axbxc", "axbx", "a*b", "a?", "ab", "]x", "-x", "[a", "x[", "[a-", "\\",
		"a\\", "]", "-", "b", ".hidden", "dir/file", "dir/sub/file", "dir/.hidden", "dir/", "/a/b", "a//b", "a.d",
		"x.wad.bak", "Zebra", "zebra", "savegame.zds", "auto01.zds", "auto1.zds", "sub/a.wad", "*", "?",
	};
	static const int flagSets[] = {
		0, FNM_NOESCAPE, FNM_PATHNAME, FNM_PERIOD, FNM_PATHNAME | FNM_PERIOD, FNM_CASEFOLD, FNM_LEADING_DIR,
		FNM_PATHNAME | FNM_LEADING_DIR, FNM_CASEFOLD | FNM_PATHNAME, FNM_NOESCAPE | FNM_PATHNAME | FNM_PERIOD,
	};
	// Two malformed patterns are left out of the comparison, because the PC's
	// library (glibc) and the console's family (FreeBSD) read them differently
	// and this follows FreeBSD: a bracket that never closes is an ordinary '[',
	// and a backslash at the very end is an ordinary backslash. They are
	// checked for that below.
	auto malformed = [](const char *pattern) {
		const size_t length = strlen(pattern);
		size_t slashes = 0;
		while (slashes < length && pattern[length - 1 - slashes] == '\\')
			slashes++;
		return (slashes & 1) != 0 || strcmp(pattern, "[a") == 0 || strcmp(pattern, "[a-") == 0 ||
			strcmp(pattern, "x[") == 0 || strcmp(pattern, "*[") == 0;
	};
	int cases = 0;
	for (const char *pattern : patterns)
		for (const char *string : strings)
			for (const int flags : flagSets)
			{
				if (malformed(pattern))
					continue;
				const int expected = fnmatch(pattern, string, flags) == 0;
				const int got = uzps5_fnmatch(pattern, string, flags) == 0;
				cases++;
				if (expected != got)
				{
					if (failures < 25)
						printf("FAIL  fnmatch(\"%s\", \"%s\", 0x%x): the PC's says %s\n", pattern, string, flags,
							expected ? "match" : "no match");
					failures++;
				}
			}
	printf("%s  fnmatch: %d cases against the PC's, %d differ\n", failures ? "FAIL" : "ok  ", cases, failures);
	const bool freebsd = uzps5_fnmatch("[a-", "[a-", 0) == 0 && uzps5_fnmatch("x[", "x[", 0) == 0 &&
		uzps5_fnmatch("a\\", "a\\", 0) == 0 && uzps5_fnmatch("[a", "a", 0) != 0;
	printf("%s  fnmatch: an unclosed bracket and a final backslash are ordinary characters\n", freebsd ? "ok  " : "FAIL");
	if (!freebsd)
		failures++;

	if (argc > 1)
	{
		struct dirent **mine = nullptr, **theirs = nullptr;
		const int a = uzps5_scandir(argv[1], &mine, OnlyW, uzps5_alphasort);
		const int b = scandir(argv[1], &theirs, OnlyW, alphasort);
		bool same = a == b;
		for (int i = 0; same && i < a; i++)
			same = strcmp(mine[i]->d_name, theirs[i]->d_name) == 0;
		printf("%s  scandir: %d entries, %s the PC's\n", same ? "ok  " : "FAIL", a, same ? "as" : "not as");
		if (!same)
			failures++;
		const int none = uzps5_scandir("/no/such/folder", &mine, nullptr, nullptr);
		printf("%s  scandir of a missing folder fails\n", none == -1 ? "ok  " : "FAIL");
		if (none != -1)
			failures++;
	}
	printf("%s\n", failures ? "FAILED" : "all passed");
	return failures ? 1 : 0;
}
