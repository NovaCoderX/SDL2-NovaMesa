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

#include <proto/intuition.h>
#include <intuition/pointerclass.h>

#include "SDL_os3mouse.h"
#include "SDL_os3video.h"
#include "SDL_os3window.h"
#include "SDL_os3events.h"

#include "SDL_hints.h"
#include "../../events/SDL_mouse_c.h"
#include "../../events/SDL_keyboard_c.h"

OS3_GlobalMouseState globalMouseState;

/* The implementation dependent data for the window manager cursor */
static UWORD _blankCursor[] = { 0x0000, 0x0000,   // reserved, must be NULL
		0x0000, 0x0000,   // 1 row of image data
		0x0000, 0x0000    // reserved, must be NULL
		};

static SDL_Cursor* OS3_CreateCursorInternal(void) {
	// Allocate a dummy cursor to keep SDL's internal state machine happy
	return SDL_calloc(1, sizeof(SDL_Cursor));
}

static SDL_Cursor* OS3_CreateDefaultCursor(void) {
	return OS3_CreateCursorInternal();
}

static SDL_Cursor* OS3_CreateCursor(SDL_Surface* surface, int hot_x, int hot_y) {
	// Ignoring custom surfaces for the simplistic approach
	return OS3_CreateCursorInternal();
}

static SDL_Cursor* OS3_CreateSystemCursor(SDL_SystemCursor id) {
	return OS3_CreateCursorInternal();
}

static int OS3_ShowCursor(SDL_Cursor* cursor) {
    _THIS = SDL_GetVideoDevice();
    SDL_Window* sdlwin;

    // Get the window the mouse is currently physically inside
    SDL_Window* mouse_focus = SDL_GetMouseFocus();

    for (sdlwin = _this->windows; sdlwin; sdlwin = sdlwin->next) {
        SDL_WindowData* data = sdlwin->driverdata;

        if (data && data->syswin) {
            // ONLY apply the blank cursor if SDL requests it (NULL)
            // AND the mouse is currently inside our specific window bounds.
            if (cursor == NULL && mouse_focus == sdlwin) {
                SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Hiding hardware cursor\n");
                SetPointer(data->syswin, _blankCursor, 1, 1, 0, 0);
            }
            else {
                // If the cursor is requested visible, OR the mouse has physically left
                // our window bounds, explicitly clear the blank pointer.
                SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Showing hardware cursor\n");
                ClearPointer(data->syswin);
            }
        }
    }

    return 0;
}

static void OS3_FreeCursor(SDL_Cursor* cursor) {
	if (cursor) {
		SDL_free(cursor);
	}
}

static void OS3_WarpMouse(SDL_Window* sdlwin, int x, int y)
{
	SDL_WindowData *data = sdlwin->driverdata;

    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "OS3_WarpMouse(%d,%d)\n", x, y);

	if (data && data->syswin) {
		struct Window* syswin = data->syswin;

		OS3_WarpSystemPointerAbsolute(syswin->LeftEdge + syswin->BorderLeft + x,
				syswin->TopEdge  + syswin->BorderTop  + y);
	}
}

static int OS3_WarpMouseGlobal(int x, int y)
{
    /* Global coordinates = the screen's coordinate space. On this
       single-display port that is the screen our focused window
       lives on (for fullscreen, global == window space anyway). */
    SDL_Window *sdlwin = SDL_GetMouseFocus();
    int result = -1;

    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "OS3_WarpMouseGlobal(%d,%d)\n", x, y);

    if (!sdlwin) {
        sdlwin = SDL_GetKeyboardFocus();
    }

    /* Clamp into the screen so what we report matches what Intuition
       will actually do with an out-of-range position */
    if (sdlwin) {
        SDL_WindowData *data = sdlwin->driverdata;
        if (data && data->syswin && data->syswin->WScreen) {
            struct Screen *scr = data->syswin->WScreen;

            if (x < 0)                 x = 0;
            else if (x >= scr->Width)  x = scr->Width  - 1;

            if (y < 0)                 y = 0;
            else if (y >= scr->Height) y = scr->Height - 1;

            OS3_WarpSystemPointerAbsolute(x, y);
            result = 0;
        }
    }

    return result;
}

static int OS3_SetRelativeMouseMode(SDL_bool enabled) {
	SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "OS3_SetRelativeMouseMode(%s)\n", enabled ? "enabled" : "disabled");

	// Supported.
	return 0;
}

static int OS3_CaptureMouse(SDL_Window* sdlwin) {
	SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "OS3_CaptureMouse() - Called\n");

	// Supported.
	return 0;
}

static Uint32 OS3_GetGlobalMouseState(int* x, int* y) {
	ULONG buttons = 0;

	if (x) {
		*x = globalMouseState.x;
	}

	if (y) {
		*y = globalMouseState.y;
	}

	if (globalMouseState.buttonPressed[SDL_BUTTON_LEFT]) {
		buttons |= SDL_BUTTON_LMASK;
	}

	if (globalMouseState.buttonPressed[SDL_BUTTON_MIDDLE]) {
		buttons |= SDL_BUTTON_MMASK;
	}

	if (globalMouseState.buttonPressed[SDL_BUTTON_RIGHT]) {
		buttons |= SDL_BUTTON_RMASK;
	}

	return buttons;
}

void OS3_RefreshCursorState(void) {
	SDL_Mouse* mouse = SDL_GetMouse();

	if (mouse) {
		SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Refreshing cursor state\n");
		SDL_SetCursor(NULL);
	}
}

void OS3_InitMouse(_THIS) {
	SDL_Mouse* mouse = SDL_GetMouse();

	SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "[OS3_InitMouse]: Called\n");

	mouse->CreateCursor = OS3_CreateCursor;
	mouse->CreateSystemCursor = OS3_CreateSystemCursor;
	mouse->ShowCursor = OS3_ShowCursor;
	mouse->FreeCursor = OS3_FreeCursor;
	mouse->WarpMouse = OS3_WarpMouse;
	mouse->WarpMouseGlobal = OS3_WarpMouseGlobal;
	mouse->SetRelativeMouseMode = OS3_SetRelativeMouseMode;
	mouse->CaptureMouse = OS3_CaptureMouse;
	mouse->GetGlobalMouseState = OS3_GetGlobalMouseState;

	SDL_SetDefaultCursor(OS3_CreateDefaultCursor());
}

void OS3_QuitMouse(_THIS) {
	SDL_Mouse* mouse = SDL_GetMouse();

	SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "[OS3_QuitMouse]: Called\n");

	if (mouse->def_cursor) {
		OS3_FreeCursor(mouse->def_cursor);
		mouse->def_cursor = NULL;
	}

	mouse->cur_cursor = NULL;
}

#endif /* SDL_VIDEO_DRIVER_AMIGAOS3 */

