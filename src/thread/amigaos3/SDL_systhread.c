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

#if SDL_THREAD_AMIGAOS3

#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dostags.h>

#include "SDL_thread.h"
#include "../SDL_thread_c.h"
#include "../SDL_systhread.h"
#include "SDL_systhread_c.h"


/* Per-thread data kept alive until WaitThread/DetachThread */
typedef struct
{
    SDL_Thread  *thread;
    struct Task *parentTask;
    BYTE         doneSigBit;   /* unique per thread — avoids signal collisions */
} OS3_ThreadHandle;

static ULONG RunThread(char *args __asm("a0"))
{
    /* Recover our handle from the argument string */
    OS3_ThreadHandle *hdl = (OS3_ThreadHandle *)atol(args);

    /* Take local copies before signalling parent */
    struct Task  *parent  = hdl->parentTask;
    BYTE          sigBit  = hdl->doneSigBit;
    SDL_Thread   *thread  = hdl->thread;

    /* Store our own task pointer so SDL_ThreadID() works */
    thread->handle = FindTask(NULL);

    /* Tell parent we have started and no longer need hdl */
    Signal(parent, SIGBREAKF_CTRL_E);

    /* Run the SDL thread — calls SetupThread, user function, cleanup */
    SDL_RunThread(thread);

    /* Tell parent we are done */
    Signal(parent, 1L << sigBit);

    return 0;
}

static int threadId = 0;

int SDL_SYS_CreateThread(SDL_Thread *thread)
{
    char nameBuffer[64];
    char argBuffer[24];

    OS3_ThreadHandle *hdl = AllocVec(sizeof(*hdl), MEMF_ANY | MEMF_CLEAR);
    if (!hdl) {
        return SDL_SetError("Not enough memory to create thread");
    }

    hdl->thread     = thread;
    hdl->parentTask = FindTask(NULL);

    /* Allocate a unique done-signal bit for this thread */
    hdl->doneSigBit = AllocSignal(-1);
    if (hdl->doneSigBit == (BYTE)-1) {
        FreeVec(hdl);
        return SDL_SetError("No signal bits available for thread");
    }

    /* Keep hdl alive in thread->data until WaitThread/DetachThread */
    thread->data = hdl;

    /* Clear both start and done signals before spawning */
    SetSignal(0L, SIGBREAKF_CTRL_E | (1L << hdl->doneSigBit));

    SDL_snprintf(argBuffer, sizeof(argBuffer), "%ld", (LONG)hdl);

    threadId++;
    if (thread->name) {
        SDL_snprintf(nameBuffer, sizeof(nameBuffer), "SDL %s", thread->name);
    } else {
        SDL_snprintf(nameBuffer, sizeof(nameBuffer), "SDL Thread %d", threadId);
    }

    thread->handle = (struct Task *)CreateNewProcTags(
        NP_Output,      Output(),
        NP_Input,       Input(),
        NP_Name,        (ULONG)nameBuffer,
        NP_CloseOutput, FALSE,
        NP_CloseInput,  FALSE,
        NP_StackSize,   thread->stacksize ? (ULONG)thread->stacksize : 262144UL,
        NP_Entry,       (ULONG)RunThread,
        NP_Arguments,   (ULONG)argBuffer,
        TAG_DONE);

    if (!thread->handle) {
        FreeSignal(hdl->doneSigBit);
        FreeVec(hdl);
        thread->data = NULL;
        return SDL_SetError("CreateNewProcTags failed");
    }

    /* Wait until child has extracted what it needs from hdl */
    Wait(SIGBREAKF_CTRL_E);

    /* hdl is still valid (kept in thread->data) — child no longer touches it */

    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Created thread '%s' (task %p)\n", nameBuffer, thread->handle);

    return 0;
}

void SDL_SYS_SetupThread(const char *name)
{
    /* Called from SDL_RunThread — nothing extra needed on OS3 */
}

SDL_threadID SDL_ThreadID(void)
{
    return (SDL_threadID)FindTask(NULL);
}

int SDL_SYS_SetThreadPriority(SDL_ThreadPriority priority)
{
    int value;

    switch (priority) {
        case SDL_THREAD_PRIORITY_LOW:           value = -5;  break;
        case SDL_THREAD_PRIORITY_HIGH:          value =  5;  break;
        case SDL_THREAD_PRIORITY_TIME_CRITICAL: value =  10; break;
        default:                                value =  0;  break;
    }

    SetTaskPri(FindTask(NULL), (BYTE)value);
    return 0;
}

void SDL_SYS_WaitThread(SDL_Thread *thread)
{
    OS3_ThreadHandle *hdl = (OS3_ThreadHandle *)thread->data;

    if (!hdl) {
        SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "WaitThread: no handle for '%s'\n", thread->name);
        return;
    }

    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Waiting for thread '%s' (signal bit %d)\n",
            thread->name, (int)hdl->doneSigBit);

    /* If child already finished, this returns immediately (signal already latched) */
    Wait(1L << hdl->doneSigBit);

    FreeSignal(hdl->doneSigBit);
    FreeVec(hdl);
    thread->data = NULL;

    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Thread '%s' done\n", thread->name);
}

void SDL_SYS_DetachThread(SDL_Thread *thread)
{
    /* For detached threads we won't call WaitThread, so clean up the handle now.
       Accept that the child's done-signal may fire into a freed/recycled bit —
       this is a known limitation of signal-based threading. */
    OS3_ThreadHandle *hdl = (OS3_ThreadHandle *)thread->data;

    if (hdl) {
        SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Detaching thread '%s'\n", thread->name);
        FreeSignal(hdl->doneSigBit);
        FreeVec(hdl);
        thread->data = NULL;
    }
}

#endif /* SDL_THREAD_AMIGAOS3 */
