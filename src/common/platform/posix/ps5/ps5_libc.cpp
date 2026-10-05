/*
** ps5_libc.cpp
**
** C library functions the engine calls that neither the console nor the
** platform layer (libps5platform.a) has. Each is the plain POSIX behaviour,
** written on functions that are there.
**
** They carry a uzps5_ prefix, as the platform layer's carry ps5_: a title
** that defined libc's own names would export them, which the tool that
** converts the title for the console refuses. The link binds the standard
** names to these (ps5/tools/link-title.sh), and the same file compiles on a
** PC for its test (ps5/tools/test_libc.cpp).
**
** Copyright 2026 PS5-UZDOOM port contributors
** SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fnmatch.h>
#include <limits.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef FNM_LEADING_DIR
#define FNM_LEADING_DIR 0
#endif
#ifndef FNM_CASEFOLD
#define FNM_CASEFOLD 0
#endif

extern "C"
{

//==========================================================================
//
// fnmatch: shell patterns. '*', '?', bracket expressions with ranges and
// negation, backslash escapes; FNM_NOESCAPE, FNM_PATHNAME, FNM_PERIOD,
// FNM_LEADING_DIR and FNM_CASEFOLD.
//
//==========================================================================

static int Fold(int c, int flags)
{
	return (flags & FNM_CASEFOLD) ? tolower((unsigned char)c) : (unsigned char)c;
}

// Does c match the bracket expression starting after '['? *end is set to
// the character after its ']'. Returns -1 when the bracket never closes,
// which makes '[' an ordinary character.
static int MatchBracket(const char *p, int c, int flags, const char **end)
{
	bool negate = false, matched = false;
	if (*p == '!' || *p == '^')
	{
		negate = true;
		p++;
	}
	c = Fold(c, flags);
	bool first = true;
	for (;; first = false)
	{
		if (*p == '\0')
			return -1;
		if (*p == ']' && !first)
			break;
		int low = (unsigned char)*p++;
		if (low == '\\' && !(flags & FNM_NOESCAPE) && *p != '\0')
			low = (unsigned char)*p++;
		int high = low;
		if (*p == '-' && p[1] != ']' && p[1] != '\0')
		{
			p++;
			high = (unsigned char)*p++;
			if (high == '\\' && !(flags & FNM_NOESCAPE) && *p != '\0')
				high = (unsigned char)*p++;
		}
		if (c >= Fold(low, flags) && c <= Fold(high, flags))
			matched = true;
		// A range written in capitals covers folded text too
		if ((flags & FNM_CASEFOLD) && c >= tolower(low) && c <= tolower(high))
			matched = true;
	}
	*end = p + 1;
	// A slash in a path is matched only by a slash in the pattern
	if ((flags & FNM_PATHNAME) && c == '/')
		return 0;
	return matched != negate;
}

static int Match(const char *pattern, const char *string, const char *start, int flags)
{
	for (;;)
	{
		const char pc = *pattern++;
		// A period at the start (of the string, or of a path component) is
		// matched only by a period in the pattern.
		const bool leading = *string == '.' && (flags & FNM_PERIOD) &&
			(string == start || ((flags & FNM_PATHNAME) && string[-1] == '/'));
		switch (pc)
		{
		case '\0':
			if ((flags & FNM_LEADING_DIR) && *string == '/')
				return 0;
			return *string == '\0' ? 0 : FNM_NOMATCH;

		case '?':
			if (*string == '\0' || leading || ((flags & FNM_PATHNAME) && *string == '/'))
				return FNM_NOMATCH;
			string++;
			break;

		case '*':
			while (*pattern == '*')
				pattern++;
			if (leading)
				return FNM_NOMATCH;
			if (*pattern == '\0')
			{
				if (flags & FNM_PATHNAME)
					return ((flags & FNM_LEADING_DIR) || strchr(string, '/') == nullptr) ? 0 : FNM_NOMATCH;
				return 0;
			}
			for (;; string++)
			{
				if (Match(pattern, string, start, flags) == 0)
					return 0;
				if (*string == '\0' || ((flags & FNM_PATHNAME) && *string == '/'))
					return FNM_NOMATCH;
			}

		case '[':
		{
			const char *end;
			if (*string == '\0' || leading)
				return FNM_NOMATCH;
			const int result = MatchBracket(pattern, *string, flags, &end);
			if (result < 0)
			{
				// No closing bracket: an ordinary '['
				if (*string != '[')
					return FNM_NOMATCH;
			}
			else
			{
				if (result == 0)
					return FNM_NOMATCH;
				pattern = end;
			}
			string++;
			break;
		}

		case '\\':
			if (!(flags & FNM_NOESCAPE) && *pattern != '\0')
			{
				if (Fold(*pattern, flags) != Fold(*string, flags))
					return FNM_NOMATCH;
				pattern++;
				string++;
				break;
			}
			// fall through: an ordinary backslash

		default:
			if (Fold(pc, flags) != Fold(*string, flags))
				return FNM_NOMATCH;
			string++;
			break;
		}
	}
}

int uzps5_fnmatch(const char *pattern, const char *string, int flags)
{
	return Match(pattern, string, string, flags);
}

//==========================================================================
//
// scandir, alphasort
//
//==========================================================================

int uzps5_alphasort(const struct dirent **a, const struct dirent **b)
{
	return strcoll((*a)->d_name, (*b)->d_name);
}

// The entries of a directory that `select` accepts, each a copy the caller
// frees, sorted with `compare`; the count, or -1.
int uzps5_scandir(const char *dirname, struct dirent ***namelist,
	int (*select)(const struct dirent *),
	int (*compare)(const struct dirent **, const struct dirent **))
{
	DIR *dir = opendir(dirname);
	if (dir == nullptr)
		return -1;

	struct dirent **list = nullptr;
	size_t count = 0, capacity = 0;
	bool failed = false;
	while (struct dirent *entry = readdir(dir))
	{
		if (select != nullptr && !select(entry))
			continue;
		if (count == capacity)
		{
			const size_t wanted = capacity ? capacity * 2 : 32;
			struct dirent **grown = (struct dirent **)realloc(list, wanted * sizeof(*list));
			if (grown == nullptr)
			{
				failed = true;
				break;
			}
			list = grown;
			capacity = wanted;
		}
		struct dirent *copy = (struct dirent *)malloc(sizeof(struct dirent));
		if (copy == nullptr)
		{
			failed = true;
			break;
		}
		memcpy(copy, entry, sizeof(struct dirent));
		list[count++] = copy;
	}
	closedir(dir);

	if (failed)
	{
		for (size_t i = 0; i < count; i++)
			free(list[i]);
		free(list);
		return -1;
	}
	if (compare != nullptr && count > 1)
	{
		qsort(list, count, sizeof(*list), (int (*)(const void *, const void *))compare);
	}
	*namelist = list;
	return (int)count;
}

//==========================================================================
//
// getpwnam: a title runs as no user the password database knows. The
// engine asks only to expand "~name", and treats no answer as "leave the
// path alone".
//
//==========================================================================

struct passwd *uzps5_getpwnam(const char *)
{
	return nullptr;
}

//==========================================================================
//
// pathconf: the limits FreeBSD's file systems have.
//
//==========================================================================

long uzps5_pathconf(const char *, int name)
{
	switch (name)
	{
	case _PC_NAME_MAX: return 255;
	case _PC_PATH_MAX: return 1024;
	case _PC_LINK_MAX: return 1;
	case _PC_NO_TRUNC: return 1;
	default:
		errno = EINVAL;
		return -1;
	}
}

} // extern "C"
