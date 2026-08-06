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
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/keymap.h>
#include <proto/wb.h>
#include <proto/lowlevel.h>
#include <proto/input.h>
#include <clib/alib_protos.h>
#include <devices/input.h>
#include <SDI_interrupt.h>

#include "SDL_os3video.h"
#include "SDL_os3mouse.h"
#include "SDL_os3window.h"
#include "SDL_os3events.h"
#include "SDL_os3keyboard.h"

#include "../../events/SDL_keyboard_c.h"
#include "../../events/SDL_mouse_c.h"
#include "../../events/SDL_windowevents_c.h"
#include "../../events/scancodes_amiga.h"
#include "../../events/SDL_events_c.h"

#include "SDL_version.h"

#define RAWKEY_NM_WHEEL_UP      0x7A
#define RAWKEY_NM_WHEEL_DOWN    0x7B
#define RAWKEY_NM_WHEEL_LEFT    0x7C
#define RAWKEY_NM_WHEEL_RIGHT   0x7D
#define RAWKEY_NM_BUTTON_FOURTH 0x7E

extern OS3_GlobalMouseState globalMouseState;

struct Device *InputBase = NULL;

static struct Interrupt *inputHandler = NULL;
static struct MsgPort *inputPort = NULL;
static struct IOStdReq *inputRequest = NULL;
static char inputHandlerName[] = "SDL Input Handler";

#define MAXIMSGS 64
static struct InputEvent imsgs[MAXIMSGS];
static volatile int imsglow = 0;
static volatile int imsghigh = 0;
static volatile int mouse_movement_x, mouse_movement_y = 0;
static SDL_bool isWindowActive = SDL_FALSE;
static SDL_bool isMouseInputCaptured = SDL_FALSE;
static SDL_bool isKeyboardInputCaptured = SDL_FALSE;

static void OS3_SyncKeyModifier(SDL_bool held, SDL_Scancode scancode)
{
    const Uint8 *keystate = SDL_GetKeyboardState(NULL);
    SDL_bool sdlHeld = keystate[scancode] ? SDL_TRUE : SDL_FALSE;

    if (held != sdlHeld) {
        /* Synthesize the transition the window never received.
           SDL_SendKeyboardKey updates both the key state array and
           the KMOD_* modifier mask for modifier scancodes. */
        SDL_SendKeyboardKey(held ? SDL_PRESSED : SDL_RELEASED, scancode);
    }
}

static void OS3_SyncKeyModifiers(_THIS)
{
    const UWORD q = PeekQualifier();

    OS3_SyncKeyModifier((q & IEQUALIFIER_LSHIFT)   != 0, SDL_SCANCODE_LSHIFT);
    OS3_SyncKeyModifier((q & IEQUALIFIER_RSHIFT)   != 0, SDL_SCANCODE_RSHIFT);
    OS3_SyncKeyModifier((q & IEQUALIFIER_CONTROL)  != 0, SDL_SCANCODE_LCTRL);
    OS3_SyncKeyModifier((q & IEQUALIFIER_LALT)     != 0, SDL_SCANCODE_LALT);
    OS3_SyncKeyModifier((q & IEQUALIFIER_RALT)     != 0, SDL_SCANCODE_RALT);
    OS3_SyncKeyModifier((q & IEQUALIFIER_LCOMMAND) != 0, SDL_SCANCODE_LGUI);
    OS3_SyncKeyModifier((q & IEQUALIFIER_RCOMMAND) != 0, SDL_SCANCODE_RGUI);

    /* Caps Lock is a toggle state, not a held key - sync the mod
       bit directly rather than synthesizing press/release events */
    if (((q & IEQUALIFIER_CAPSLOCK) != 0) != ((SDL_GetModState() & KMOD_CAPS) != 0)) {
        SDL_ToggleModState(KMOD_CAPS, (q & IEQUALIFIER_CAPSLOCK) != 0);
    }
}

static void OS3_DetectNumLock(const SDL_Scancode s)
{
    static SDL_bool oldState = SDL_FALSE;
    SDL_bool currentState = SDL_FALSE;

    // This function tries to determine whether NumLock is enabled or not,
    // based on the reported scancodes
    switch (s) {
        case SDL_SCANCODE_KP_0:
        case SDL_SCANCODE_KP_1:
        case SDL_SCANCODE_KP_2:
        case SDL_SCANCODE_KP_3:
        case SDL_SCANCODE_KP_4:
        case SDL_SCANCODE_KP_6:
        case SDL_SCANCODE_KP_7:
        case SDL_SCANCODE_KP_8:
        case SDL_SCANCODE_KP_9:
        case SDL_SCANCODE_KP_COMMA:
            currentState = SDL_TRUE;
#ifdef DEBUG_EVENTS_VERBOSE
            SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Numlock on\n");
#endif
            break;

        case SDL_SCANCODE_HOME:
        case SDL_SCANCODE_UP:
        case SDL_SCANCODE_PAGEUP:
        case SDL_SCANCODE_LEFT:
        case SDL_SCANCODE_RIGHT:
        case SDL_SCANCODE_END:
        case SDL_SCANCODE_PAGEDOWN:
        case SDL_SCANCODE_INSERT:
        case SDL_SCANCODE_DELETE:
        	currentState = SDL_FALSE;
#ifdef DEBUG_EVENTS_VERBOSE
            SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Numlock off\n");
#endif
            break;

        default:
            return;
    }

    if (currentState != oldState) {
        oldState = currentState;
#ifdef DEBUG_EVENTS_VERBOSE
        SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Toggling numlock state\n");
#endif
        SDL_SendKeyboardKey(SDL_PRESSED, SDL_SCANCODE_NUMLOCKCLEAR);
    }
}


static int OS3_GetButtonState(UWORD code)
{
    return (code & IECODE_UP_PREFIX) ? SDL_RELEASED : SDL_PRESSED;
}

static int OS3_GetButton(UWORD code)
{
    switch (code & ~IECODE_UP_PREFIX) {
        case IECODE_LBUTTON:
            return SDL_BUTTON_LEFT;
        case IECODE_RBUTTON:
            return SDL_BUTTON_RIGHT;
        case IECODE_MBUTTON:
            return SDL_BUTTON_MIDDLE;
        default:
            return 0;
    }
}

static struct InputEvent* IN_GetNextEvent(void)
{
	struct InputEvent *ie = NULL;

	if (imsglow != imsghigh) {
		ie = &imsgs[imsglow];
		imsglow++;
		imsglow %= MAXIMSGS;
	}

	return ie;
}

void OS3_PumpEvents(_THIS)
{
	SDL_Window* sdlwin;

	for (sdlwin = _this->windows; sdlwin; sdlwin = sdlwin->next) {
		SDL_WindowData *data = sdlwin->driverdata;

		if (data && data->syswin) {
			struct Window* syswin = data->syswin;
			struct MsgPort* msgPort = syswin->UserPort;
			struct IntuiMessage* imsg;
			struct InputEvent* event;

			while ((imsg = (struct IntuiMessage*)GetMsg(msgPort))) {
			    ULONG class = imsg->Class;
			    ReplyMsg((struct Message*)imsg);

			    switch (class) {
			        case IDCMP_CLOSEWINDOW:
			        	SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "OS3_PumpEvents() - IDCMP_CLOSEWINDOW\n");
			        	isWindowActive = SDL_FALSE;
			            SDL_SendWindowEvent(sdlwin, SDL_WINDOWEVENT_CLOSE, 0, 0);
			            break;

			        case IDCMP_ACTIVEWINDOW:
			        	SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "OS3_PumpEvents() - IDCMP_ACTIVEWINDOW\n");
			        	isWindowActive = SDL_TRUE;
			            SDL_SendWindowEvent(sdlwin, SDL_WINDOWEVENT_FOCUS_GAINED, 0, 0);
			            OS3_SyncKeyModifiers(_this);
			            break;

			        case IDCMP_INACTIVEWINDOW:
			        	SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "OS3_PumpEvents() - IDCMP_INACTIVEWINDOW\n");
			        	isWindowActive = SDL_FALSE;
			            SDL_SendWindowEvent(sdlwin, SDL_WINDOWEVENT_FOCUS_LOST, 0, 0);
			            break;

			        case IDCMP_NEWSIZE: {
			            int w, h;

			            w = syswin->Width  - syswin->BorderLeft - syswin->BorderRight;
			            h = syswin->Height - syswin->BorderTop  - syswin->BorderBottom;
			            SDL_SendWindowEvent(sdlwin, SDL_WINDOWEVENT_SIZE_CHANGED, w, h);

#ifdef SDL_VIDEO_OPENGL
			            if (data->glContext) {
			            	OS3_GL_UpdateContext(_this, sdlwin);
			            }
#endif
			            break;
			        }
			    }
			}

			/* Mouse movement from input handler */
			if (mouse_movement_x != 0 || mouse_movement_y != 0) {
				int mx = mouse_movement_x;
				int my = mouse_movement_y;

				// Reset.
				mouse_movement_x = mouse_movement_y = 0;

				if (SDL_GetRelativeMouseMode()) {
					SDL_SendMouseMotion(sdlwin, 0, 1, mx, my);
				} else {
					int wx = syswin->MouseX - syswin->BorderLeft;
					int wy = syswin->MouseY - syswin->BorderTop;
					int innerW = syswin->Width  - syswin->BorderLeft - syswin->BorderRight;
					int innerH = syswin->Height - syswin->BorderTop  - syswin->BorderBottom;

					/* The pointer is effectively confined to the window when:
						 - fullscreen: the backdrop window covers the whole screen, OR
						 - the app has grabbed the mouse: desktop SDL physically pins
						   the pointer inside the window, so we emulate the same
						   contract in coordinate space (Intuition cannot confine
						   the real pointer for us).*/
					SDL_bool confined = (data->customScreen != NULL) || isMouseInputCaptured;

					if (confined) {
						/* Clamp to the window's inner area. This keeps edge
						   coordinates deterministic regardless of mouse speed,
						   keeps the free axis sliding along a pinned edge, and
						   tolerates pointer sources (FS-UAE host integration,
						   certain RTG clamps) that report one-past-the-edge
						   values - byte-for-byte what a desktop app observes
						   under real pointer confinement.*/
						if (wx < 0)            wx = 0;
						else if (wx >= innerW) wx = innerW - 1;

						if (wy < 0)            wy = 0;
						else if (wy >= innerH) wy = innerH - 1;

						/* A confined pointer always holds mouse focus */
						if (SDL_GetMouseFocus() != sdlwin) {
							SDL_SetMouseFocus(sdlwin);
						}

						SDL_SendMouseMotion(sdlwin, 0, 0, wx, wy);
					} else {
						/* Windowed mode and ungrabbed: genuine enter/leave semantics */
						SDL_bool inside = (wx >= 0 && wy >= 0 && wx < innerW && wy < innerH);

						if (inside) {
							if (SDL_GetMouseFocus() != sdlwin) {
								SDL_SetMouseFocus(sdlwin);      /* sends WINDOWEVENT_ENTER, sets MOUSE_FOCUS */
							}
							SDL_SendMouseMotion(sdlwin, 0, 0, wx, wy);
						} else {
							if (SDL_GetMouseFocus() == sdlwin) {
								SDL_SetMouseFocus(NULL);        /* sends WINDOWEVENT_LEAVE, clears MOUSE_FOCUS */
							}
							/* don't send motion while outside - matches desktop SDL behaviour */
						}
					}
				}
			}

			/* Input handler queue — buttons and keyboard */
			while ((event = IN_GetNextEvent())) {
				if (event->ie_Class == IECLASS_RAWMOUSE) {
					if (event->ie_Code != IECODE_NOBUTTON) {
						int button = OS3_GetButton(event->ie_Code);
						int state  = OS3_GetButtonState(event->ie_Code);
						if (button) {
							globalMouseState.buttonPressed[button] = (state == SDL_PRESSED);
							SDL_SendMouseButton(sdlwin, 0, state, button);
						}
					}
				} else if (event->ie_Class == IECLASS_RAWKEY) {
					int code = event->ie_Code;
					int rawkey;

	                switch (code)
	                {
						case RAWKEY_NM_WHEEL_UP:
							 SDL_SendMouseWheel(sdlwin, 0, 0, 1, SDL_MOUSEWHEEL_NORMAL);
	                        break;

	    				case RAWKEY_NM_WHEEL_DOWN:
	    					SDL_SendMouseWheel(sdlwin, 0, 0, -1, SDL_MOUSEWHEEL_NORMAL);
	    				    break;

	    				default:
							rawkey = code & ~IECODE_UP_PREFIX; /* Strips 0x80 */
							if (rawkey < (int)SDL_arraysize(amiga_scancode_table)) {
								SDL_Scancode s = amiga_scancode_table[rawkey];

								if ((code & IECODE_UP_PREFIX) == 0) {
									/* Key press */
									OS3_DetectNumLock(s);
									SDL_SendKeyboardKey(SDL_PRESSED, s);

									const ULONG unicode = OS3_TranslateUnicode(
										_this,
										event->ie_Code,
										event->ie_Qualifier,
										event->ie_EventAddress);  /* from copied InputEvent */

									if (unicode) {
										char text[5] = { 0 };
										SDL_UCS4ToUTF8(unicode, text);
										SDL_SendKeyboardText(text);
									}
								} else {
									/* Key release */
									SDL_SendKeyboardKey(SDL_RELEASED, s);
								}
							}
	                }
				}
			}
		}
	}
}

void OS3_SetMouseGrab(SDL_bool grabbed) {
	SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "OS3_SetMouseGrab(%s)\n", grabbed ? "enabled" : "disabled");

	isMouseInputCaptured = grabbed;
}

void OS3_SetKeyboardGrab(SDL_bool grabbed) {
	SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "OS3_SetKeyboardGrab(%s)\n", grabbed ? "enabled" : "disabled");

	isKeyboardInputCaptured = grabbed;
}

void OS3_WarpSystemPointerAbsolute(int x, int y)
{
    struct InputEvent ie;

    if (inputRequest) {
		SDL_memset(&ie, 0, sizeof(ie));
		ie.ie_Class = IECLASS_POINTERPOS;
		ie.ie_Code  = IECODE_NOBUTTON;
		ie.ie_position.ie_xy.ie_x = x;
		ie.ie_position.ie_xy.ie_y = y;

		inputRequest->io_Command = IND_WRITEEVENT;
		inputRequest->io_Data    = (APTR)&ie;
		inputRequest->io_Length  = sizeof(ie);
		DoIO((struct IORequest *)inputRequest);

	    // Put io_Data back where IND_ADDHANDLER left it, otherwise IND_REMHANDLER
	    // will not find the handler at shutdown and we leave a dangling interrupt.
	    inputRequest->io_Data = (APTR)inputHandler;
    }
}

static void IN_AddEvent(struct InputEvent *event)
{
    int next = (imsghigh + 1) % MAXIMSGS;
    if (next != imsglow) {    /* not full */
        CopyMem(event, &imsgs[imsghigh], sizeof(struct InputEvent));
        imsghigh = next;
    }
}

HANDLERPROTO(IN_InputHandler, struct InputEvent *, struct InputEvent *ielist, APTR id)
{
	struct InputEvent *event;
	int code;

    if (isWindowActive)
    {
        for (event = ielist; event; event = event->ie_NextEvent)
    	{
            // We only care about mouse and keyboard events!
            if (event->ie_Class == IECLASS_RAWMOUSE) {
                code = event->ie_Code;

                // mouse buttons 1-3
    			if (code != IECODE_NOBUTTON)
    			{
        			// Store the event.
                    IN_AddEvent(event);

                    if (isMouseInputCaptured) {
						// Eat the event without breaking the linked list
						event->ie_Class = IECLASS_NULL;
						event->ie_Code = 0;
						event->ie_Qualifier = 0;
					}
    			}

                // Always store the relative mouse movement.
                mouse_movement_x += event->ie_position.ie_xy.ie_x;
                mouse_movement_y += event->ie_position.ie_xy.ie_y;
            }

            if (event->ie_Class == IECLASS_RAWKEY) {
                // Store the event.
                IN_AddEvent(event);

               if (isKeyboardInputCaptured) {
					// Eat the event without breaking the linked list
					event->ie_Class = IECLASS_NULL;
					event->ie_Code = 0;
					event->ie_Qualifier = 0;
				}
            }
        }
    }

    return ielist;
}

static int IN_InitInputHandler(void)
{
	SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "[IN_SetupInputHandler]: Setting up the input handler\n");

    // Reset.
    imsglow = imsghigh = 0;
    mouse_movement_x = mouse_movement_y = 0;

	inputHandler = AllocVec(sizeof(struct Interrupt), MEMF_PUBLIC|MEMF_CLEAR);
    if (!inputHandler) {
    	return SDL_SetError("Could not allocated memory for the input handler");
    }

    inputPort = CreateMsgPort();
   	if (!inputPort) {
   		return SDL_SetError("Could not create input handler message port");
    }

	inputRequest = CreateStdIO(inputPort);
	if (!inputRequest) {
		return SDL_SetError("Could not create input request");
    }

	if (OpenDevice((CONST_STRPTR)"input.device", 0, (struct IORequest*)inputRequest, 0) != 0) {
		return SDL_SetError("Could not open input.device");
	}

	/* input.device exports PeekQualifier() as a direct-call function;
	   the inline stub needs the device base in InputBase (same pattern
	   as TimerBase for timer.device's ReadEClock) */
	InputBase = inputRequest->io_Device;

    inputHandler->is_Node.ln_Type = NT_INTERRUPT;
	inputHandler->is_Node.ln_Pri = 100;
	inputHandler->is_Node.ln_Name = inputHandlerName;
	inputHandler->is_Code = (APTR)IN_InputHandler;
	inputRequest->io_Data = (APTR)inputHandler;

	inputRequest->io_Command = IND_ADDHANDLER;
	DoIO((struct IORequest*)inputRequest);

	SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "[IN_SetupInputHandler]: Finished setting up the input handler\n");

    // Success.
    return 0;
}

static void IN_ShutdownInputHandler(void)
{
	SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "[IN_ShutdownInputHandler]: Shutting down the input handler\n");

	if (inputRequest) {
		// IND_REMHANDLER finds the handler to remove through io_Data, so make
		// sure it is still pointing at our Interrupt (a warp borrows it).
		inputRequest->io_Data = (APTR)inputHandler;
		inputRequest->io_Command = IND_REMHANDLER;
		DoIO((struct IORequest *)inputRequest);

		CloseDevice((struct IORequest*)inputRequest);
		DeleteStdIO(inputRequest);
		inputRequest = NULL;
		InputBase = NULL;
	}

    if (inputPort) {
        DeleteMsgPort(inputPort);
        inputPort = NULL;
    }

    if (inputHandler) {
        FreeVec((APTR)inputHandler);
        inputHandler = NULL;
    }
}

int OS3_InitEvents(_THIS)
{
    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "[OS3_InitEvents]: Called\n");

    isWindowActive = SDL_FALSE;
    isMouseInputCaptured = SDL_FALSE;
    isKeyboardInputCaptured = SDL_FALSE;

    return IN_InitInputHandler();
}

void OS3_QuitEvents(_THIS)
{
	SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "[OS3_QuitEvents]: Called\n");

	IN_ShutdownInputHandler();
}

#endif /* SDL_VIDEO_DRIVER_AMIGAOS3 */


