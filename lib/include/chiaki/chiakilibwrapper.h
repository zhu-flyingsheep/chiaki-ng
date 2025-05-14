// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#ifndef CHIAKI_LIB_WRAPPER_H
#define CHIAKI_LIB_WRAPPER_H

#include "discoveryservice.h"
#include "regist.h"
#include "session.h"
#include "ffmpegdecoder.h"
#include <libswscale/swscale.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        uint8_t *data;
        int width;
        int height;
        int linesize;
    } RGBFrameInfo;

    // 新增结构体管理帧缓冲
    typedef struct FrameBuffer
    {
        AVFrame *frame;
        struct SwsContext *sws_ctx;
        AVFrame *rgb_frame;
        ChiakiMutex mutex;
    } FrameBuffer;
    static FrameBuffer front_buffer;  // 前台缓冲（用于显示）
    static FrameBuffer back_buffer;   // 后台缓冲（用于解码）
    static atomic_bool buffer_swapped = false;

    CHIAKI_EXPORT void ReleaseCurrentFrame(); // 释放当前帧的导出函数

    CHIAKI_EXPORT RGBFrameInfo pullRgbFrame();

    CHIAKI_EXPORT ChiakiErrorCode discovery_ps(ChiakiDiscoveryServiceCb cb, ChiakiLog *log);

    CHIAKI_EXPORT bool wakeup_ps(const char *host, const char *regist_key, bool ps5, ChiakiLog *log);
    CHIAKI_EXPORT ChiakiRegist *regist_ps(const char *host,
                                          const char *psn_id,
                                          const char *pin,
                                          const char *cpin,
                                          bool broadcast,
                                          int target,
                                          ChiakiRegistCb cb,
                                          ChiakiLog *log,
                                          void *cb_user);
    static ChiakiFfmpegDecoder *ffmpeg_decoder;
    static ChiakiSession *session;




    CHIAKI_EXPORT ChiakiErrorCode start_session(const char *host,
                                                const char *string_rp_key,
                                                const char *rp_regist_key,
                                                ChiakiTarget target,
                                                ChiakiLog *log);

    CHIAKI_EXPORT void goto_bed();

    CHIAKI_EXPORT void sendControllButton(uint32_t buttonMask, unsigned int sleepTimeMs);
    CHIAKI_EXPORT void sendControllAnlogButton(uint32_t buttonMask, unsigned int sleepTimeMs, uint8_t strength);
    CHIAKI_EXPORT void sendLeftStickDirection(float angle_rad, int sleepTimeMs, int16_t strength);

#ifdef __cplusplus
}
#endif

#endif // CHIAKI_LIB_WRAPPER_H
