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

#if defined(SDL_VIDEO_OPENGL) && defined(SDL_VIDEO_DRIVER_AMIGAOS3)

#include <GL/gl.h>
#include <GL/amiga_mesa.h>

#include "SDL_os3video.h"
#include "SDL_os3window.h"
#include "SDL_os3opengl.h"

extern void* AmiGetGLProc(const char *proc);


int OS3_GL_LoadLibrary(_THIS, const char* path)
{
    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "OS3_GL_LoadLibrary() - Called\n");

    // Ready to rock and roll.
    return 0;
}

void OS3_GL_UnloadLibrary(_THIS)
{
    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "OS3_GL_UnloadLibrary() - Called\n");
}

void* OS3_GL_GetProcAddress(_THIS, const char* proc)
{
    return AmiGetGLProc(proc);
}

SDL_GLContext OS3_GL_CreateContext(_THIS, SDL_Window* window)
{
	SDL_WindowData* data = window->driverdata;
	AMesaContext* ctx;

	if (!data || !data->syswin) {
		SDL_SetError("No system window available for the OpenGL context");
		return NULL;
	}

	if (data->glContext) {
		SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "Old OpenGL context found for this window, deleting...\n");
		amesa_destroy_context((AMesaContext*)data->glContext);
		data->glContext = NULL;
	}

    ctx = amesa_create_context(data->syswin);
    if (!ctx) {
        SDL_SetError("Failed to create OpenGL context for window: %p", data->syswin);
        return NULL;
    }

    // Success.
    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Created OpenGL context: %p for window: %p\n", ctx, data->syswin);

    // Update window data, this window owns this context.
    data->glContext = ctx;

    // Creating a context is assumed to make it current.
    OS3_GL_MakeCurrent(_this, window, ctx);

    return (SDL_GLContext)ctx;
}

void OS3_GL_DeleteContext(_THIS, SDL_GLContext context)
{
	SDL_Window *sdlwin;

	if (context) {
		SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Deleting OpenGL context: %p\n", context);
		amesa_destroy_context((AMesaContext*)context);

		// Clear the stale pointer from whichever window owned this context.
		for (sdlwin = _this->windows; sdlwin; sdlwin = sdlwin->next) {
			SDL_WindowData* data = sdlwin->driverdata;

			if (data && data->glContext == context) {
				data->glContext = NULL;
			}
		}
	}
}

int OS3_GL_MakeCurrent(_THIS, SDL_Window* window, SDL_GLContext context)
{
    if (context) {
    	SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Making OpenGL context: %p current\n", context);
        amesa_make_current((AMesaContext*)context);
    } else {
    	SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Removing the current OpenGL context\n");
        amesa_make_current(NULL);
    }

    return 0;
}

SDL_bool OS3_GL_UpdateContext(_THIS, SDL_Window* window)
{
	SDL_WindowData* data = (SDL_WindowData*)window->driverdata;

    if (data && data->glContext && data->syswin) {
    	SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Updating OpenGL context: %p to use window: %p\n", data->glContext, data->syswin);
        if (amesa_update_context((AMesaContext*)data->glContext, data->syswin) == GL_TRUE) {
        	return SDL_TRUE;
        }
    }

    return SDL_FALSE;
}

void OS3_GL_GetDrawableSize(_THIS, SDL_Window* window, int* w, int* h)
{
	if (w) *w = window ? window->w : 0;
    if (h) *h = window ? window->h : 0;
}

int OS3_GL_SetSwapInterval(_THIS, int interval)
{
    // Returns -1 if setting the swap interval is not supported.
    return -1;
}

int OS3_GL_GetSwapInterval(_THIS)
{
    // Returns 0 if there is no vertical retrace synchronisation.
	return 0;
}

int OS3_GL_SwapWindow(_THIS, SDL_Window* window)
{
    SDL_WindowData* data = (SDL_WindowData*)window->driverdata;

    if (data && data->glContext) {
        amesa_swap_buffers((AMesaContext*)data->glContext);
    }

    return 0;
}

#endif /* SDL_VIDEO_DRIVER_AMIGAOS3 */


