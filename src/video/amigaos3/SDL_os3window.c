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

#include "SDL_os3video.h"
#include "SDL_os3modes.h"
#include "SDL_os3screen.h"
#include "SDL_os3window.h"

#ifdef SDL_VIDEO_OPENGL
#include "SDL_os3opengl.h"
#endif

#include "SDL_os3mouse.h"
#include "SDL_os3events.h"
#include "SDL_os3keyboard.h"

#include "SDL_syswm.h"
#include "SDL_timer.h"

#include "../../events/SDL_keyboard_c.h"
#include "../../events/SDL_events_c.h"


static SDL_bool OS3_IsFullscreen(SDL_Window* sdlwin)
{
    return (sdlwin->flags & SDL_WINDOW_FULLSCREEN) || (sdlwin->flags & SDL_WINDOW_FULLSCREEN_DESKTOP);
}

static void OS3_SyncWindowSize(SDL_Window* sdlwin)
{
    SDL_WindowData* data = sdlwin->driverdata;
    struct Window* syswin = NULL;

    if (data) {
    	syswin = data->syswin;
    }

    if (syswin) {
        int width = 0;
        int height = 0;

		// Get the inner dimensions.
		width = syswin->Width - syswin->BorderLeft - syswin->BorderRight;
		height = syswin->Height - syswin->BorderTop - syswin->BorderBottom;

		sdlwin->w = width;
		sdlwin->h = height;

		SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Window size has been set to (%dx%d)\n", sdlwin->w, sdlwin->h);
    }
}

static void OS3_CenterWindow(struct Screen* screen, SDL_Window* sdlwin)
{
    if (SDL_WINDOWPOS_ISCENTERED(sdlwin->windowed.x) ||
        SDL_WINDOWPOS_ISUNDEFINED(sdlwin->windowed.x)) {

        sdlwin->x = sdlwin->windowed.x = (screen->Width - sdlwin->windowed.w) / 2;
        SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "X centered %d\n", sdlwin->x);
    }

    if (SDL_WINDOWPOS_ISCENTERED(sdlwin->windowed.y) ||
        SDL_WINDOWPOS_ISUNDEFINED(sdlwin->windowed.y)) {

        sdlwin->y = sdlwin->windowed.y = (screen->Height - sdlwin->windowed.h) / 2;
        SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Y centered %d\n", sdlwin->y);
    }
}

static void OS3_DefineWindowBox(SDL_Window* sdlwin, struct Screen* screen, SDL_bool fullscreen, SDL_Rect* box)
{
    if (fullscreen) {
        box->x = 0;
        box->y = 0;
        box->w = screen->Width;
        box->h = screen->Height;
    } else {
        OS3_CenterWindow(screen, sdlwin);

        box->x = sdlwin->windowed.x;
        box->y = sdlwin->windowed.y;
        box->w = sdlwin->windowed.w;
        box->h = sdlwin->windowed.h;
    }
}

static void OS3_CloseSystemWindow(SDL_Window* sdlwin)
{
	SDL_WindowData* data = sdlwin->driverdata;

	if (data) {
		struct Window* syswin = data->syswin;

		if (syswin) {
			SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Closing system window: %p\n", syswin);
			CloseWindow(syswin);
			data->syswin = NULL;
	    }
	}

	SDL_SendWindowEvent(sdlwin, SDL_WINDOWEVENT_FOCUS_LOST, 0, 0);
}

static SDL_bool OS3_SetupWindowData(SDL_Window* sdlwin, struct Screen* customScreen, struct Window* syswin)
{
    SDL_WindowData* data;

    if (sdlwin->driverdata) {
        data = sdlwin->driverdata;
        SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Old window data %p exists\n", data);
    } else {
    	SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Creating new window data\n");
        data = (SDL_WindowData*) SDL_calloc(1, sizeof(*data));
        if (!data) {
            SDL_OutOfMemory();
            return SDL_FALSE;
        }

        sdlwin->driverdata = data;
    }

    // Set the data.
    data->customScreen = customScreen;
    data->syswin = syswin;

#ifdef DEBUG_VIDEO_VERBOSE
    if (data->customScreen) {
    	SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Updated window data - customScreen: %p %dx%d, syswin %dx%d with borders T%d B%d L%d R%d, sdlwin %dx%d",
			data->customScreen,
			data->customScreen ? data->customScreen->Width  : -1,
			data->customScreen ? data->customScreen->Height : -1,
			syswin->Width, syswin->Height,
			syswin->BorderTop, syswin->BorderBottom,
			syswin->BorderLeft, syswin->BorderRight,
			sdlwin->w, sdlwin->h);
    } else {
    	SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Updated window data - syswin %dx%d with borders T%d B%d L%d R%d, sdlwin %dx%d",
			syswin->Width, syswin->Height,
			syswin->BorderTop, syswin->BorderBottom,
			syswin->BorderLeft, syswin->BorderRight,
			sdlwin->w, sdlwin->h);
    }
#endif

    return SDL_TRUE;
}

static void OS3_SeedMousePosition(SDL_Window *sdlwin, struct Window *syswin)
{
    /* Until the user moves the mouse, the input handler accumulates no
       deltas and no motion event is sent - SDL believes the pointer is
       at (0,0) while the real Intuition pointer sits wherever the user
       last left it (e.g. screen centre after a Workbench double-click).
       Seed SDL's state with the true position so the app's cursor
       starts in the right place. Desktop backends do the equivalent by
       querying the OS pointer on window creation/focus. */
    int wx = syswin->MouseX - syswin->BorderLeft;
    int wy = syswin->MouseY - syswin->BorderTop;
    int innerW = syswin->Width  - syswin->BorderLeft - syswin->BorderRight;
    int innerH = syswin->Height - syswin->BorderTop  - syswin->BorderBottom;

    /* Clamp - matches the confined-pointer path in PumpEvents */
    if (wx < 0)            wx = 0;
    else if (wx >= innerW) wx = innerW - 1;
    if (wy < 0)            wy = 0;
    else if (wy >= innerH) wy = innerH - 1;

    SDL_SendMouseMotion(sdlwin, 0, 0, wx, wy);
}

static SDL_bool OS3_CreateSystemWindow(_THIS, SDL_Window* sdlwin, SDL_bool fullscreen)
{
	struct Screen* screen = NULL;
	struct Window* syswin = NULL;
	SDL_Rect box;

    if (fullscreen) {
        SDL_bool desktop_fs = (sdlwin->flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP;
        if (desktop_fs) {
            // FULLSCREEN_DESKTOP: open screen at desktop size — SDL will resize window to match the desktop resolution.
        	SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Trying to create a system window (fullscreen desktop mode)\n");
            SDL_VideoDisplay* display = SDL_GetDisplay(0);

            // Sanity check, shouldn't be possible.
			if (!display) {
				SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "Could not get the primary display data\n");
				return SDL_FALSE;
			}

            screen = OS3_CreateCustomScreen(display->desktop_mode.w, display->desktop_mode.h);
        } else {
        	 // True FULLSCREEN: open screen at the requested game resolution.
			SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Trying to create a system window (fullscreen non-desktop mode)\n");
            screen = OS3_CreateCustomScreen(sdlwin->w, sdlwin->h);
        }
    } else {
    	SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Trying to create a system window (windowed mode)\n");
    	screen = OS3_LockPublicScreen();
    }

    if (!screen) {
    	// Cannot do much without a screen....
    	return SDL_FALSE;
    }

	OS3_DefineWindowBox(sdlwin, screen, fullscreen, &box);

	if (fullscreen) {
		syswin = OpenWindowTags(
			NULL,
			WA_CustomScreen, (ULONG)screen,
			WA_Left, box.x,
			WA_Top, box.y,
			WA_Width, box.w,
			WA_Height, box.h,
			WA_Backdrop, TRUE,
			WA_Borderless, TRUE,
			WA_Activate, TRUE,
			WA_SimpleRefresh, TRUE,
			WA_NoCareRefresh, TRUE,
			WA_ReportMouse, FALSE,
			WA_RMBTrap, TRUE,
			WA_IDCMP, IDCMP_CLOSEWINDOW|IDCMP_ACTIVEWINDOW|IDCMP_INACTIVEWINDOW,
			TAG_DONE);
	} else {
		syswin = OpenWindowTags(
			NULL,
			WA_PubScreen, (ULONG)screen,
			WA_Title, (ULONG)sdlwin->title,
			WA_Left, box.x,
			WA_Top, box.y,
			WA_InnerWidth, box.w,
			WA_InnerHeight, box.h,
			WA_Backdrop, FALSE,
			WA_Borderless, (sdlwin->flags & SDL_WINDOW_BORDERLESS) ? TRUE : FALSE,
			WA_DragBar, (sdlwin->flags & SDL_WINDOW_BORDERLESS) ? FALSE : TRUE,
			WA_DepthGadget, (sdlwin->flags & SDL_WINDOW_BORDERLESS) ? FALSE : TRUE,
			WA_CloseGadget, (sdlwin->flags & SDL_WINDOW_BORDERLESS) ? FALSE : TRUE,
			WA_Activate, TRUE,
			WA_SimpleRefresh, TRUE,
			WA_NoCareRefresh, TRUE,
			WA_ReportMouse, FALSE,
			WA_RMBTrap, TRUE,
			WA_IDCMP, IDCMP_CLOSEWINDOW|IDCMP_ACTIVEWINDOW|IDCMP_INACTIVEWINDOW|IDCMP_NEWSIZE,
			TAG_DONE);

		// Finished with this now.
		OS3_UnlockPublicScreen(screen);
		screen = NULL;
	}

    if (!syswin) {
    	SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "Could not create system window\n");

		// We have to manually clean up here because we don't have valid window data.
		if (screen) {
			CloseScreen(screen);
			screen = NULL;
		}

    	return SDL_FALSE;
    }

    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Opened system window: %p - x=%d, y=%d, width=%d, height=%d\n",
    		syswin, box.x, box.y, box.w, box.h);

	if (screen) {
		// Need to set the custom screen black because it could be bigger than what was asked for.
		ULONG blackPen = ObtainBestPen(screen->ViewPort.ColorMap, 0, 0, 0, TAG_DONE);
		SetAPen(syswin->RPort, blackPen);
		RectFill(syswin->RPort, 0, 0, syswin->Width - 1, syswin->Height - 1);
		ReleasePen(screen->ViewPort.ColorMap, blackPen);
	} else {
		// In windowed mode on a public screen.
		ULONG blackPen = ObtainBestPen(syswin->WScreen->ViewPort.ColorMap, 0, 0, 0, TAG_DONE);
		SetAPen(syswin->RPort, blackPen);

		// Fill only the inner window area to preserve window borders and gadgets.
		RectFill(syswin->RPort,
				 syswin->BorderLeft,
				 syswin->BorderTop,
				 syswin->Width - syswin->BorderRight - 1,
				 syswin->Height - syswin->BorderBottom - 1);

		ReleasePen(syswin->WScreen->ViewPort.ColorMap, blackPen);
	}

	// Setup the window data structure.
	if (!OS3_SetupWindowData(sdlwin, screen, syswin)) {
		SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "Could not setup window data\n");

		// We have to manually clean up here because we don't have valid window data.
		CloseWindow(syswin);
		syswin = NULL;

		if (screen) {
			CloseScreen(screen);
			screen = NULL;
		}

		return SDL_FALSE;
	}

	// Make sure the new window is activated.
	if (screen) {
		ScreenToFront(screen);
	}

	WindowToFront(syswin);
	ActivateWindow(syswin);
	OS3_SeedMousePosition(sdlwin, syswin);

	// All is cool.
    return SDL_TRUE;
}

int OS3_CreateWindow(_THIS, SDL_Window* sdlwin)
{
	SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "OS3_CreateWindow() - Called\n");

	// Sanity check, shouldn't be possible.
    if (OS3_IsFullscreen(sdlwin)) {
    	return SDL_SetError("Unexpected 'SDL_WINDOW_FULLSCREEN' flag passed to OS3_CreateWindow()");
    }

	if (!OS3_CreateSystemWindow(_this, sdlwin, SDL_FALSE)) {
		return SDL_SetError("Failed to create window");
	}

    // Sync the size of the SDL window to the size of the initial intuition window.
	OS3_SyncWindowSize(sdlwin);

    // Success.
    return 0;
}

void OS3_SetWindowFullscreen(_THIS, SDL_Window* sdlwin, SDL_VideoDisplay* display, SDL_bool fullscreen)
{
	SDL_WindowData* data = sdlwin->driverdata;
	SDL_bool currently_fullscreen = (data && data->customScreen);
	SDL_bool desktop_fs = (sdlwin->flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP;
	SDL_bool mode_changed = SDL_TRUE;
#ifdef SDL_VIDEO_OPENGL
	SDL_bool current_context = SDL_FALSE;
#endif

	SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "OS3_SetWindowFullscreen() - Called\n");

	if (sdlwin->is_destroying) {
        // This function also gets called during window closing
        SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Window '%s' is being destroyed, skipping mode change...\n", sdlwin->title);
        return;
	}

	// See if we actually need to toggle the mode.
	if (fullscreen) {
		if (currently_fullscreen && data && data->syswin) {
			SDL_VideoDisplay* display = SDL_GetDisplay(0);

			// Sanity check, shouldn't be possible.
		    if (!display) {
		    	SDL_SetError("Could not get the primary display data");
		    	return;
		    }

			if (desktop_fs) {
				if (data->customScreen->Width == display->desktop_mode.w && data->customScreen->Height == display->desktop_mode.h) {
					mode_changed = SDL_FALSE;
				}
			} else {
				if (data->customScreen->Width == sdlwin->w && data->customScreen->Height == sdlwin->h) {
					mode_changed = SDL_FALSE;
				}
			}
		}
	} else {
		if ((!currently_fullscreen) && data && data->syswin) {
			mode_changed = SDL_FALSE;
		}
	}

	if (!mode_changed) {
		SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Already in the requested mode, skipping mode change...\n");
		return;
	}

	SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Trying to change window '%s' to %s mode\n",
			sdlwin->title, fullscreen ? "fullscreen" : "windowed");

#ifdef SDL_VIDEO_OPENGL
	// Need to unset the current OpenGL context if it is being used by this window.
	if (data && data->glContext && data->glContext == SDL_GL_GetCurrentContext()) {
		current_context = SDL_TRUE;
		SDL_GL_MakeCurrent(sdlwin, NULL);
	}
#endif

	// Close whatever is currently open before transitioning.
	OS3_CloseSystemWindow(sdlwin);
	OS3_CloseCustomScreen(sdlwin);

	if (fullscreen) {
		if (!OS3_CreateSystemWindow(_this, sdlwin, SDL_TRUE)) {
			SDL_SetError("Failed to create window");
			return;
		}
	} else {
		if (!OS3_CreateSystemWindow(_this, sdlwin, SDL_FALSE)) {
			SDL_SetError("Failed to create window");
			return;
		}
	}

#ifdef SDL_VIDEO_OPENGL
	// Reapply the OpenGL context.
	if (current_context) {
		// Update the OpenGL context with the new window dimensions.
		if (!OS3_GL_UpdateContext(_this, sdlwin)) {
			SDL_SetError("Failed to update the OpenGL context for the new window");
			return;
		}

		// Need to tell SDL that this new window is using the current context.
		SDL_GL_MakeCurrent(sdlwin, (SDL_GLContext)data->glContext);
	}
#endif

	/* Reapply cursor/grab state to new window */
	OS3_RefreshCursorState();

	// Release sticky keys.
	OS3_ResetNormalKeys();

	// All is cool.
	SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Window '%s' has successfully switched mode\n", sdlwin->title);
}

void OS3_SetWindowTitle(_THIS, SDL_Window* sdlwin)
{
    SDL_WindowData* data = sdlwin->driverdata;

    if (data->syswin) {
    	char* title = sdlwin->title ? sdlwin->title : "Nova";

    	SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Setting window title to '%s'\n", title);
        SetWindowTitles(data->syswin, (STRPTR)title, (STRPTR)title);
    }
}

void OS3_SetWindowSize(_THIS, SDL_Window *sdlwin)
{
    SDL_WindowData* data = sdlwin->driverdata;

    if (data && data->syswin && (!data->customScreen)) {
		struct Window* syswin = data->syswin;

		SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "New window size width=%d, height=%d\n", sdlwin->w, sdlwin->h);

		/* ChangeWindowBox takes OUTER dimensions - add the borders */
		ChangeWindowBox(syswin,
			syswin->LeftEdge, syswin->TopEdge,
			sdlwin->w + syswin->BorderLeft + syswin->BorderRight,
			sdlwin->h + syswin->BorderTop  + syswin->BorderBottom);

		/* NOTE: asynchronous - completion arrives as IDCMP_NEWSIZE */
    }
}

void OS3_RaiseWindow(_THIS, SDL_Window* sdlwin)
{
    SDL_WindowData* data = sdlwin->driverdata;

    if (data->syswin) {
        WindowToFront(data->syswin);
        ActivateWindow(data->syswin);
    }
}

void OS3_SetWindowMouseGrab(_THIS, SDL_Window* sdlwin, SDL_bool grabbed)
{
	SDL_WindowData* data = sdlwin->driverdata;

	if (data->syswin) {
		OS3_SetMouseGrab(grabbed);
	}
}

void OS3_SetWindowKeyboardGrab(_THIS, SDL_Window* sdlwin, SDL_bool grabbed)
{
	SDL_WindowData* data = sdlwin->driverdata;

	if (data->syswin) {
		OS3_SetKeyboardGrab(grabbed);
	}
}

void OS3_DestroyWindow(_THIS, SDL_Window* sdlwin)
{
    SDL_WindowData* data = sdlwin->driverdata;

    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "OS3_DestroyWindow() - Called for '%s'\n", sdlwin->title);

    if (data) {
		if (!(sdlwin->flags & SDL_WINDOW_FOREIGN)) {
			OS3_CloseSystemWindow(sdlwin);
			OS3_CloseCustomScreen(sdlwin);
		}

		SDL_free(data);
    }

    sdlwin->driverdata = NULL;
}

SDL_bool OS3_GetWindowWMInfo(_THIS, SDL_Window* sdlwin, struct SDL_SysWMinfo* info)
{
    if (info->version.major <= SDL_MAJOR_VERSION) {
        struct Window* syswin = ((SDL_WindowData*)sdlwin->driverdata)->syswin;

        info->subsystem = SDL_SYSWM_OS3;
        info->info.os3.window = syswin;

        return SDL_TRUE;
    } else {
        SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Application not compiled with SDL %d.%d\n",
            SDL_MAJOR_VERSION, SDL_MINOR_VERSION);

        SDL_SetError("Application not compiled with SDL %d.%d",
            SDL_MAJOR_VERSION, SDL_MINOR_VERSION);

        return SDL_FALSE;
    }
}

int OS3_GetWindowBordersSize(_THIS, SDL_Window* sdlwin, int* top, int* left, int* bottom, int* right)
{
    SDL_WindowData* data = sdlwin->driverdata;
    struct Window* syswin;

    if (!data || !data->syswin) {
    	return -1;
    }

	syswin = data->syswin;

    if (top) {
        *top = syswin->BorderTop;
    }

    if (left) {
        *left = syswin->BorderLeft;
    }

    if (bottom) {
        *bottom = syswin->BorderBottom;
    }

    if (right) {
        *right = syswin->BorderRight;
    }

    return 0;
}

void OS3_SetWindowAlwaysOnTop(_THIS, SDL_Window* sdlwin, SDL_bool on_top)
{
    SDL_WindowData* data = sdlwin->driverdata;

    if (data->syswin && on_top) {
        WindowToFront(data->syswin);
    }
}

#endif /* SDL_VIDEO_DRIVER_AMIGAOS3 */

