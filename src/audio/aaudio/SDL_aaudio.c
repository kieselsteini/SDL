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
#include "SDL_internal.h"

#ifdef SDL_AUDIO_DRIVER_AAUDIO

#include "../SDL_sysaudio.h"
#include "../SDL_audio_c.h"
#include "SDL_aaudio.h"

#include "../../core/android/SDL_android.h"
#include <stdbool.h>
#include <aaudio/AAudio.h>

struct SDL_PrivateAudioData
{
    AAudioStream *stream;

    Uint8 *mixbuf;    // Raw mixing buffer
    int frame_size;

    int resume;  // Resume device if it was paused automatically
};

// Debug
#if 0
#define LOGI(...) SDL_Log(__VA_ARGS__);
#else
#define LOGI(...)
#endif

typedef struct AAUDIO_Data
{
    void *handle;
#define SDL_PROC(ret, func, params) ret (*func) params;
#include "SDL_aaudiofuncs.h"
} AAUDIO_Data;
static AAUDIO_Data ctx;

static int AAUDIO_LoadFunctions(AAUDIO_Data *data)
{
#define SDL_PROC(ret, func, params)                                                             \
    do {                                                                                        \
        data->func = (ret (*) params)SDL_LoadFunction(data->handle, #func);                                     \
        if (!data->func) {                                                                      \
            return SDL_SetError("Couldn't load AAUDIO function %s: %s", #func, SDL_GetError()); \
        }                                                                                       \
    } while (0);
#include "SDL_aaudiofuncs.h"
    return 0;
}

static void AAUDIO_errorCallback(AAudioStream *stream, void *userData, aaudio_result_t error)
{
    LOGI("SDL AAUDIO_errorCallback: %d - %s", error, ctx.AAudio_convertResultToText(error));
}

#define LIB_AAUDIO_SO "libaaudio.so"

static int AAUDIO_OpenDevice(SDL_AudioDevice *device)
{
    struct SDL_PrivateAudioData *hidden;
    const SDL_bool iscapture = device->iscapture;
    aaudio_result_t res;

    SDL_assert(device->handle != NULL);  // AAUDIO_UNSPECIFIED is zero, so legit devices should all be non-zero.

    LOGI(__func__);

    if (iscapture) {
        if (!Android_JNI_RequestPermission("android.permission.RECORD_AUDIO")) {
            LOGI("This app doesn't have RECORD_AUDIO permission");
            return SDL_SetError("This app doesn't have RECORD_AUDIO permission");
        }
    }

    hidden = device->hidden = (struct SDL_PrivateAudioData *)SDL_calloc(1, sizeof(*device->hidden));
    if (hidden == NULL) {
        return SDL_OutOfMemory();
    }

    AAudioStreamBuilder *builder = NULL;
    res = ctx.AAudio_createStreamBuilder(&builder);
    if (res != AAUDIO_OK) {
        LOGI("SDL Failed AAudio_createStreamBuilder %d", res);
        return SDL_SetError("SDL Failed AAudio_createStreamBuilder %d", res);
    } else if (builder == NULL) {
        LOGI("SDL Failed AAudio_createStreamBuilder - builder NULL");
        return SDL_SetError("SDL Failed AAudio_createStreamBuilder - builder NULL");
    }

    // !!! FIXME: call AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY); ?

    ctx.AAudioStreamBuilder_setSampleRate(builder, device->spec.freq);
    ctx.AAudioStreamBuilder_setChannelCount(builder, device->spec.channels);

    const int aaudio_device_id = (int) ((size_t) device->handle);
    LOGI("Opening device id %d", aaudio_device_id);
    ctx.AAudioStreamBuilder_setDeviceId(builder, aaudio_device_id);

    const aaudio_direction_t direction = (iscapture ? AAUDIO_DIRECTION_INPUT : AAUDIO_DIRECTION_OUTPUT);
    ctx.AAudioStreamBuilder_setDirection(builder, direction);
    aaudio_format_t format;
    if (device->spec.format == SDL_AUDIO_S32SYS) {
        format = AAUDIO_FORMAT_PCM_I32;
    } else if (device->spec.format == SDL_AUDIO_F32SYS) {
        format = AAUDIO_FORMAT_PCM_FLOAT;
    } else {
        format = AAUDIO_FORMAT_PCM_I16;  // sint16 is a safe bet for everything else.
    }

    ctx.AAudioStreamBuilder_setFormat(builder, format);

    ctx.AAudioStreamBuilder_setErrorCallback(builder, AAUDIO_errorCallback, hidden);

    LOGI("AAudio Try to open %u hz %u bit chan %u %s samples %u",
         device->spec.freq, SDL_AUDIO_BITSIZE(device->spec.format),
         device->spec.channels, (device->spec.format & 0x1000) ? "BE" : "LE", device->sample_frames);

    res = ctx.AAudioStreamBuilder_openStream(builder, &hidden->stream);
    ctx.AAudioStreamBuilder_delete(builder);

    if (res != AAUDIO_OK) {
        LOGI("SDL Failed AAudioStreamBuilder_openStream %d", res);
        return SDL_SetError("%s : %s", __func__, ctx.AAudio_convertResultToText(res));
    }

    device->spec.freq = ctx.AAudioStream_getSampleRate(hidden->stream);
    device->spec.channels = ctx.AAudioStream_getChannelCount(hidden->stream);

    format = ctx.AAudioStream_getFormat(hidden->stream);
    if (format == AAUDIO_FORMAT_PCM_I16) {
        device->spec.format = SDL_AUDIO_S16SYS;
    } else if (format == AAUDIO_FORMAT_PCM_I32) {
        device->spec.format = SDL_AUDIO_S32SYS;
    } else if (format == AAUDIO_FORMAT_PCM_FLOAT) {
        device->spec.format = SDL_AUDIO_F32SYS;
    } else {
        return SDL_SetError("Got unexpected audio format %d from AAudioStream_getFormat", (int) format);
    }

    device->sample_frames = ctx.AAudioStream_getBufferCapacityInFrames(hidden->stream) / 2;

    LOGI("AAudio Try to open %u hz %u bit chan %u %s samples %u",
         device->spec.freq, SDL_AUDIO_BITSIZE(device->spec.format),
         device->spec.channels, SDL_AUDIO_ISBIGENDIAN(device->spec.format) ? "BE" : "LE", device->sample_frames);

    SDL_UpdatedAudioDeviceFormat(device);

    // Allocate mixing buffer
    if (!iscapture) {
        hidden->mixbuf = (Uint8 *)SDL_malloc(device->buffer_size);
        if (hidden->mixbuf == NULL) {
            return SDL_OutOfMemory();
        }
        SDL_memset(hidden->mixbuf, device->silence_value, device->buffer_size);
    }

    hidden->frame_size = device->spec.channels * (SDL_AUDIO_BITSIZE(device->spec.format) / 8);

    res = ctx.AAudioStream_requestStart(hidden->stream);
    if (res != AAUDIO_OK) {
        LOGI("SDL Failed AAudioStream_requestStart %d iscapture:%d", res, iscapture);
        return SDL_SetError("%s : %s", __func__, ctx.AAudio_convertResultToText(res));
    }

    LOGI("SDL AAudioStream_requestStart OK");
    return 0;
}

static void AAUDIO_CloseDevice(SDL_AudioDevice *device)
{
    struct SDL_PrivateAudioData *hidden = device->hidden;
    if (hidden) {
        LOGI(__func__);

        if (hidden->stream) {
            ctx.AAudioStream_requestStop(hidden->stream);
            ctx.AAudioStream_close(hidden->stream);
        }

        SDL_free(hidden->mixbuf);
        SDL_free(hidden);
        device->hidden = NULL;
    }
}

static Uint8 *AAUDIO_GetDeviceBuf(SDL_AudioDevice *device, int *bufsize)
{
    return device->hidden->mixbuf;
}

static void AAUDIO_WaitDevice(SDL_AudioDevice *device)
{
    AAudioStream *stream = device->hidden->stream;
    while (!SDL_AtomicGet(&device->shutdown) && ((int) ctx.AAudioStream_getBufferSizeInFrames(stream)) < device->sample_frames) {
        SDL_Delay(1);
    }
}

static void AAUDIO_PlayDevice(SDL_AudioDevice *device, const Uint8 *buffer, int buflen)
{
    AAudioStream *stream = device->hidden->stream;
    const aaudio_result_t res = ctx.AAudioStream_write(stream, buffer, device->sample_frames, 0);
    if (res < 0) {
        LOGI("%s : %s", __func__, ctx.AAudio_convertResultToText(res));
    } else {
        LOGI("SDL AAudio play: %d frames, wanted:%d frames", (int)res, sample_frames);
    }

#if 0
    // Log under-run count
    {
        static int prev = 0;
        int32_t cnt = ctx.AAudioStream_getXRunCount(hidden->stream);
        if (cnt != prev) {
            SDL_Log("AAudio underrun: %d - total: %d", cnt - prev, cnt);
            prev = cnt;
        }
    }
#endif
}

static int AAUDIO_CaptureFromDevice(SDL_AudioDevice *device, void *buffer, int buflen)
{
    const aaudio_result_t res = ctx.AAudioStream_read(device->hidden->stream, buffer, device->sample_frames, 0);
    if (res < 0) {
        LOGI("%s : %s", __func__, ctx.AAudio_convertResultToText(res));
        return -1;
    }
    LOGI("SDL AAudio capture:%d frames, wanted:%d frames", (int)res, buflen / device->hidden->frame_size);
    return res * device->hidden->frame_size;
}

static void AAUDIO_Deinitialize(void)
{
    Android_StopAudioHotplug();

    LOGI(__func__);
    if (ctx.handle) {
        SDL_UnloadObject(ctx.handle);
    }
    SDL_zero(ctx);
    LOGI("End AAUDIO %s", SDL_GetError());
}

static SDL_bool AAUDIO_Init(SDL_AudioDriverImpl *impl)
{
    LOGI(__func__);

    /* AAudio was introduced in Android 8.0, but has reference counting crash issues in that release,
     * so don't use it until 8.1.
     *
     * See https://github.com/google/oboe/issues/40 for more information.
     */
    if (SDL_GetAndroidSDKVersion() < 27) {
        return SDL_FALSE;
    }

    SDL_zero(ctx);

    ctx.handle = SDL_LoadObject(LIB_AAUDIO_SO);
    if (ctx.handle == NULL) {
        LOGI("SDL couldn't find " LIB_AAUDIO_SO);
        return SDL_FALSE;
    }

    if (AAUDIO_LoadFunctions(&ctx) < 0) {
        SDL_UnloadObject(ctx.handle);
        SDL_zero(ctx);
        return SDL_FALSE;
    }

    impl->ThreadInit = Android_AudioThreadInit;
    impl->DetectDevices = Android_StartAudioHotplug;
    impl->Deinitialize = AAUDIO_Deinitialize;
    impl->OpenDevice = AAUDIO_OpenDevice;
    impl->CloseDevice = AAUDIO_CloseDevice;
    impl->WaitDevice = AAUDIO_WaitDevice;
    impl->PlayDevice = AAUDIO_PlayDevice;
    impl->GetDeviceBuf = AAUDIO_GetDeviceBuf;
    impl->WaitCaptureDevice = AAUDIO_WaitDevice;
    impl->CaptureFromDevice = AAUDIO_CaptureFromDevice;

    impl->HasCaptureSupport = SDL_TRUE;

    LOGI("SDL AAUDIO_Init OK");
    return SDL_TRUE;
}

AudioBootStrap AAUDIO_bootstrap = {
    "AAudio", "AAudio audio driver", AAUDIO_Init, SDL_FALSE
};


static SDL_bool PauseOneDevice(SDL_AudioDevice *device, void *userdata)
{
    struct SDL_PrivateAudioData *hidden = (struct SDL_PrivateAudioData *)device->hidden;
    if (hidden != NULL) {
        if (hidden->stream) {
            aaudio_result_t res;

            if (device->iscapture) {
                // Pause() isn't implemented for 'capture', use Stop()
                res = ctx.AAudioStream_requestStop(hidden->stream);
            } else {
                res = ctx.AAudioStream_requestPause(hidden->stream);
            }

            if (res != AAUDIO_OK) {
                LOGI("SDL Failed AAudioStream_requestPause %d", res);
                SDL_SetError("%s : %s", __func__, ctx.AAudio_convertResultToText(res));
            }

            SDL_LockMutex(device->lock);
            hidden->resume = SDL_TRUE;
        }
    }
    return SDL_FALSE;  // keep enumerating.
}

// Pause (block) all non already paused audio devices by taking their mixer lock
void AAUDIO_PauseDevices(void)
{
    if (ctx.handle != NULL) {  // AAUDIO driver is used?
        (void) SDL_FindPhysicalAudioDeviceByCallback(PauseOneDevice, NULL);
    }
}

// Resume (unblock) all non already paused audio devices by releasing their mixer lock
static SDL_bool ResumeOneDevice(SDL_AudioDevice *device, void *userdata)
{
    struct SDL_PrivateAudioData *hidden = device->hidden;
    if (hidden != NULL) {
        if (hidden->resume) {
            hidden->resume = SDL_FALSE;
            SDL_UnlockMutex(device->lock);
        }

        if (hidden->stream) {
            aaudio_result_t res = ctx.AAudioStream_requestStart(hidden->stream);
            if (res != AAUDIO_OK) {
                LOGI("SDL Failed AAudioStream_requestStart %d", res);
                SDL_SetError("%s : %s", __func__, ctx.AAudio_convertResultToText(res));
            }
        }
    }
    return SDL_FALSE;  // keep enumerating.
}

void AAUDIO_ResumeDevices(void)
{
    if (ctx.handle != NULL) {  // AAUDIO driver is used?
        (void) SDL_FindPhysicalAudioDeviceByCallback(ResumeOneDevice, NULL);
    }
}

/*
 We can sometimes get into a state where AAudioStream_write() will just block forever until we pause and unpause.
 None of the standard state queries indicate any problem in my testing. And the error callback doesn't actually get called.
 But, AAudioStream_getTimestamp() does return AAUDIO_ERROR_INVALID_STATE
*/
static SDL_bool DetectBrokenPlayStatePerDevice(SDL_AudioDevice *device, void *userdata)
{
    SDL_assert(device != NULL);
    if (!device->iscapture && device->hidden != NULL) {
        struct SDL_PrivateAudioData *hidden = device->hidden;
        int64_t framePosition, timeNanoseconds;
        aaudio_result_t res = ctx.AAudioStream_getTimestamp(hidden->stream, CLOCK_MONOTONIC, &framePosition, &timeNanoseconds);
        if (res == AAUDIO_ERROR_INVALID_STATE) {
            aaudio_stream_state_t currentState = ctx.AAudioStream_getState(hidden->stream);
            // AAudioStream_getTimestamp() will also return AAUDIO_ERROR_INVALID_STATE while the stream is still initially starting. But we only care if it silently went invalid while playing.
            if (currentState == AAUDIO_STREAM_STATE_STARTED) {
                LOGI("SDL AAUDIO_DetectBrokenPlayState: detected invalid audio device state: AAudioStream_getTimestamp result=%d, framePosition=%lld, timeNanoseconds=%lld, getState=%d", (int)res, (long long)framePosition, (long long)timeNanoseconds, (int)currentState);
                return SDL_TRUE;  // this guy.
            }
        }
    }

    return SDL_FALSE;  // enumerate more devices.
}

SDL_bool AAUDIO_DetectBrokenPlayState(void)
{
    return (ctx.handle && SDL_FindPhysicalAudioDeviceByCallback(DetectBrokenPlayStatePerDevice, NULL) != NULL) ? SDL_TRUE : SDL_FALSE;
}

#endif // SDL_AUDIO_DRIVER_AAUDIO
