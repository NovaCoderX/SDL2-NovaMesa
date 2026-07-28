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

#include <proto/keymap.h>
#include <proto/diskfont.h>
#include <proto/locale.h>
#include <diskfont/diskfonttag.h>

#include "SDL_os3video.h"
#include "SDL_os3keyboard.h"

#include "../../events/SDL_keyboard_c.h"
#include "../../events/scancodes_amiga.h"

static ULONG* unicodeMappingTable;

static SDL_Keycode OS3_MapRawKey(_THIS, int code) {
	struct InputEvent ie;
	char buffer[2] = { 0, 0 };

	ie.ie_Class = IECLASS_RAWKEY;
	ie.ie_SubClass = 0;
	ie.ie_Code = code;
	ie.ie_Qualifier = 0;
	ie.ie_EventAddress = NULL;

	WORD res = MapRawKey(&ie, (STRPTR )buffer, sizeof(buffer), NULL);
	if (res == 1) {
		return buffer[0];
	}

#ifdef DEBUG_KEYBOARD_VERBOSE
    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "MapRawKey(code %u) returned %d\n", code, res);
#endif

	return 0;
}

ULONG OS3_TranslateUnicode(SDL_VideoDevice* _this, UWORD code, ULONG qualifier, APTR iaddress) {
	struct InputEvent ie;
	char buffer[10];

	ie.ie_Class = IECLASS_RAWKEY;
	ie.ie_SubClass = 0;
	ie.ie_Code = code & ~(IECODE_UP_PREFIX);
	ie.ie_Qualifier = qualifier;
	ie.ie_EventAddress = iaddress; /* Enables dead key support */

	WORD res = MapRawKey(&ie, (STRPTR )buffer, sizeof(buffer), 0);
	if (res == 1) {
		if (unicodeMappingTable) {
			return unicodeMappingTable[(int) buffer[0]];
		} else if (buffer[0] <= 0x7F) {
			// Return just ASCII values which are valid UTF-8
			return buffer[0];
		} else {
			SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "Failed to map ANSI code %u to unicode\n", buffer[0]);
		}
	}

#ifdef DEBUG_KEYBOARD_VERBOSE
    if (res != 1) {
        SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "MapRawKey(code %u, qualifier %u) returned %d\n", code, qualifier, res);
    }
#endif

	return 0;
}

static SDL_bool OS3_IsModifier(SDL_Scancode code) {
	switch (code) {
	case SDL_SCANCODE_LSHIFT:
	case SDL_SCANCODE_RSHIFT:
	case SDL_SCANCODE_CAPSLOCK:
	case SDL_SCANCODE_LCTRL:
	case SDL_SCANCODE_RCTRL:
	case SDL_SCANCODE_LALT:
	case SDL_SCANCODE_RALT:
	case SDL_SCANCODE_LGUI:
	case SDL_SCANCODE_RGUI:
		return SDL_TRUE;
	default:
		return SDL_FALSE;
	}
}

void OS3_ResetNormalKeys(void) {
	SDL_Scancode i = 0;
	int count = 0;
	const Uint8* state = SDL_GetKeyboardState(&count);

	SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Resetting keyboard\n");

	// This is called during fullscreen toggle. Modifier keys keep
	// their state and are synced separately. Releasing all keys could
	// lead to a situation where Alt key would "stick" depending on
	// when user released the key.
	while (i < count) {
		if (state[i] == SDL_PRESSED) {
			if (OS3_IsModifier(i)) {
				SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Ignore pressed modifier key %d\n", i);
			} else {
				SDL_SendKeyboardKey(SDL_RELEASED, i);
			}
		}

		i++;
	}
}

static void OS3_UpdateKeymap(_THIS) {
	SDL_Keycode keymap[SDL_NUM_SCANCODES];

	SDL_GetDefaultKeymap(keymap);

	for (int i = 0; i < SDL_arraysize(amiga_scancode_table); i++) {
		/* Make sure this scancode is a valid character scancode */
		const SDL_Scancode scancode = amiga_scancode_table[i];
		if (scancode == SDL_SCANCODE_UNKNOWN) {
			continue;
		}

		/* If this key is one of the non-mappable keys, ignore it */
		/* Don't allow the number keys right above the qwerty row to translate or the top left key (grave/backquote) */
		/* Not mapping numbers fixes the French layout, giving numeric keycodes for the number keys, which is the expected behavior */
		if ((keymap[scancode] & SDLK_SCANCODE_MASK) || scancode == SDL_SCANCODE_GRAVE /*||
		 (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_0)*/) {
			continue;
		}

		keymap[scancode] = OS3_MapRawKey(_this, i);
	}

	SDL_SetKeymap(0, keymap, SDL_NUM_SCANCODES, SDL_FALSE);
}

void OS3_InitKeyboard(_THIS) {
	SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "[OS3_InitKeyboard]: Called\n");

	OS3_UpdateKeymap(_this);

	//SDL_SetScancodeName(SDL_SCANCODE_APPLICATION, "Menu");
	SDL_SetScancodeName(SDL_SCANCODE_LGUI, "Left Amiga");
	SDL_SetScancodeName(SDL_SCANCODE_RGUI, "Right Amiga");
	SDL_SetScancodeName(SDL_SCANCODE_LCTRL, "Control");

	if (!unicodeMappingTable) {
		struct Locale* locale = OpenLocale(NULL);

		if (locale) {
			const ULONG codeSet = locale->loc_CodeSet ? locale->loc_CodeSet : 4;

			SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Default code set %lu\n", codeSet);

			unicodeMappingTable = (ULONG*) ObtainCharsetInfo(DFCS_NUMBER, codeSet, DFCS_MAPTABLE);

			if (!unicodeMappingTable) {
				SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Failed to get unicode mapping table\n");
			}

			CloseLocale(locale);
		} else {
			SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Failed to open current locale\n");
		}
	}
}

void OS3_QuitKeyboard(_THIS) {
	SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "[OS3_QuitKeyboard]: Called\n");
}

#endif /* SDL_VIDEO_DRIVER_AMIGAOS3 */

