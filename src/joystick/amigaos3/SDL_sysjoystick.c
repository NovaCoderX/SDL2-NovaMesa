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

#if defined(SDL_JOYSTICK_AMIGAOS3)

#include "SDL_joystick.h"
#include "../SDL_sysjoystick.h"
#include "../SDL_joystick_c.h"

#include "SDL_events.h"

#include <proto/exec.h>
#include <proto/lowlevel.h>
#include <proto/graphics.h>
#include <libraries/lowlevel.h>

/* How many frames we let ReadJoyPort() autosense settle before trusting it */
#define AMIGAOS3_SETTLE_FRAMES 20

#define AMIGAOS3_MAX_JOYSTICKS 2

/* SDL device index -> physical joyport.
 * Physical port 0 is normally the mouse port, port 1 is the traditional
 * joystick port, so we present port 1 as SDL joystick 0. Games that only
 * ever try SDL_JoystickOpen(0) then get the stick, not the mouse. */
#define AMIGAOS3_PORT_FOR_INDEX(index) (1 - (index))

static const ULONG joybut[] = {
    JPF_BUTTON_RED,
    JPF_BUTTON_BLUE,
    JPF_BUTTON_PLAY,
    JPF_BUTTON_YELLOW,
    JPF_BUTTON_GREEN,
    JPF_BUTTON_FORWARD,
    JPF_BUTTON_REVERSE,
};

struct joystick_hwdata {
    ULONG joystate;
    int port; /* physical joyport this stick reads from */
};

static int numberOfJoysticks = 0;
static char joystickNames[AMIGAOS3_MAX_JOYSTICKS][64];
static ULONG joystickTypes[AMIGAOS3_MAX_JOYSTICKS]; /* JP_TYPE_* cached at init/open */

/* Cache the autosensed controller type and name for a device index.
 * GetDeviceName/GetDeviceGUID serve this cache instead of re-polling the
 * hardware (each fresh autosense costs AMIGAOS3_SETTLE_FRAMES frames), which
 * also keeps the name-derived GUID stable for the lifetime of the device. */
static void AMIGAOS3_UpdateCachedPortInfo(int device_index, ULONG data)
{
    ULONG type = data & JP_TYPE_MASK;

    joystickTypes[device_index] = type;

    if (type == JP_TYPE_JOYSTK || type == JP_TYPE_UNKNOWN) {
        SDL_strlcpy(joystickNames[device_index], "Generic joystick", sizeof(joystickNames[device_index]));
    } else if (type == JP_TYPE_GAMECTLR) {
        SDL_strlcpy(joystickNames[device_index], "Generic gamepad", sizeof(joystickNames[device_index]));
    } else if (type == JP_TYPE_MOUSE) {
        SDL_strlcpy(joystickNames[device_index], "Generic mouse", sizeof(joystickNames[device_index]));
    } else {
        SDL_strlcpy(joystickNames[device_index], "Unknown controller", sizeof(joystickNames[device_index]));
    }
}

static int AMIGAOS3_JoystickInit(void)
{
    ULONG portData[AMIGAOS3_MAX_JOYSTICKS];
    int i;

	SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "AMIGAOS3_JoystickInit() - Called\n");

    /* Assuming LowLevelBase is opened elsewhere in your SDL2 Amiga init code.
     * NOTE: the settle loop below uses WaitTOF(), so GfxBase must also be
     * valid by the time SDL_INIT_JOYSTICK runs (it is opened unconditionally
     * during our library init, before any subsystem starts). */
    if (LowLevelBase) {
        numberOfJoysticks = AMIGAOS3_MAX_JOYSTICKS; /* Hard-coded to 2 classic joyports */
    } else {
        numberOfJoysticks = 0;
    }

    if (numberOfJoysticks > 0) {
        /* Let ReadJoyPort() autosense settle, reading both ports in the same
         * pass so init costs AMIGAOS3_SETTLE_FRAMES frames total instead of
         * per port. */
        for (i = 0; i < AMIGAOS3_SETTLE_FRAMES; i++) {
            portData[0] = ReadJoyPort(AMIGAOS3_PORT_FOR_INDEX(0));
            portData[1] = ReadJoyPort(AMIGAOS3_PORT_FOR_INDEX(1));
            WaitTOF();
        }

        for (i = 0; i < numberOfJoysticks; i++) {
            AMIGAOS3_UpdateCachedPortInfo(i, portData[i]);
        }
    }

    for (i = 0; i < numberOfJoysticks; i++) {
        SDL_PrivateJoystickAdded(i);
    }

    return numberOfJoysticks;
}

static int AMIGAOS3_JoystickGetCount(void)
{
    return numberOfJoysticks;
}

static void AMIGAOS3_JoystickDetect(void)
{
	// Nothing to see here.
}

static const char *AMIGAOS3_JoystickGetDeviceName(int device_index)
{
    /* Served from the cache filled at Init/Open - no hardware polling here,
     * this can be called every frame by apps enumerating joystick names. */
    return joystickNames[device_index];
}

static const char* AMIGAOS3_JoystickGetDevicePath(int device_index) { return NULL; }
static int AMIGAOS3_JoystickGetDeviceSteamVirtualGamepadSlot(int device_index) { return -1; }
static int AMIGAOS3_JoystickGetDevicePlayerIndex(int device_index) { return device_index; }
static void AMIGAOS3_JoystickSetDevicePlayerIndex(int device_index, int player_index) {}
static SDL_JoystickID AMIGAOS3_JoystickGetDeviceInstanceID(int device_index) { return device_index; }

static int AMIGAOS3_JoystickOpen(SDL_Joystick *joystick, int device_index)
{
    int port = AMIGAOS3_PORT_FOR_INDEX(device_index);
    ULONG data;
    int i;

	SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "AMIGAOS3_JoystickOpen() - Called\n");

    joystick->hwdata = SDL_calloc(1, sizeof(struct joystick_hwdata));
    if (!joystick->hwdata) {
        return SDL_OutOfMemory();
    }

    /* Re-autosense on open (fresh settle) so a controller plugged in after
     * SDL_Init is still picked up, and refresh the cache to match. */
    for (i = 0; i < AMIGAOS3_SETTLE_FRAMES; i++) {
        data = ReadJoyPort(port);
        WaitTOF();
    }
    AMIGAOS3_UpdateCachedPortInfo(device_index, data);

    if ((data & JP_TYPE_MASK) == JP_TYPE_NOTAVAIL) {
        SDL_free(joystick->hwdata);
        joystick->hwdata = NULL;
        return SDL_SetError("Joystick device %d is not available", device_index);
    }

    /* Reject mice to avoid confusing engines like Exult */
    if ((data & JP_TYPE_MASK) == JP_TYPE_MOUSE) {
        SDL_free(joystick->hwdata);
        joystick->hwdata = NULL;
        return SDL_SetError("Mouse detected on port, rejecting as joystick");
    }

    if ((data & JP_TYPE_MASK) == JP_TYPE_GAMECTLR) {
        joystick->nbuttons = 7;
    } else {
        joystick->nbuttons = 3;
    }

    joystick->naxes = 2;
    joystick->nhats = 0;
    joystick->nballs = 0;
    joystick->hwdata->joystate = 0L;
    joystick->hwdata->port = port;

    SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Opened joystick: %p\n", joystick->hwdata);

    return 0;
}

static void AMIGAOS3_JoystickUpdate(SDL_Joystick *joystick)
{
    ULONG data;
    int i;

    data = ReadJoyPort(joystick->hwdata->port);
    if ((data & JP_TYPE_MASK) != JP_TYPE_NOTAVAIL) {

        /* Axis 1 (Y-Axis) */
        if (data & JPF_JOY_UP) {
            if (!(joystick->hwdata->joystate & JPF_JOY_UP))
                SDL_PrivateJoystickAxis(joystick, 1, -32768);
        } else if (data & JPF_JOY_DOWN) {
            if (!(joystick->hwdata->joystate & JPF_JOY_DOWN))
                SDL_PrivateJoystickAxis(joystick, 1, 32767);
        }
        if (!(data & (JPF_JOY_UP|JPF_JOY_DOWN)) && (joystick->hwdata->joystate & (JPF_JOY_UP|JPF_JOY_DOWN))) {
            SDL_PrivateJoystickAxis(joystick, 1, 0);
        }

        /* Axis 0 (X-Axis) */
        if (data & JPF_JOY_LEFT) {
            if (!(joystick->hwdata->joystate & JPF_JOY_LEFT))
                SDL_PrivateJoystickAxis(joystick, 0, -32768);
        } else if (data & JPF_JOY_RIGHT) {
            if (!(joystick->hwdata->joystate & JPF_JOY_RIGHT))
                SDL_PrivateJoystickAxis(joystick, 0, 32767);
        }
        if (!(data & (JPF_JOY_LEFT|JPF_JOY_RIGHT)) && (joystick->hwdata->joystate & (JPF_JOY_LEFT|JPF_JOY_RIGHT))) {
            SDL_PrivateJoystickAxis(joystick, 0, 0);
        }

        /* Buttons */
        for (i = 0; i < joystick->nbuttons; i++) {
            /* On plain 2/3-button sticks the Blue line can raise a phantom
             * Play, so mask it - but only there: on a real CD32 pad Blue and
             * Play are independent buttons and must not suppress each other. */
            if (data & joybut[i]) {
                if (i == 1 && joystick->nbuttons == 3) data &= (~(joybut[2]));
                if (!(joystick->hwdata->joystate & joybut[i])) {
                    SDL_PrivateJoystickButton(joystick, i, SDL_PRESSED);
                }
            } else if (joystick->hwdata->joystate & joybut[i]) {
                SDL_PrivateJoystickButton(joystick, i, SDL_RELEASED);
            }
        }

        joystick->hwdata->joystate = data;
    } else if (joystick->hwdata->joystate) {
        /* Port stopped responding while input was held - synthesize releases
         * so the app doesn't see a stuck direction or button forever. */
        ULONG oldstate = joystick->hwdata->joystate;

        if (oldstate & (JPF_JOY_UP|JPF_JOY_DOWN)) {
            SDL_PrivateJoystickAxis(joystick, 1, 0);
        }
        if (oldstate & (JPF_JOY_LEFT|JPF_JOY_RIGHT)) {
            SDL_PrivateJoystickAxis(joystick, 0, 0);
        }
        for (i = 0; i < joystick->nbuttons; i++) {
            if (oldstate & joybut[i]) {
                SDL_PrivateJoystickButton(joystick, i, SDL_RELEASED);
            }
        }

        joystick->hwdata->joystate = 0L;
    }
}

static void AMIGAOS3_JoystickClose(SDL_Joystick *joystick)
{
    if (joystick->hwdata) {
    	SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "Closing joystick: %p\n", joystick->hwdata);
        SDL_free(joystick->hwdata);
        joystick->hwdata = NULL;
    }
}

static void AMIGAOS3_JoystickQuit(void)
{
    /* LowLevelBase closure handled in main SDL teardown */
	SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, "AMIGAOS3_JoystickQuit() - Called\n");
}

static SDL_JoystickGUID AMIGAOS3_JoystickGetDeviceGUID(int device_index)
{
    return SDL_CreateJoystickGUID(SDL_HARDWARE_BUS_UNKNOWN, 0, 0, 0, NULL, joystickNames[device_index], 0, 0);
}

static int AMIGAOS3_JoystickRumble(SDL_Joystick *joystick, Uint16 low_frequency_rumble, Uint16 high_frequency_rumble) { return SDL_Unsupported(); }
static int AMIGAOS3_JoystickRumbleTriggers(SDL_Joystick *joystick, Uint16 left_rumble, Uint16 right_rumble) { return SDL_Unsupported(); }
static Uint32 AMIGAOS3_JoystickGetCapabilities(SDL_Joystick *joystick) { return 0; }
static int AMIGAOS3_JoystickSetLED(SDL_Joystick *joystick, Uint8 red, Uint8 green, Uint8 blue) { return SDL_Unsupported(); }
static int AMIGAOS3_JoystickSendEffect(SDL_Joystick *joystick, const void *data, int size) { return SDL_Unsupported(); }
static int AMIGAOS3_JoystickSetSensorsEnabled(SDL_Joystick *joystick, SDL_bool enabled) { return SDL_Unsupported(); }

/* Provide an automatic GameController mapping so ports written against the
 * SDL_GameController API see our sticks/pads. The core turns this into a
 * mapping string via SDL_PrivateGenerateAutomaticControllerMapping().
 *
 * Layout: Red = A, Blue = B, Play = Start; CD32 pads additionally get
 * Green = X, Yellow = Y and Forward/Reverse as the shoulders. The digital
 * directions are exposed both as the D-pad (half-axis bindings) and as the
 * left stick (full-axis bindings), so games reading either just work. */
static SDL_bool AMIGAOS3_JoystickGetGamepadMapping(int device_index, SDL_GamepadMapping *out)
{
    SDL_bool is_gamepad;

    if (device_index < 0 || device_index >= numberOfJoysticks) {
        return SDL_FALSE;
    }

    /* Never map the mouse port as a gamepad */
    if (joystickTypes[device_index] == JP_TYPE_MOUSE) {
        return SDL_FALSE;
    }

    is_gamepad = (joystickTypes[device_index] == JP_TYPE_GAMECTLR) ? SDL_TRUE : SDL_FALSE;

    SDL_zerop(out);

    out->a.kind = EMappingKind_Button;
    out->a.target = 0; /* Red */
    out->b.kind = EMappingKind_Button;
    out->b.target = 1; /* Blue */
    out->start.kind = EMappingKind_Button;
    out->start.target = 2; /* Play */

    if (is_gamepad) {
        out->y.kind = EMappingKind_Button;
        out->y.target = 3; /* Yellow */
        out->x.kind = EMappingKind_Button;
        out->x.target = 4; /* Green */
        out->rightshoulder.kind = EMappingKind_Button;
        out->rightshoulder.target = 5; /* Forward */
        out->leftshoulder.kind = EMappingKind_Button;
        out->leftshoulder.target = 6; /* Reverse */
    }

    out->dpleft.kind = EMappingKind_Axis;
    out->dpleft.target = 0;
    out->dpleft.half_axis_negative = SDL_TRUE;
    out->dpright.kind = EMappingKind_Axis;
    out->dpright.target = 0;
    out->dpright.half_axis_positive = SDL_TRUE;
    out->dpup.kind = EMappingKind_Axis;
    out->dpup.target = 1;
    out->dpup.half_axis_negative = SDL_TRUE;
    out->dpdown.kind = EMappingKind_Axis;
    out->dpdown.target = 1;
    out->dpdown.half_axis_positive = SDL_TRUE;

    out->leftx.kind = EMappingKind_Axis;
    out->leftx.target = 0;
    out->lefty.kind = EMappingKind_Axis;
    out->lefty.target = 1;

    return SDL_TRUE;
}

SDL_JoystickDriver SDL_AMIGAOS3_JoystickDriver =
{
    AMIGAOS3_JoystickInit,
    AMIGAOS3_JoystickGetCount,
    AMIGAOS3_JoystickDetect,
    AMIGAOS3_JoystickGetDeviceName,
    AMIGAOS3_JoystickGetDevicePath,
    AMIGAOS3_JoystickGetDeviceSteamVirtualGamepadSlot,
    AMIGAOS3_JoystickGetDevicePlayerIndex,
    AMIGAOS3_JoystickSetDevicePlayerIndex,
    AMIGAOS3_JoystickGetDeviceGUID,
    AMIGAOS3_JoystickGetDeviceInstanceID,
    AMIGAOS3_JoystickOpen,
    AMIGAOS3_JoystickRumble,
    AMIGAOS3_JoystickRumbleTriggers,
    AMIGAOS3_JoystickGetCapabilities,
    AMIGAOS3_JoystickSetLED,
    AMIGAOS3_JoystickSendEffect,
    AMIGAOS3_JoystickSetSensorsEnabled,
    AMIGAOS3_JoystickUpdate,
    AMIGAOS3_JoystickClose,
    AMIGAOS3_JoystickQuit,
    AMIGAOS3_JoystickGetGamepadMapping
};

#endif /* SDL_JOYSTICK_AMIGAOS3 */
