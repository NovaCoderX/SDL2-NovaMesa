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

#if SDL_AUDIO_DRIVER_AMIGAOS3

#include "SDL_audio.h"
#include "SDL_timer.h"
#include "../SDL_audio_c.h"
#include "../SDL_sysaudio.h"
#include "SDL_os3audio.h"

#include <proto/exec.h>
#include <proto/ahi.h>
#include <clib/alib_protos.h>

/* The tag name used by the AmigaOS3 audio driver */
#define DRIVER_NAME "amigaos3"

static SDL_bool OS3_OpenAhiDevice(OS3AudioData * os3data)
{
    if (os3data->deviceOpen) {
        return SDL_TRUE;
    }

    os3data->deviceOpen = SDL_FALSE;
    os3data->ahiReplyPort = CreateMsgPort();

    if (os3data->ahiReplyPort) {
        /* create an iorequest for the device */
        os3data->ahiRequest[0] = (struct AHIRequest *)CreateIORequest(os3data->ahiReplyPort, sizeof(struct AHIRequest));

        if (os3data->ahiRequest[0]) {
            os3data->ahiRequest[0]->ahir_Version = 4;

            if (OpenDevice((CONST_STRPTR)AHINAME, AHI_DEFAULT_UNIT, (struct IORequest *)os3data->ahiRequest[0], 0) == 0) {
                /* Create a duplicate request using public memory for double buffering */
                os3data->ahiRequest[1] = (struct AHIRequest *)AllocVec(sizeof(struct AHIRequest), MEMF_PUBLIC);

                if (os3data->ahiRequest[1]) {
                    CopyMem(os3data->ahiRequest[0], os3data->ahiRequest[1], sizeof(struct AHIRequest));
                    os3data->deviceOpen = SDL_TRUE;
                    os3data->currentBuffer = 0;
                    os3data->link = NULL;
                    os3data->requestSent[0] = SDL_FALSE;
                    os3data->requestSent[1] = SDL_FALSE;
                } else {
                    CloseDevice((struct IORequest *)os3data->ahiRequest[0]);
                }
            }

            if (!os3data->deviceOpen) {
                DeleteIORequest((struct IORequest *)os3data->ahiRequest[0]);
                os3data->ahiRequest[0] = NULL;
            }
        }

        if (!os3data->deviceOpen) {
            DeleteMsgPort(os3data->ahiReplyPort);
            os3data->ahiReplyPort = NULL;
        }
    }

    return os3data->deviceOpen;
}

static void OS3_CloseAhiDevice(OS3AudioData * os3data)
{
    if (os3data->ahiRequest[0]) {
        int i;

        /* Abort and reap BOTH potentially in-flight requests.
           With double buffering, both slots can be pending at
           shutdown; reaping only the newest (link) can leave a
           reply message on the port, and deleting a MsgPort with
           pending messages is undefined behaviour on AmigaOS. */
        for (i = 0; i < 2; i++) {
            if (os3data->ahiRequest[i] && os3data->requestSent[i]) {
                AbortIO((struct IORequest *)os3data->ahiRequest[i]);
                WaitIO((struct IORequest *)os3data->ahiRequest[i]);
                os3data->requestSent[i] = SDL_FALSE;
            }
        }

        os3data->link = NULL;

        /* Drain any stale replies before deleting the port */
        while (GetMsg(os3data->ahiReplyPort) != NULL) {
            /* just drain */
        }

        CloseDevice((struct IORequest *)os3data->ahiRequest[0]);
        DeleteIORequest((struct IORequest *)os3data->ahiRequest[0]);
        os3data->ahiRequest[0] = NULL;

        if (os3data->ahiRequest[1]) {
            FreeVec(os3data->ahiRequest[1]);
            os3data->ahiRequest[1] = NULL;
        }
    }

    if (os3data->ahiReplyPort) {
        DeleteMsgPort(os3data->ahiReplyPort);
        os3data->ahiReplyPort = NULL;
    }

    os3data->deviceOpen = SDL_FALSE;
}

static SDL_bool OS3_AudioAvailable(void)
{
    SDL_bool isAvailable = SDL_FALSE;
    OS3AudioData *tempData = SDL_calloc(1, sizeof(OS3AudioData));

    if (tempData) {
        isAvailable = OS3_OpenAhiDevice(tempData);
        if (isAvailable) {
            OS3_CloseAhiDevice(tempData);
        }
        SDL_free(tempData);
    }

    return isAvailable;
}

static int OS3_SwapBuffer(int current)
{
    return (1 - current);
}

/* ---------------------------------------------- */
/* Audio driver exported functions implementation */
/* ---------------------------------------------- */
static void OS3_CloseDevice(_THIS)
{
    OS3AudioData *os3data = _this->hidden;

    if (os3data) {
        OS3_CloseAhiDevice(os3data);

        int i;
        for (i = 0; i < 2; i++) {
            if (os3data->audioBuffer[i]) {
                FreeVec(os3data->audioBuffer[i]);
                os3data->audioBuffer[i] = NULL;
            }
        }

        SDL_free(os3data);
        _this->hidden = NULL;
    }
}

static int OS3_OpenDevice(_THIS, const char * devname)
{
    OS3AudioData *os3data = NULL;

    _this->hidden = (OS3AudioData *) SDL_malloc(sizeof(OS3AudioData));

    if (!_this->hidden) {
        return SDL_OutOfMemory();
    }

    SDL_memset(_this->hidden, 0, sizeof(OS3AudioData));
    os3data = _this->hidden;

    /* AHI limits on classic hardware: force 2 channels if requested more */
    if (_this->spec.channels > 2) {
        _this->spec.channels = 2;
    }

    if (_this->spec.channels < 1) {
        SDL_SetError("AHI channel number not supported");
        OS3_CloseDevice(_this);
        return -1;
    }

    switch (_this->spec.format & 0xFF) {
        case 8:
            _this->spec.format = AUDIO_S8;
            os3data->ahiType = (_this->spec.channels == 1) ? AHIST_M8S : AHIST_S8S;
            break;
        case 16:
            _this->spec.format = AUDIO_S16MSB;
            os3data->ahiType = (_this->spec.channels == 1) ? AHIST_M16S : AHIST_S16S;
            break;
        default:
            /* Fallback to 16-bit for anything else */
            _this->spec.format = AUDIO_S16MSB;
            os3data->ahiType = (_this->spec.channels == 1) ? AHIST_M16S : AHIST_S16S;
            break;
    }

    /* Calculate the final parameters for this audio specification */
    SDL_CalculateAudioSpec(&_this->spec);

    os3data->audioBufferSize = _this->spec.size;

    /* Strict AmigaOS requirement: Sound buffers must be in public memory */
    os3data->audioBuffer[0] = (Uint8 *) AllocVec(_this->spec.size, MEMF_PUBLIC | MEMF_CLEAR);
    os3data->audioBuffer[1] = (Uint8 *) AllocVec(_this->spec.size, MEMF_PUBLIC | MEMF_CLEAR);

    if (os3data->audioBuffer[0] == NULL || os3data->audioBuffer[1] == NULL) {
        SDL_SetError("No memory for AHI audio buffer");
        OS3_CloseDevice(_this);
        return -1;
    }

    return 0;
}

static void OS3_ThreadInit(_THIS)
{
    OS3AudioData *os3data = _this->hidden;

    if (!OS3_OpenAhiDevice(os3data)) {
        SDL_SetError("Failed to open AHI within audio thread");
    }

    /* Bump task priority to ensure smooth audio mixing */
    SetTaskPri(FindTask(NULL), 5);
}

static void OS3_WaitDevice(_THIS)
{
    /* Dummy - OS3_PlayDevice handles the waiting using WaitIO */
}

static void OS3_PlayDevice(_THIS)
{
    struct AHIRequest  *ahiRequest;
    SDL_AudioSpec      *spec    = &_this->spec;
    OS3AudioData       *os3data = _this->hidden;
    int                 current = os3data->currentBuffer;
    int                 previous = OS3_SwapBuffer(current);

    if (!os3data->deviceOpen) {
        return;
    }

    ahiRequest = os3data->ahiRequest[current];

    ahiRequest->ahir_Std.io_Message.mn_Node.ln_Pri = 0;
    ahiRequest->ahir_Std.io_Data    = os3data->audioBuffer[current];
    ahiRequest->ahir_Std.io_Length  = os3data->audioBufferSize;
    ahiRequest->ahir_Std.io_Offset  = 0;
    ahiRequest->ahir_Std.io_Command = CMD_WRITE;
    ahiRequest->ahir_Volume         = 0x10000;
    ahiRequest->ahir_Position       = 0x8000;
    ahiRequest->ahir_Link           = os3data->link;
    ahiRequest->ahir_Frequency      = spec->freq;
    ahiRequest->ahir_Type           = os3data->ahiType;

    SendIO((struct IORequest *)ahiRequest);
    os3data->requestSent[current] = SDL_TRUE;

    if (os3data->link) {
        WaitIO((struct IORequest *)os3data->link);
        os3data->requestSent[previous] = SDL_FALSE;
    }

    os3data->link = ahiRequest;
    os3data->currentBuffer = OS3_SwapBuffer(current);
}

static Uint8 * OS3_GetDeviceBuf(_THIS)
{
    return _this->hidden->audioBuffer[_this->hidden->currentBuffer];
}

/* ------------------------------------------ */
/* Audio driver init functions implementation */
/* ------------------------------------------ */
static SDL_bool OS3_Init(SDL_AudioDriverImpl * impl)
{
    if (!OS3_AudioAvailable()) {
        SDL_SetError("Failed to open AHI device");
        return SDL_FALSE;
    }

    impl->OpenDevice = OS3_OpenDevice;
    impl->ThreadInit = OS3_ThreadInit;
    impl->WaitDevice = OS3_WaitDevice;
    impl->PlayDevice = OS3_PlayDevice;
    impl->GetDeviceBuf = OS3_GetDeviceBuf;
    impl->CloseDevice = OS3_CloseDevice;

    impl->HasCaptureSupport = SDL_FALSE;
    impl->OnlyHasDefaultOutputDevice = SDL_TRUE;
    impl->OnlyHasDefaultCaptureDevice = SDL_FALSE;

    return SDL_TRUE;
}

AudioBootStrap AMIGAOS3AUDIO_bootstrap = {
   DRIVER_NAME, "AmigaOS3 AHI audio", OS3_Init, SDL_FALSE
};

#endif /* SDL_AUDIO_DRIVER_AMIGAOS3 */
