/*
 * The few parts of GLib the bundled FluidSynth uses, on POSIX threads
 * (see posix_glibstubs.h).
 *
 * Copyright 2026 PS5-UZDOOM port contributors
 * Modelled on win32_glibstubs.c from ZMusic's FluidSynth.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#if defined(WITH_GLIB_STUBS) && !defined(_WIN32)

#include "fluidsynth_priv.h"
#include "fluid_sys.h"
#include "posix_glibstubs.h"

#include <errno.h>
#include <string.h>
#include <time.h>

int fluid_g_file_test(const char *path, int flags)
{
    struct stat info;
    if (path == NULL || stat(path, &info) != 0)
    {
        return FALSE;
    }
    if (flags & G_FILE_TEST_EXISTS)
    {
        return TRUE;
    }
    if (flags & G_FILE_TEST_IS_REGULAR)
    {
        return S_ISREG(info.st_mode);
    }
    return FALSE;
}

/* Only FluidSynth's command shell splits lines, and that shell is not built. */
int fluid_g_shell_parse_argv(const char *command_line, int *argcp, char ***argvp, void *dummy)
{
    (void)command_line;
    (void)dummy;
    if (argcp != NULL)
    {
        *argcp = 0;
    }
    if (argvp != NULL)
    {
        *argvp = NULL;
    }
    return FALSE;
}

double fluid_g_get_monotonic_time(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec * 1000000.0 + (double)now.tv_nsec / 1000.0;
}

/* Thread support */
static void *g_thread_wrapper(void *info_)
{
    GThread *info = (GThread *)info_;
    info->func(info->data);
    /* A detached thread's record is its own to free; a joinable one's is
     * freed in fluid_g_thread_join. */
    if (!info->joinable)
    {
        free(info);
    }
    return NULL;
}

GThread *fluid_g_thread_create(GThreadFunc func, void *data, int joinable, GError **error)
{
    static GError error_container;
    GThread *info;
    int result;

    g_return_val_if_fail(func != NULL, NULL);

    info = (GThread *)malloc(sizeof(GThread));
    if (info == NULL)
    {
        return NULL;
    }

    info->func = func;
    info->data = data;
    info->joinable = joinable;

    result = pthread_create(&info->handle, NULL, g_thread_wrapper, info);
    if (error != NULL)
    {
        error_container.code = result;
        error_container.message = result != 0 ? strerror(result) : NULL;
        *error = result != 0 ? &error_container : NULL;
    }
    if (result != 0)
    {
        free(info);
        return NULL;
    }
    if (!joinable)
    {
        /* The thread frees the record when it ends: do not touch it again. */
        pthread_detach(info->handle);
    }
    return info;
}

void fluid_g_thread_join(GThread *thread)
{
    if (thread != NULL && thread->joinable)
    {
        pthread_join(thread->handle, NULL);
        free(thread);
    }
}

#endif
