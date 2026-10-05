/*
** ps5_libc.cpp
**
** C library functions the engine calls that the console's libc does not
** have. Each is the plain POSIX behaviour, written on functions the
** console (or the platform layer under it) does provide.
**
** Copyright 2026 PS5-UZDOOM port contributors
** SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <dirent.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>

extern "C"
{

int alphasort(const struct dirent **a, const struct dirent **b)
{
	return strcoll((*a)->d_name, (*b)->d_name);
}

// The entries of a directory that `select` accepts, each a copy the caller
// frees, sorted with `compare`; the count, or -1.
int scandir(const char *dirname, struct dirent ***namelist,
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

// A title runs as no user the password database knows; the engine asks only
// to expand "~", and treats no answer as "leave the path alone".
struct passwd *getpwuid(uid_t)
{
	return nullptr;
}

struct passwd *getpwnam(const char *)
{
	return nullptr;
}

} // extern "C"
