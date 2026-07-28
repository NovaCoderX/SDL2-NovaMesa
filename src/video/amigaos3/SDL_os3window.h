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

#ifndef _SDL_os3window_h
#define _SDL_os3window_h

#include "../SDL_sysvideo.h"

typedef struct {
	struct Screen* 		customScreen;
	struct Window* 		syswin;
	int 				originalX, originalY, originalW, originalH;
	void* 				framebuffer;
#if defined(SDL_VIDEO_OPENGL)
	void*				glContext;
#endif
} SDL_WindowData;

extern int OS3_CreateWindow(_THIS, SDL_Window* window);
extern void OS3_SetWindowTitle(_THIS, SDL_Window* window);
extern void OS3_SetWindowSize(_THIS, SDL_Window* window);
extern void OS3_RaiseWindow(_THIS, SDL_Window* window);
extern void OS3_SetWindowAlwaysOnTop(_THIS, SDL_Window* window, SDL_bool on_top);
extern void OS3_SetWindowFullscreen(_THIS, SDL_Window* window, SDL_VideoDisplay* display, SDL_bool fullscreen);
extern void OS3_SetWindowMouseGrab(_THIS, SDL_Window* window, SDL_bool grabbed);
extern void OS3_SetWindowKeyboardGrab(_THIS, SDL_Window* window, SDL_bool grabbed);
extern void OS3_DestroyWindow(_THIS, SDL_Window* window);
extern int OS3_GetWindowBordersSize(_THIS, SDL_Window* window, int* top, int* left, int* bottom, int* right);
extern SDL_bool OS3_GetWindowWMInfo(_THIS, SDL_Window* window, struct SDL_SysWMinfo* info);

#endif /* _SDL_os3window_h */

