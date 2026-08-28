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

#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/wb.h>
#include <proto/dos.h>
#include <proto/icon.h>
#include <proto/Picasso96.h>
#include <libraries/Picasso96.h>

#include "SDL_os3video.h"
#include "SDL_os3screen.h"
#include "SDL_os3window.h"


struct Screen* OS3_LockPublicScreen(void)
{
    struct Screen* screen = NULL;

    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Trying to lock the public screen\n");

    screen = LockPubScreen(NULL);
    if (screen) {
        SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Locked the public screen: %p\n", screen);
    } else {
    	SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "Failed to lock the public screen\n");
    }

    return screen;
}

void OS3_UnlockPublicScreen(struct Screen* screen)
{
    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Trying to unlock the public screen\n");

	if (screen) {
		SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Unlocking the public screen: %p\n", screen);
        UnlockPubScreen(NULL, screen);
    }
}

struct Screen* OS3_CreateCustomScreen(int width, int height)
{
	struct Screen* screen = NULL;
	ULONG modeId = INVALID_ID;

	SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Trying to create screen - width=%d, height=%d, depth=%d\n", width, height, 32);

	modeId = getP96Mode32(width, height);
	if (modeId == INVALID_ID) {
		SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "Could not find a suitable P96 mode for the attributes: width=%d, height=%d, depth=%d\n", width, height, 32);
	}

	if (modeId != INVALID_ID) {
		screen = OpenScreenTags(NULL, SA_Depth, p96GetModeIDAttr(modeId, P96IDA_BITSPERPIXEL), SA_DisplayID, modeId,
				SA_Top, 0, SA_Left, 0, SA_Width, width, SA_Height, height, SA_Type,
				CUSTOMSCREEN, SA_Quiet, TRUE, SA_ShowTitle, FALSE, SA_Draggable, FALSE,
				SA_Exclusive, TRUE, SA_AutoScroll, FALSE, TAG_DONE);
	}

    if (screen) {
        SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Opened screen: %p - width=%d, height=%d, depth=%d\n", screen, width, height, 32);
    } else {
    	SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "Could not create a screen for the mode attributes: width=%d, height=%d, depth=%d\n", width, height, 32);
    }

	return screen;
}

void OS3_CloseCustomScreen(SDL_Window* sdlwin)
{
	SDL_WindowData* data = sdlwin->driverdata;

	if (data) {
		struct Screen* screen = data->customScreen;

		if (screen) {
			SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Trying to closing screen: %p\n", screen);

			if (CloseScreen(screen) == FALSE) {
				SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "Screen has open window(s), cannot close\n");
			} else {
				SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Screen closed successfully\n");
				data->customScreen = NULL;
	        }
	    }
	}
}


#endif /* SDL_VIDEO_DRIVER_AMIGAOS3 */

