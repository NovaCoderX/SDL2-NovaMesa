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
#include <proto/wb.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <proto/intuition.h>
#include <proto/icon.h>
#include <proto/keymap.h>
#include <proto/dos.h>
#include <proto/Picasso96.h>
#include <libraries/Picasso96.h>

#include "SDL_video.h"
#include "SDL_hints.h"

#include "../SDL_sysvideo.h"
#include "../SDL_pixels_c.h"
#include "../../events/SDL_events_c.h"

#include "SDL_os3video.h"
#include "SDL_os3modes.h"
#include "SDL_os3window.h"

#ifdef SDL_VIDEO_OPENGL
#include "SDL_os3opengl.h"
#endif

#include "SDL_os3framebuffer.h"
#include "SDL_os3events.h"
#include "SDL_os3mouse.h"
#include "SDL_os3keyboard.h"
#include "SDL_os3messagebox.h"

#define OS3VID_DRIVER_NAME "AmigaOS 3"

static int OS3_VideoInit(_THIS);
static void OS3_VideoQuit(_THIS);


// Kept as defines so the library name only has to be changed in one place.
#define P96_LIBRARY_NAME "Picasso96API.library"
#define P96_LIBRARY_VERSION 2

struct Library *P96Base = NULL;

SDL_bool isP96Mode(ULONG modeId) {
	if (modeId == INVALID_ID) {
		return SDL_FALSE;
	}

	return p96GetModeIDAttr(modeId, P96IDA_ISP96) ? SDL_TRUE : SDL_FALSE;
}

SDL_bool isUsableP96Mode(ULONG modeId) {
	if (!isP96Mode(modeId)) {
		return SDL_FALSE;
	}

	if (ModeNotAvailable(modeId)) {
		return SDL_FALSE;
	}

	return SDL_TRUE;
}

SDL_bool isPixelFormat32(ULONG pixelFormat) {
	switch (pixelFormat)
	{
		case RGBFB_A8R8G8B8:
		case RGBFB_A8B8G8R8:
		case RGBFB_R8G8B8A8:
		case RGBFB_B8G8R8A8:
			return SDL_TRUE;

		default:
			return SDL_FALSE;
	}
}

ULONG getP96Mode32(int width, int height) {
	ULONG modeId = INVALID_ID;
	ULONG nextid = NextDisplayInfo(INVALID_ID);

	while (nextid != INVALID_ID) {
		if (isUsableP96Mode(nextid)) {
			if (isPixelFormat32(p96GetModeIDAttr(nextid, P96IDA_RGBFORMAT))) {
				if (p96GetModeIDAttr(nextid, P96IDA_WIDTH) == (ULONG)width) {
					if (p96GetModeIDAttr(nextid, P96IDA_HEIGHT) == (ULONG)height) {
						// Success, exact match.
						modeId = nextid;
						break;
					}
				}
			}
		}

		nextid = NextDisplayInfo(nextid);
	}

	return modeId;
}

static void OS3_FindApplicationName(_THIS) {
	SDL_VideoData* data = (SDL_VideoData*) _this->driverdata;

	size_t size;
	const size_t maxPathLen = 255;
	char pathBuffer[maxPathLen];
	struct Task* me = NULL;

	me = FindTask(NULL);
	SDL_snprintf(pathBuffer, maxPathLen, "%s", ((struct Node*) me)->ln_Name);
	size = SDL_strlen(pathBuffer) + 1;

	data->appName = SDL_malloc(size);
	if (data->appName) {
		SDL_snprintf((char*) data->appName, size, (char*) pathBuffer);
	}

	SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Application name: '%s'\n", data->appName);
}

static void OS3_RegisterApplication(_THIS) {
	SDL_VideoData* data = (SDL_VideoData*) _this->driverdata;

	data->appId = 69;
}

static void OS3_UnregisterApplication(_THIS) {
	SDL_VideoData* data = (SDL_VideoData*) _this->driverdata;

	data->appId = 0;
}

static SDL_bool OS3_AllocSystemResources(_THIS) {
	SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "OS3_AllocSystemResources() - Called\n");

	OS3_FindApplicationName(_this);
	OS3_RegisterApplication(_this);

	return SDL_TRUE;
}

static void OS3_FreeSystemResources(_THIS) {
	SDL_VideoData* data = (SDL_VideoData*) _this->driverdata;

	SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "OS3_FreeSystemResources() - Called\n");

	if (data->appName) {
		SDL_free(data->appName);
	}

	if (data->appId) {
		OS3_UnregisterApplication(_this);
	}
}

static void OS3_DeleteDevice(SDL_VideoDevice* device) {
	OS3_FreeSystemResources(device);
	SDL_free(device->driverdata);
	SDL_free(device);
}

static void OS3_SetMesaGLFunctions(SDL_VideoDevice* device) {
#ifdef SDL_VIDEO_OPENGL
	device->GL_LoadLibrary = OS3_GL_LoadLibrary;
	device->GL_GetProcAddress = OS3_GL_GetProcAddress;
	device->GL_UnloadLibrary = OS3_GL_UnloadLibrary;
	device->GL_CreateContext = OS3_GL_CreateContext;
	device->GL_MakeCurrent = OS3_GL_MakeCurrent;
	device->GL_GetDrawableSize = OS3_GL_GetDrawableSize;
	device->GL_SetSwapInterval = OS3_GL_SetSwapInterval;
	device->GL_GetSwapInterval = OS3_GL_GetSwapInterval;
	device->GL_SwapWindow = OS3_GL_SwapWindow;
	device->GL_DeleteContext = OS3_GL_DeleteContext;
	device->GL_DefaultProfileConfig = NULL;
#endif
}

static void OS3_SetFunctionPointers(SDL_VideoDevice* device) {
	device->VideoInit = OS3_VideoInit;
	device->VideoQuit = OS3_VideoQuit;
	device->GetDisplayBounds = OS3_GetDisplayBounds;
	device->GetDisplayUsableBounds = NULL;
	device->GetDisplayDPI = NULL;
	device->GetDisplayModes = OS3_GetDisplayModes;
	device->SetDisplayMode = OS3_SetDisplayMode;
	device->CreateSDLWindow = OS3_CreateWindow;
	device->CreateSDLWindowFrom = NULL;
	device->SetWindowTitle = OS3_SetWindowTitle;
	device->SetWindowIcon = NULL;
	device->SetWindowPosition = NULL;
	device->SetWindowSize = OS3_SetWindowSize;
	device->SetWindowMinimumSize = NULL;
	device->SetWindowMaximumSize = NULL;
	device->GetWindowBordersSize = OS3_GetWindowBordersSize;
	device->SetWindowOpacity = NULL;
	device->SetWindowModalFor = NULL;
	device->SetWindowInputFocus = NULL;
	device->ShowWindow = NULL;
	device->HideWindow = NULL;
	device->RaiseWindow = OS3_RaiseWindow;
	device->MaximizeWindow = NULL;
	device->MinimizeWindow = NULL;
	device->RestoreWindow = NULL;
	device->SetWindowBordered = NULL;
	device->SetWindowAlwaysOnTop = OS3_SetWindowAlwaysOnTop;
	device->SetWindowFullscreen = OS3_SetWindowFullscreen;
	device->SetWindowGammaRamp = NULL;
	device->GetWindowGammaRamp = NULL;
	device->SetWindowMouseGrab = OS3_SetWindowMouseGrab;
	device->SetWindowKeyboardGrab = OS3_SetWindowKeyboardGrab;
	device->DestroyWindow = OS3_DestroyWindow;
	device->CreateWindowFramebuffer = OS3_CreateWindowFramebuffer;
	device->UpdateWindowFramebuffer = OS3_UpdateWindowFramebuffer;
	device->DestroyWindowFramebuffer = OS3_DestroyWindowFramebuffer;
	device->OnWindowEnter = NULL;
	device->FlashWindow = NULL;
	device->GetWindowWMInfo = OS3_GetWindowWMInfo;

	//  OpenGL support
	OS3_SetMesaGLFunctions(device);

	device->PumpEvents = OS3_PumpEvents;
	device->SuspendScreenSaver = NULL;
	device->StartTextInput = NULL;
	device->StopTextInput = NULL;
	device->SetTextInputRect = NULL;
	device->SetTextInputRect = NULL;
	device->ShowScreenKeyboard = NULL;
	device->HideScreenKeyboard = NULL;
	device->IsScreenKeyboardShown = NULL;
	device->SetClipboardText = NULL;
	device->GetClipboardText = NULL;
	device->HasClipboardText = NULL;
	device->ShowMessageBox = NULL;
	device->SetWindowHitTest = NULL;
	device->AcceptDragAndDrop = NULL;
	device->free = OS3_DeleteDevice;
}

static SDL_VideoDevice* OS3_CreateDevice(void) {
	SDL_VideoDevice* device;
	SDL_VideoData* data;

	/* Initialize all variables that we clean on shutdown */
	device = (SDL_VideoDevice*) SDL_calloc(1, sizeof(SDL_VideoDevice));

	if (device) {
		data = (SDL_VideoData*) SDL_calloc(1, sizeof(SDL_VideoData));
	} else {
		data = NULL;
	}

	if (!data) {
		SDL_free(device);
		SDL_OutOfMemory();
		return NULL;
	}

	device->driverdata = data;

	if (!OS3_AllocSystemResources(device)) {
		/* If we return with NULL, SDL_VideoQuit() can't clean up OS3 stuff. So let's do it now. */
		OS3_FreeSystemResources(device);
		SDL_free(device);
		SDL_free(data);
		SDL_Unsupported();
		return NULL;
	}

	OS3_SetFunctionPointers(device);

	return device;
}

VideoBootStrap OS3_bootstrap = {
OS3VID_DRIVER_NAME, "AmigaOS 3 video driver", OS3_CreateDevice,
OS3_ShowMessageBox
};

int OS3_VideoInit(_THIS) {
	SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "OS3_VideoInit() - Called\n");

	P96Base = OpenLibrary((CONST_STRPTR)P96_LIBRARY_NAME, P96_LIBRARY_VERSION);
	if (!P96Base) {
		return SDL_SetError("Can't open " P96_LIBRARY_NAME);
	}

	if (OS3_InitModes(_this) < 0) {
		return SDL_SetError("Failed to initialize display modes");
	}

	OS3_InitKeyboard(_this);
	OS3_InitMouse(_this);

	if (OS3_InitEvents(_this) < 0) {
		return SDL_SetError("Failed to initialize events");
	}

	SDL_LogInfo(SDL_LOG_CATEGORY_VIDEO, "Disable SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS\n");

	// We don't want SDL to change  window setup in SDL_OnWindowFocusLost()
	SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");

	SDL_LogInfo(SDL_LOG_CATEGORY_VIDEO, "Disable SDL_POLL_SENTINEL\n");

	// Poll sentinels added after SDL 2.0.14 cause increasing CPU load (TODO: fix)
	SDL_SetHint(SDL_HINT_POLL_SENTINEL, "0");

	SDL_LogInfo(SDL_LOG_CATEGORY_VIDEO, "Disable SDL_FRAMEBUFFER_ACCELERATION\n");

	// Avoid creation of accelerated ("compositing") surfaces when using "software" driver.
	SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0");

	return 0;
}

void OS3_VideoQuit(_THIS) {
	SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "OS3_VideoQuit() - Called\n");

	OS3_QuitEvents(_this);
	OS3_QuitMouse(_this);
	OS3_QuitKeyboard(_this);
	OS3_QuitModes(_this);

	if (P96Base) {
		CloseLibrary(P96Base);
		P96Base = NULL;
	}
}

#endif /* SDL_VIDEO_DRIVER_AMIGAOS3 */

