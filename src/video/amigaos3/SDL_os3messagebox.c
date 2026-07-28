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

#include "SDL_messagebox.h"
#include "SDL_os3messagebox.h"
#include "SDL_os3window.h"

#define BUTTON_BUF_SIZE 1024

static char* OS3_MakeButtonString(const SDL_MessageBoxData* messageboxdata) {
	char* buttonBuffer = SDL_malloc(BUTTON_BUF_SIZE);

	if (buttonBuffer) {
		int b;

		SDL_memset(buttonBuffer, 0, BUTTON_BUF_SIZE);

		/* Generate "Button1|Button2... "*/
		for (b = 0; b < messageboxdata->numbuttons; b++) {
			SDL_strlcat(buttonBuffer, messageboxdata->buttons[b].text, BUTTON_BUF_SIZE);

			if (b != (messageboxdata->numbuttons - 1)) {
				SDL_strlcat(buttonBuffer, "|", BUTTON_BUF_SIZE);
			}
		}

		SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Gadget String '%s'\n", buttonBuffer);
	}

	return buttonBuffer;
}

static struct Window* OS3_GetWindow(const SDL_MessageBoxData* messageboxdata) {
	struct Window* syswin = NULL;

	if (messageboxdata->window && messageboxdata->window->driverdata) {
		SDL_WindowData* data = messageboxdata->window->driverdata;
		syswin = data->syswin;
	}

	return syswin;
}

int OS3_ShowMessageBox(const SDL_MessageBoxData* messageboxdata, int* buttonid) {
	char* buttonString;
	int result = -1;

	if ((buttonString = OS3_MakeButtonString(messageboxdata))) {
		struct EasyStruct es = {
			sizeof(struct EasyStruct),
			0, // Flags
			(STRPTR)(messageboxdata->title ? messageboxdata->title : "Alert"),
			(STRPTR)"%s", // Force safe string formatting
			(STRPTR)buttonString
		};

		const int LAST_BUTTON = messageboxdata->numbuttons;

		/* Pass the actual message via the args array to prevent format string vulnerabilities */
		APTR args[1] = { (APTR)(messageboxdata->message ? messageboxdata->message : "") };

		int amigaButton = EasyRequestArgs(OS3_GetWindow(messageboxdata), &es, NULL, args);

		SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Amiga button %d chosen\n", amigaButton);

		if (buttonid) {
			/* If they closed the window or picked a malicious gadget ID out of bounds */
			if (amigaButton >= 0 && amigaButton <= LAST_BUTTON) {
				if (amigaButton == 0 && LAST_BUTTON > 1) {
					/* On Amiga, 0 denotes the final rightmost button when multiple gadgets exist */
					*buttonid = messageboxdata->buttons[LAST_BUTTON - 1].buttonid;
				} else {
					/* Otherwise it maps sequentially offset by 1 */
					*buttonid = messageboxdata->buttons[amigaButton - 1].buttonid;
				}

				SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Mapped to SDL button id %d\n", *buttonid);
			} else {
				*buttonid = -1;
			}
		}

		SDL_free(buttonString);
		result = 0;
	}

	return result;
}

#endif /* SDL_VIDEO_DRIVER_AMIGAOS3 */
