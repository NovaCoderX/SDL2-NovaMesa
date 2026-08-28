/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>

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

#if SDL_VIDEO_DRIVER_AMIGAOS3

#include <proto/exec.h>
#include <proto/Picasso96.h>
#include <libraries/Picasso96.h>

#include "SDL_os3video.h"
#include "SDL_os3window.h"
#include "SDL_os3framebuffer.h"


int OS3_CreateWindowFramebuffer(_THIS, SDL_Window* sdlwin,
                             Uint32* format, void** pixels, int* pitch)
{
    SDL_WindowData *data = (SDL_WindowData*)sdlwin->driverdata;

    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Creating frame buffer for window '%s' (%dx%d)\n", sdlwin->title, sdlwin->w, sdlwin->h);

    /* Free any previous buffer (e.g. window resize) */
    if (data->framebuffer) {
        FreeVec(data->framebuffer);
        data->framebuffer = NULL;
    }

    *format = SDL_PIXELFORMAT_ARGB8888; // We only support ARGB format (from an SDL perspective).
    *pitch  = sdlwin->w * 4;   /* 32bpp, always 4-byte aligned */
    *pixels = AllocVec(*pitch * sdlwin->h, MEMF_FAST | MEMF_CLEAR);

    if (!*pixels) {
        return SDL_OutOfMemory();
    }

    data->framebuffer = *pixels;

#ifdef DEBUG_FRAME_BUFFER_VERBOSE
    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Shadow buffer pitch=%d\n", *pitch);
#endif

    return 0;
}

int OS3_UpdateWindowFramebuffer(_THIS, SDL_Window* sdlwin,
                             const SDL_Rect* rects, int numrects)
{
    SDL_WindowData* data = (SDL_WindowData*)sdlwin->driverdata;
    SDL_Surface* surface = sdlwin->surface;
    int i, dirtyStart, dirtyEnd;
    struct RenderInfo renderInfo;

    if (!data->syswin || !surface) {
        return 0;
    }

    /* Find vertical extent of dirty rects — same logic as RTG_UpdateRects */
    dirtyStart = sdlwin->h;
    dirtyEnd = 0;

    for (i = 0; i < numrects; i++) {
        const SDL_Rect *r = &rects[i];

        if (!r) continue;
        if (r->y < dirtyStart)         dirtyStart = r->y;
        if ((r->y + r->h) > dirtyEnd)  dirtyEnd   = r->y + r->h;
    }

    if (dirtyEnd <= dirtyStart) {
        return 0;
    }

#ifdef DEBUG_FRAME_BUFFER_VERBOSE
    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Blitting dirty strip - start=%d, end=%d\n", dirtyStart, dirtyEnd);
#endif

	// The surface is ARGB, P96 converts to whatever the screen is
	// really using on the way out.
	renderInfo.Memory = (APTR)surface->pixels;
	renderInfo.BytesPerRow = (WORD)surface->pitch;
	renderInfo.pad = 0;
	renderInfo.RGBFormat = RGBFB_A8R8G8B8;

	p96WritePixelArray(&renderInfo, //RenderInfo
		0, //SrcX
		dirtyStart, //SrcY
		data->syswin->RPort, //RastPort
		data->syswin->BorderLeft, //DestX
		data->syswin->BorderTop + dirtyStart, //DestY
		sdlwin->w, //SizeX
		(dirtyEnd - dirtyStart)); //SizeY

    return 0;
}

void OS3_DestroyWindowFramebuffer(_THIS, SDL_Window* sdlwin)
{
    SDL_WindowData* data = (SDL_WindowData*)sdlwin->driverdata;

    if (data && data->framebuffer) {
    	SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Freeing frame buffer...\n");
        FreeVec(data->framebuffer);
        data->framebuffer = NULL;
    }
}

#endif /* SDL_VIDEO_DRIVER_AMIGAOS3 */

