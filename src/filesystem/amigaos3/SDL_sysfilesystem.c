/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2023 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "../../SDL_internal.h"

#ifdef SDL_FILESYSTEM_AMIGAOS3

#include "SDL_error.h"
#include "SDL_filesystem.h"

#include <proto/dos.h>

/* Recursively create a directory path one component at a time.
   Classic AmigaOS CreateDir() only creates one level, so we walk
   up to the parent first then create on the way back down. */
static BOOL OS3_MakePath(const char *path)
{
    BPTR lock;
    char *temp;
    char *sep;
    BOOL success = FALSE;
    size_t len;

    /* Cheapest check first — does it already exist? */
    lock = Lock((STRPTR)path, SHARED_LOCK);
    if (lock) {
        UnLock(lock);
        return TRUE;
    }

    temp = SDL_strdup(path);
    if (!temp) return FALSE;

    /* Strip trailing slash so CreateDir gets a clean name */
    len = SDL_strlen(temp);
    if (len > 0 && temp[len - 1] == '/') {
        temp[--len] = '\0';
    }

    /* Find the last path separator.
       Amiga paths: volume ends with ':', components separated by '/'.
       We only recurse for '/' separators — a ':' means the parent
       is a volume/device that must already exist. */
    sep = SDL_strrchr(temp, '/');

    if (sep) {
        /* Temporarily cut the string at the separator */
        *sep = '\0';
        success = OS3_MakePath(temp);   /* create parent first */
        *sep = '/';

        if (!success) {
            SDL_free(temp);
            return FALSE;
        }
    }

    /* Now create this directory level */
    lock = CreateDir((STRPTR)temp);
    if (lock) {
        UnLock(lock);
        success = TRUE;
    } else {
        LONG err = IoErr();
        if (err == ERROR_OBJECT_EXISTS) {
            success = TRUE;   /* already there — that's fine */
        } else {
            SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "CreateDir '%s' failed (err %ld)\n", temp, err);
            success = FALSE;
        }
    }

    SDL_free(temp);
    return success;
}

char *SDL_GetBasePath(void)
{
    /* PROGDIR: is available on AmigaOS 2.0+ and always points to
       the directory containing the running executable */
    const char * const basePath = "PROGDIR:";
    const size_t len = SDL_strlen(basePath) + 1;

    char *buffer = (char *)SDL_malloc(len);
    if (!buffer) {
        SDL_OutOfMemory();
        return NULL;
    }

    SDL_snprintf(buffer, len, "%s", basePath);
    return buffer;
}

char *SDL_GetPrefPath(const char *org, const char *app)
{
    /* ENVARC: is the standard AmigaOS location for persistent
       per-user preferences — survives reboots unlike ENV: */
    const char * const envPath = "ENVARC:";
    size_t len = SDL_strlen(envPath) + 1;  /* +1 for NUL */
    char *buffer;

    if (org) len += SDL_strlen(org) + 1;   /* +1 for '/' */
    if (app) len += SDL_strlen(app) + 1;   /* +1 for '/' */

    buffer = (char *)SDL_malloc(len);
    if (!buffer) {
        SDL_OutOfMemory();
        return NULL;
    }

    SDL_snprintf(buffer, len, "%s", envPath);

    if (org) {
        SDL_snprintf(buffer + SDL_strlen(buffer),
                     len - SDL_strlen(buffer), "%s/", org);
    }
    if (app) {
        SDL_snprintf(buffer + SDL_strlen(buffer),
                     len - SDL_strlen(buffer), "%s/", app);
    }

    if (OS3_MakePath(buffer)) {
        return buffer;
    }

    SDL_free(buffer);
    return NULL;
}

#endif /* SDL_FILESYSTEM_AMIGAOS3 */



