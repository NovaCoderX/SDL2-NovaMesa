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

#ifndef _SDL_os3video_h
#define _SDL_os3video_h

#include <exec/types.h>
#include <intuition/intuition.h>
#include <proto/input.h>

#include "../SDL_sysvideo.h"

/* Private video data */
typedef struct
{
	ULONG appId;
	STRPTR appName;
} SDL_VideoData;

extern SDL_bool isP96Mode(ULONG modeId);
extern SDL_bool isUsableP96Mode(ULONG modeId);
extern SDL_bool isPixelFormat32(ULONG pixelFormat);
extern ULONG getP96Mode32(int width, int height);

#endif /* _SDL_os3video_h */


