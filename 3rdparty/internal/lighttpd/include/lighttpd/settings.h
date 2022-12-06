#ifndef _LIGHTTPD_SETTINGS_H_
#define _LIGHTTPD_SETTINGS_H_

// This is not the original settings.h. This is a replacement shim, redefining
// the LI and glib macros and functions used by radix.c.

#include <arpa/inet.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define LI_API

typedef void* gpointer;
typedef uint32_t guint32;

typedef void (*GFunc)(gpointer data, gpointer user_data);

#define g_slice_new0(TYPE) calloc(1, sizeof(TYPE));
#define g_slice_free(TYPE, INSTANCE) free(INSTANCE)

#define LI_FORCE_ASSERT(X)

#endif