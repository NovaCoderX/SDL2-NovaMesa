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

#if SDL_VIDEO_DRIVER_AMIGAOS3

#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/cybergraphics.h>
#include <cybergraphx/cybergraphics.h>

#include "SDL_os3video.h"
#include "SDL_os3modes.h"
#include "SDL_os3screen.h"
#include "SDL_os3window.h"

#define MIN_VIDEO_MODE_WIDTH 320
#define MIN_VIDEO_MODE_HEIGHT 200

#define MAX_VIDEO_MODE_WIDTH 1280
#define MAX_VIDEO_MODE_HEIGHT 1024


static SDL_bool OS3_GetDisplayModeData(ULONG modeId, SDL_DisplayMode* mode, SDL_bool desktopMode)
{
    SDL_DisplayModeData *data;
    ULONG width, height, depth;
    SDL_bool validMode = SDL_FALSE;

    // We are only interested in RTG modes.
    if (IsCyberModeID(modeId)) {
    	width = GetCyberIDAttr(CYBRIDATTR_WIDTH, modeId);
    	height = GetCyberIDAttr(CYBRIDATTR_HEIGHT, modeId);
    	depth = GetCyberIDAttr(CYBRIDATTR_DEPTH, modeId);

        // We only support 24/32-bit modes.
    	if (depth >= 24) {
    		validMode = SDL_TRUE;
    	}

    	if (!desktopMode) {
			// We do not support modes below this size.
			if (width < MIN_VIDEO_MODE_WIDTH || height < MIN_VIDEO_MODE_HEIGHT) {
				validMode = SDL_FALSE;
			}

			// Or above this size.
			if (width > MAX_VIDEO_MODE_WIDTH || height > MAX_VIDEO_MODE_HEIGHT) {
				validMode = SDL_FALSE;
			}

			// Or anything silly.
			if (validMode && (width <= 640)) {
				// Cross-multiplied equivalent of: height / width < 200 / 320
				if ((height * 8) < (width * 5)) {
					validMode = SDL_FALSE;
				}
			}
    	}
    }

	if (!validMode) {
    	return SDL_FALSE;
	}

    data = (SDL_DisplayModeData*)SDL_malloc(sizeof(*data));
    if (!data) {
    	SDL_OutOfMemory();
        return SDL_FALSE;
    }

    data->modeId = modeId;

    SDL_zero(*mode);
    mode->w = width;
    mode->h = height;
    mode->refresh_rate = 60;
    mode->format = SDL_PIXELFORMAT_ARGB8888; // We only support ARGB format (from an SDL perspective).
    mode->driverdata = data;

    // All is cool.
    return SDL_TRUE;
}

int OS3_GetDisplayBounds(_THIS, SDL_VideoDisplay* display, SDL_Rect* rect)
{
    rect->x = 0;
    rect->y = 0;
    rect->w = display->current_mode.w;
    rect->h = display->current_mode.h;

    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Display bounds: x=%d, y=%d, w=%d, h=%d\n", rect->x, rect->y, rect->w, rect->h);

    return 0;
}

void OS3_GetDisplayModes(_THIS, SDL_VideoDisplay* display)
{
    SDL_DisplayMode mode;
    ULONG nextid = INVALID_ID;

    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "OS3_GetDisplayModes() - Called\n");

    while ((nextid = NextDisplayInfo(nextid)) != INVALID_ID) {
        if (OS3_GetDisplayModeData(nextid, &mode, SDL_FALSE)) {
            if (SDL_AddDisplayMode(display, &mode)) {
            	ULONG width, height, depth;

            	width = GetCyberIDAttr(CYBRIDATTR_WIDTH, nextid);
            	height = GetCyberIDAttr(CYBRIDATTR_HEIGHT, nextid);
            	depth = GetCyberIDAttr(CYBRIDATTR_DEPTH, nextid);

            	SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Added display mode: %lu - width=%lu, height=%lu, depth=%lu\n",
            			nextid, width, height, depth);
            } else {
            	// Rejected, probably just a duplicate.
            	SDL_free(mode.driverdata);
            }
        }
    }
}

int OS3_SetDisplayMode(_THIS, SDL_VideoDisplay* display, SDL_DisplayMode* mode)
{
	SDL_DisplayModeData* modeData = (SDL_DisplayModeData*)mode->driverdata;
    ULONG width, height, depth;

	width = GetCyberIDAttr(CYBRIDATTR_WIDTH, modeData->modeId);
	height = GetCyberIDAttr(CYBRIDATTR_HEIGHT, modeData->modeId);
	depth = GetCyberIDAttr(CYBRIDATTR_DEPTH, modeData->modeId);

	SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Requested display mode: %lu - width=%lu, height=%lu, depth=%lu\n",
			modeData->modeId, width, height, depth);

	// We won't actually create the screen now, wait until SDL creates the window.
    return 0;
}

int OS3_InitModes(_THIS)
{
    SDL_VideoDisplay display;
    SDL_DisplayMode current_mode;
    SDL_DisplayData* displaydata;
    ULONG modeId;
    struct Screen* publicScreen;

    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "OS3_InitModes() - Called\n");

    publicScreen = OS3_LockPublicScreen();
    if (!publicScreen) {
        return SDL_SetError("Could not get display mode data for the public screen");
    }

    modeId = GetVPModeID(&publicScreen->ViewPort);

    // We should not keep the public screen locked.
    OS3_UnlockPublicScreen(publicScreen);

    if (!OS3_GetDisplayModeData(modeId, &current_mode, SDL_TRUE)) {
        SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Failed to get display mode data for the public screen\n");
        return SDL_SetError("Could not get display mode data for the public screen");
    } else {
    	ULONG width, height, depth;

    	width = GetCyberIDAttr(CYBRIDATTR_WIDTH, modeId);
    	height = GetCyberIDAttr(CYBRIDATTR_HEIGHT, modeId);
    	depth = GetCyberIDAttr(CYBRIDATTR_DEPTH, modeId);

    	SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Public screen display mode: %lu - width=%lu, height=%lu, depth=%lu\n",
    			modeId,  width, height, depth);
    }

    displaydata = (SDL_DisplayData*)SDL_malloc(sizeof(*displaydata));
    if (!displaydata) {
        return SDL_OutOfMemory();
    }

    displaydata->desktopModeId = modeId;

    SDL_zero(display);
    display.desktop_mode = current_mode;
    display.current_mode = current_mode;
    display.driverdata = displaydata;

    SDL_AddVideoDisplay(&display, SDL_FALSE);

    return 0;
}

void OS3_QuitModes(_THIS)
{
    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "OS3_QuitModes() Called\n");
}

#endif /* SDL_VIDEO_DRIVER_AMIGAOS3 */


