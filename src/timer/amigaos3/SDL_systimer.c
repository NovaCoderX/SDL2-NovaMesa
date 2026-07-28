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

#if SDL_TIMER_AMIGAOS3

#include <proto/exec.h>
#include <proto/timer.h>
#include <devices/timer.h>

#include "SDL_timer.h"
#include "SDL_os3timer_c.h"

/* The proto/timer.h inline stubs require this exact global symbol */
struct Device *TimerBase = NULL;

/* Shared timer requests used by SDL_syssem.c for semaphore timeouts */
struct timerequest *timerReq[2] = { NULL, NULL };

/* EClock start value and frequency for SDL_GetTicks64 */
static struct EClockVal startClock;
static ULONG eclockFreq = 1;

static SDL_bool started = SDL_FALSE;

static struct MsgPort *timerPort = NULL;

static SDL_bool OS3_OpenTimerReq(int index, ULONG unit)
{
    timerReq[index] = (struct timerequest *)
        CreateIORequest(timerPort, sizeof(struct timerequest));
    if (!timerReq[index]) return SDL_FALSE;

    if (OpenDevice((CONST_STRPTR)TIMERNAME, unit, (struct IORequest *)timerReq[index], 0) != 0) {
        DeleteIORequest((struct IORequest *)timerReq[index]);
        timerReq[index] = NULL;
        return SDL_FALSE;
    }

    return SDL_TRUE;
}

void SDL_TicksInit(void)
{
    if (started) return;

    timerPort = CreateMsgPort();
    if (!timerPort) {
        SDL_SetError("Could not create timer message port");
        return;
    }

    if (!OS3_OpenTimerReq(0, UNIT_VBLANK) ||
        !OS3_OpenTimerReq(1, UNIT_MICROHZ)) {
        SDL_SetError("Could not open timer.device");
        return;
    }

    // Assign the global TimerBase so proto/timer.h macros function correctly
    TimerBase = (struct Device *)timerReq[0]->tr_node.io_Device;

    /* Capture start time using EClock for high-resolution GetTicks64 */
    eclockFreq = ReadEClock(&startClock);
    if (!eclockFreq) eclockFreq = 1;

    started = SDL_TRUE;
}

void SDL_TicksQuit(void)
{
    int i;

    for (i = 1; i >= 0; i--) {
        if (timerReq[i]) {
            CloseDevice((struct IORequest *)timerReq[i]);
            DeleteIORequest((struct IORequest *)timerReq[i]);
            timerReq[i] = NULL;
        }
    }

    if (timerPort) {
        DeleteMsgPort(timerPort);
        timerPort = NULL;
    }

    TimerBase = NULL;
    started = SDL_FALSE;
}

static Uint64 EClockToU64(const struct EClockVal *ev)
{
    return ((Uint64)ev->ev_hi << 32) | (Uint64)ev->ev_lo;
}

Uint64 SDL_GetTicks64(void)
{
    struct EClockVal now;
    Uint64 delta;

    if (!started) SDL_TicksInit();

    ReadEClock(&now);
    delta = EClockToU64(&now) - EClockToU64(&startClock);

    return (delta * 1000ULL) / (Uint64)eclockFreq;
}

Uint64 SDL_GetPerformanceCounter(void)
{
    struct EClockVal now;

    ReadEClock(&now);

    return EClockToU64(&now);
}

Uint64 SDL_GetPerformanceFrequency(void)
{
    return (Uint64)eclockFreq;
}

void SDL_Delay(Uint32 ms)
{
    struct MsgPort     *port;
    struct timerequest *req;

    if (ms == 0) return;

    port = CreateMsgPort();
    if (!port) return;

    req = (struct timerequest *)CreateIORequest(port, sizeof(*req));
    if (req) {
        if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_MICROHZ, (struct IORequest *)req, 0) == 0) {
            req->tr_node.io_Command  = TR_ADDREQUEST;
            req->tr_time.tv_secs     = ms / 1000;
            req->tr_time.tv_micro    = (ms % 1000) * 1000;
            DoIO((struct IORequest *)req);
            CloseDevice((struct IORequest *)req);
        }
        DeleteIORequest((struct IORequest *)req);
    }
    DeleteMsgPort(port);
}

#endif /* SDL_TIMER_AMIGAOS3 */


