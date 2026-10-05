/*
 * The few parts of GLib the bundled FluidSynth uses, on POSIX threads.
 *
 * FluidSynth needs GLib for threads, locks, atomics and a few helpers. ZMusic
 * already carries stand-ins for them on Windows (win32_glibstubs.h); this is
 * the same set for a POSIX system that has no GLib to link, such as the PS5.
 * Enabled with WITH_GLIB_STUBS on a system that is not Windows.
 *
 * Copyright 2026 PS5-UZDOOM port contributors
 * Modelled on win32_glibstubs.h from ZMusic's FluidSynth.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef _POSIX_GLIBSTUBS_H
#define _POSIX_GLIBSTUBS_H

#if defined(WITH_GLIB_STUBS) && !defined(_WIN32)

#include <alloca.h>
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

/* Miscellaneous stubs */
#define GLIB_CHECK_VERSION(x, y, z) 0 /* Evaluate to 0 to get FluidSynth to use the "old" thread API */
#define GLIB_MAJOR_VERSION 2
#define GLIB_MINOR_VERSION 29

typedef struct
{
    int code;
    const char *message;
} GError;
typedef void *gpointer;
typedef int gint;
typedef unsigned int guint;
typedef int gboolean;
typedef int32_t gint32;
typedef int64_t gint64;
typedef uint64_t guint64;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define g_new(s, c) FLUID_ARRAY(s, c)
#define g_free(p) FLUID_FREE(p)
#define g_strfreev FLUID_FREE
#define g_newa(_type, _len) (_type *)alloca(sizeof(_type) * (_len))
#define g_assert(a) assert(a)
#define G_LIKELY(expr) (__builtin_expect(!!(expr), 1))
#define G_UNLIKELY(expr) (__builtin_expect(!!(expr), 0))

#define g_vsnprintf(b, c, f, a) vsnprintf(b, c, f, a)
#define g_snprintf(b, c, f, ...) snprintf(b, c, f, __VA_ARGS__)

#define g_return_val_if_fail(expr, val) if (expr) {} else { return val; }
#define g_clear_error(err) do {} while (0)

#define G_FILE_TEST_EXISTS 1
#define G_FILE_TEST_IS_REGULAR 2

#define g_file_test fluid_g_file_test
#define g_shell_parse_argv fluid_g_shell_parse_argv
int fluid_g_file_test(const char *path, int flags);
int fluid_g_shell_parse_argv(const char *command_line, int *argcp, char ***argvp, void *dummy);

#define g_stat(_filename, _statbuf) stat((_filename), (_statbuf))

/* Microseconds on the monotonic clock */
#define g_get_monotonic_time fluid_g_get_monotonic_time
double fluid_g_get_monotonic_time(void);

/* Byte ordering */
#define G_BYTE_ORDER __BYTE_ORDER__
#define G_BIG_ENDIAN __ORDER_BIG_ENDIAN__

#if G_BYTE_ORDER == G_BIG_ENDIAN
#define GINT16_FROM_LE(x) (int16_t)(((uint16_t)(x) >> 8) | ((uint16_t)(x) << 8))
#define GINT32_FROM_LE(x) (int32_t)__builtin_bswap32((uint32_t)(x))
#else
#define GINT32_FROM_LE(x) (x)
#define GINT16_FROM_LE(x) (x)
#endif

/* Thread support */
#define g_thread_supported() 1
#define g_thread_init(_) do {} while (0)
#define g_usleep(usecs) usleep(usecs)

typedef gpointer (*GThreadFunc)(void *data);
typedef struct
{
    GThreadFunc func;
    void *data;
    pthread_t handle;
    int joinable;
} GThread;

#define g_thread_create fluid_g_thread_create
#define g_thread_join fluid_g_thread_join
GThread *fluid_g_thread_create(GThreadFunc func, void *data, int joinable, GError **error);
void fluid_g_thread_join(GThread *thread);

/* Regular mutex */
typedef pthread_mutex_t GStaticMutex;
#define G_STATIC_MUTEX_INIT PTHREAD_MUTEX_INITIALIZER
#define g_static_mutex_init(_m) pthread_mutex_init(_m, NULL)
#define g_static_mutex_free(_m) pthread_mutex_destroy(_m)
#define g_static_mutex_lock(_m) pthread_mutex_lock(_m)
#define g_static_mutex_unlock(_m) pthread_mutex_unlock(_m)

/* Recursive lock capable mutex */
typedef pthread_mutex_t GStaticRecMutex;
static inline void g_static_rec_mutex_init(GStaticRecMutex *mutex)
{
    pthread_mutexattr_t attributes;
    pthread_mutexattr_init(&attributes);
    pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(mutex, &attributes);
    pthread_mutexattr_destroy(&attributes);
}
#define g_static_rec_mutex_free(_m) pthread_mutex_destroy(_m)
#define g_static_rec_mutex_lock(_m) pthread_mutex_lock(_m)
#define g_static_rec_mutex_unlock(_m) pthread_mutex_unlock(_m)

/* Dynamically allocated mutex suitable for fluid_cond_t use */
typedef pthread_mutex_t GMutex;
#define g_mutex_free(m) do { if (m != NULL) { pthread_mutex_destroy(m); g_free(m); } } while(0)
#define g_mutex_lock(m) pthread_mutex_lock(m)
#define g_mutex_unlock(m) pthread_mutex_unlock(m)

static inline GMutex *g_mutex_new(void)
{
    GMutex *mutex = (GMutex *)malloc(sizeof(GMutex));
    if (mutex != NULL)
        pthread_mutex_init(mutex, NULL);
    return mutex;
}

/* Thread condition signaling */
typedef pthread_cond_t GCond;
#define g_cond_free(cond) do { if (cond != NULL) { pthread_cond_destroy(cond); g_free(cond); } } while (0)
#define g_cond_signal(cond) pthread_cond_signal(cond)
#define g_cond_broadcast(cond) pthread_cond_broadcast(cond)
#define g_cond_wait(cond, mutex) pthread_cond_wait(cond, mutex)

static inline GCond *g_cond_new(void)
{
    GCond *cond = (GCond *)malloc(sizeof(GCond));
    if (cond != NULL)
        pthread_cond_init(cond, NULL);
    return cond;
}

/* Thread private data */
typedef pthread_key_t GStaticPrivate;
#define g_static_private_init(_priv) pthread_key_create(_priv, NULL)
#define g_static_private_get(_priv) pthread_getspecific(*_priv)
#define g_static_private_set(_priv, _data, _) pthread_setspecific(*_priv, _data)
#define g_static_private_free(_priv) pthread_key_delete(*_priv)

/* Atomic operations */
#define g_atomic_int_inc(_pi) ((void)__atomic_add_fetch(_pi, 1, __ATOMIC_SEQ_CST))
#define g_atomic_int_get(_pi) __atomic_load_n(_pi, __ATOMIC_SEQ_CST)
#define g_atomic_int_set(_pi, _val) __atomic_store_n(_pi, _val, __ATOMIC_SEQ_CST)
#define g_atomic_int_dec_and_test(_pi) (__atomic_sub_fetch(_pi, 1, __ATOMIC_SEQ_CST) == 0)
#define g_atomic_int_compare_and_exchange(_pi, _old, _new) \
    __sync_bool_compare_and_swap(_pi, _old, _new)
#define g_atomic_int_exchange_and_add(_pi, _add) __atomic_fetch_add(_pi, _add, __ATOMIC_SEQ_CST)
#define g_atomic_pointer_get(_pp) __atomic_load_n(_pp, __ATOMIC_SEQ_CST)
#define g_atomic_pointer_set(_pp, _val) __atomic_store_n(_pp, _val, __ATOMIC_SEQ_CST)
#define g_atomic_pointer_compare_and_exchange(_pp, _old, _new) \
    __sync_bool_compare_and_swap(_pp, _old, _new)

#endif

#endif
