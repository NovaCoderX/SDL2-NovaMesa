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

#include <exec/semaphores.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <proto/exec.h>

#include "SDL_thread.h"
#include "../../timer/amigaos3/SDL_os3timer_c.h"  /* for timerReq[2] */

/* SDL 2 renamed mutexP/mutexV */
#define SDL_LockMutex(m)   SDL_LockMutex(m)
#define SDL_UnlockMutex(m) SDL_UnlockMutex(m)

/*
   List macros, strict-aliasing clean: each object is only ever accessed
   through its true type. NEWLIST operates on genuine struct List objects
   (the MsgPort message list); the MIN* macros operate on struct MinList /
   struct MinNode objects (the semaphore wait list). The only casts are
   pointer-value conversions for the head/tail sentinels, which are not
   memory accesses and therefore aliasing-irrelevant.
*/

/*
   NOTE on -Wdangling-pointer: GCC warns that stack-allocated waitnodes
   are linked into sem->waitlist, which outlives the statement. This is
   intentional and safe: the wait-list protocol guarantees every node is
   MINREMOVE'd (under the SignalSemaphore) before its owning function
   returns, so no stack address ever outlives its frame.
*/

#define NEWLIST(l) \
do { struct List *_l = (l); \
     _l->lh_TailPred = (struct Node *)_l; \
     _l->lh_Tail = NULL; \
     _l->lh_Head = (struct Node *)&_l->lh_Tail; } while (0)

#define MINNEWLIST(l) \
do { struct MinList *_l = (l); \
     _l->mlh_TailPred = (struct MinNode *)_l; \
     _l->mlh_Tail = NULL; \
     _l->mlh_Head = (struct MinNode *)&_l->mlh_Tail; } while (0)

#define MINADDTAIL(l,n) \
do { struct MinNode *_n = (n); struct MinList *_l = (l); \
     _n->mln_Succ = (struct MinNode *)&_l->mlh_Tail; \
     _n->mln_Pred = _l->mlh_TailPred; \
     _l->mlh_TailPred->mln_Succ = _n; \
     _l->mlh_TailPred = _n; } while (0)

#define MINREMOVE(n) \
do { struct MinNode *_n = (n); \
     _n->mln_Pred->mln_Succ = _n->mln_Succ; \
     _n->mln_Succ->mln_Pred = _n->mln_Pred; } while (0)

/* MinList equivalent of IsListEmpty - no type-punned dereference */
#define IsMinListEmpty(l) ((l)->mlh_TailPred == (struct MinNode *)(l))

#define FALLBACKSIGNAL SIGBREAKB_CTRL_D   /* avoid CTRL_E (thread start) and CTRL_F (timer) */


struct waitnode {
    struct MinNode node;
    struct Task   *task;
    ULONG          sigmask;
};

struct SDL_semaphore {
    struct SignalSemaphore sem;
    struct MinList         waitlist;
    int                    sem_value;
};

struct mywaitdata {
    struct MsgPort      port;
    struct timerequest  timereq;
    ULONG               extramask;
    BOOL                pending;
};

static void mywaitdone(struct mywaitdata *data)
{
    if (data->pending) {
        data->pending = FALSE;
        AbortIO((struct IORequest *)&data->timereq);
        WaitIO((struct IORequest *)&data->timereq);
    }
    if ((BYTE)data->port.mp_SigBit != -1) {
        FreeSignal(data->port.mp_SigBit);
        data->port.mp_SigBit = (UBYTE)-1;
    }
}

static int mywaitinit(struct mywaitdata *data, Uint32 timeout)
{
    data->extramask = 0;
    data->pending   = FALSE;

    if ((BYTE)(data->port.mp_SigBit = AllocSignal(-1)) != -1) {
        /* Borrow device/unit from pre-opened global requests
           0 = UNIT_VBLANK (coarser, for long timeouts)
           1 = UNIT_MICROHZ (finer, for short timeouts) */
        struct timerequest *req = timerReq[timeout < 100 ? 1 : 0];
        if (!req) {
            /* Timer subsystem not up (or already torn down) - degrade
               gracefully instead of dereferencing NULL */
            mywaitdone(data);
            return -1;
        }

        data->port.mp_Node.ln_Type = NT_MSGPORT;
        data->port.mp_Flags        = PA_SIGNAL;
        data->port.mp_SigTask      = FindTask(NULL);
        NEWLIST(&data->port.mp_MsgList);   /* mp_MsgList is a struct List */

        data->timereq.tr_node.io_Message.mn_Node.ln_Type = NT_REPLYMSG;
        data->timereq.tr_node.io_Message.mn_ReplyPort    = &data->port;
        data->timereq.tr_node.io_Message.mn_Length       = sizeof(data->timereq);
        data->timereq.tr_node.io_Device                  = req->tr_node.io_Device;
        data->timereq.tr_node.io_Unit                    = req->tr_node.io_Unit;
        return 0;
    }

    mywaitdone(data);
    return -1;
}

static int mywait(struct mywaitdata *data, Uint32 timeout)
{
    ULONG wsig = 1 << data->port.mp_SigBit;
    ULONG sigs;

    if (!data->pending) {
        data->pending = TRUE;
        data->timereq.tr_node.io_Command   = TR_ADDREQUEST;
        data->timereq.tr_time.tv_secs      = timeout / 1000;
        data->timereq.tr_time.tv_micro     = (timeout % 1000) * 1000;
        SendIO((struct IORequest *)&data->timereq);
    }

    sigs = Wait(wsig | data->extramask | SIGBREAKF_CTRL_C);

    if (sigs & wsig) {
        data->pending = FALSE;
        WaitIO((struct IORequest *)&data->timereq);
    } else {
        if (data->pending && data->extramask == 0) {
            data->pending = FALSE;
            AbortIO((struct IORequest *)&data->timereq);
            WaitIO((struct IORequest *)&data->timereq);
        }
    }

    data->extramask &= sigs;

    return (sigs & SIGBREAKF_CTRL_C) ? -1 : 0;
}

SDL_sem *SDL_CreateSemaphore(Uint32 initial_value)
{
    SDL_sem *sem = (SDL_sem *)SDL_malloc(sizeof(*sem));
    if (sem) {
        SDL_memset(&sem->sem, 0, sizeof(sem->sem));
        InitSemaphore(&sem->sem);
        MINNEWLIST(&sem->waitlist);
        sem->sem_value = (int)initial_value;
    } else {
        SDL_OutOfMemory();
    }
    return sem;
}

void SDL_DestroySemaphore(SDL_sem *sem)
{
    if (sem) {
        struct mywaitdata data;

        if (mywaitinit(&data, 10) == 0) {
            ObtainSemaphore(&sem->sem);
            sem->sem_value = -1;

            while (!IsMinListEmpty(&sem->waitlist)) {
                struct waitnode *wn;
                int res;

                for (wn = (struct waitnode *)sem->waitlist.mlh_Head;
                     wn->node.mln_Succ;
                     wn = (struct waitnode *)wn->node.mln_Succ) {
                    Signal(wn->task, wn->sigmask);
                }

                ReleaseSemaphore(&sem->sem);
                res = mywait(&data, 10);
                ObtainSemaphore(&sem->sem);

                if (res < 0) break;
            }

            ReleaseSemaphore(&sem->sem);
        }

        mywaitdone(&data);
        SDL_free(sem);
    }
}

int SDL_SemTryWait(SDL_sem *sem)
{
    int retval;

    if (!sem) return SDL_SetError("Passed a NULL semaphore");

    ObtainSemaphore(&sem->sem);
    retval = (sem->sem_value > 0) ? (--sem->sem_value, 0) : SDL_MUTEX_TIMEDOUT;
    ReleaseSemaphore(&sem->sem);

    return retval;
}

int SDL_SemWait(SDL_sem *sem)
{
    int retval = 0;
    struct waitnode wn;
    LONG signal = -1;

    if (!sem) return SDL_SetError("Passed a NULL semaphore");

    ObtainSemaphore(&sem->sem);

    while (sem->sem_value <= 0) {
        if (signal == -1) {
            wn.task   = FindTask(NULL);
            signal    = AllocSignal(-1);
            if (signal == -1) { signal = FALLBACKSIGNAL; SetSignal(1 << FALLBACKSIGNAL, 0); }
            wn.sigmask = 1 << signal;
            MINADDTAIL(&sem->waitlist, &wn.node);
        }

        ReleaseSemaphore(&sem->sem);
        if (Wait(wn.sigmask | SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) { retval = -1; ObtainSemaphore(&sem->sem); break; }
        ObtainSemaphore(&sem->sem);
    }

    if (signal != -1) {
        MINREMOVE(&wn.node);
        if (signal != FALLBACKSIGNAL) FreeSignal(signal);
    }

    if (retval == 0) --sem->sem_value;

    ReleaseSemaphore(&sem->sem);
    return retval;
}

int SDL_SemWaitTimeout(SDL_sem *sem, Uint32 timeout)
{
    if (!sem) return SDL_SetError("Passed a NULL semaphore");
    if (timeout == 0) return SDL_SemTryWait(sem);
    if (timeout == SDL_MUTEX_MAXWAIT) return SDL_SemWait(sem);

    struct mywaitdata data;
    int retval = -1;

    if (mywaitinit(&data, timeout) == 0) {
        struct waitnode wn;
        LONG signal = -1;
        retval = 0;

        ObtainSemaphore(&sem->sem);

        while (sem->sem_value <= 0) {
            if (signal == -1) {
                wn.task   = FindTask(NULL);
                signal    = AllocSignal(-1);
                if (signal == -1) { signal = FALLBACKSIGNAL; SetSignal(1 << FALLBACKSIGNAL, 0); }
                wn.sigmask = 1 << signal;
                MINADDTAIL(&sem->waitlist, &wn.node);
            }

            ReleaseSemaphore(&sem->sem);
            data.extramask = wn.sigmask;
            retval = mywait(&data, timeout);
            ObtainSemaphore(&sem->sem);

            if (retval < 0) break;
            if (data.extramask == 0) { retval = SDL_MUTEX_TIMEDOUT; break; }
        }

        if (signal != -1) {
            MINREMOVE(&wn.node);
            if (signal != FALLBACKSIGNAL) FreeSignal(signal);
        }

        if (retval == 0) --sem->sem_value;

        ReleaseSemaphore(&sem->sem);
    }

    mywaitdone(&data);
    return retval;
}

Uint32 SDL_SemValue(SDL_sem *sem)
{
    Uint32 retval = 0;
    if (sem) {
        ObtainSemaphoreShared(&sem->sem);
        retval = sem->sem_value > 0 ? (Uint32)sem->sem_value : 0;
        ReleaseSemaphore(&sem->sem);
    }
    return retval;
}

int SDL_SemPost(SDL_sem *sem)
{
    struct waitnode *wn;

    if (!sem) return SDL_SetError("Passed a NULL semaphore");

    ObtainSemaphore(&sem->sem);

    wn = (struct waitnode *)sem->waitlist.mlh_Head;
    if (wn->node.mln_Succ) Signal(wn->task, wn->sigmask);

    ++sem->sem_value;
    ReleaseSemaphore(&sem->sem);
    return 0;
}

#endif /* SDL_THREAD_AMIGAOS3 */
