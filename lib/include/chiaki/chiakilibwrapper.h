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

    // 修改后的FrameBuffer结构
    typedef struct FrameBuffer
    {
        AVFrame *frame;
        struct SwsContext *sws_ctx;
        AVFrame *rgb_frame;
        ChiakiMutex mutex;
    } FrameBuffer;

    static FrameBuffer front_buffer;
    static FrameBuffer back_buffer;
    static bool buffer_swapped = false;
    static ChiakiMutex swap_mutex;

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

    // 退出回调函数类型定义
    typedef void (*ChiakiQuitCallback)(int quit_reason, const char* reason_string, void* user_data);

    CHIAKI_EXPORT ChiakiErrorCode start_session(const char *host,
                                                const char *string_rp_key,
                                                const char *rp_regist_key,
                                                ChiakiTarget target,
                                                ChiakiLog *log,
                                                ChiakiQuitCallback quit_callback,
                                                void* user_data);

    CHIAKI_EXPORT void goto_bed();

    CHIAKI_EXPORT void sendControllButton(uint32_t buttonMask, unsigned int sleepTimeMs);
    CHIAKI_EXPORT void sendControllAnlogButton(uint32_t buttonMask, unsigned int sleepTimeMs, uint8_t strength);
    CHIAKI_EXPORT void sendLeftStickDirection(float angle_rad, int sleepTimeMs, int16_t strength);
    CHIAKI_EXPORT void sendLeftStickThenButton(
        float angle_rad, 
        int16_t strength, 
        unsigned int stickHoldMs,   // 摇杆先保持多久
        uint32_t buttonMask, 
        unsigned int buttonHoldMs   // 按键按下多久
    );
    CHIAKI_EXPORT void sendRightStickDirection(float angle_rad, int sleepTimeMs, int16_t strength);
    CHIAKI_EXPORT void sendRightStickThenButton(
        float angle_rad, 
        int16_t strength, 
        unsigned int stickHoldMs,   // 摇杆先保持多久
        uint32_t buttonMask, 
        unsigned int buttonHoldMs   // 按键按下多久
    );
    CHIAKI_EXPORT void powerShoot(
        float angle_rad,                // 左摇杆方向角度
        int16_t strength,               // 左摇杆力度
        unsigned int l1r1StickHoldMs,   // L1+R1+左摇杆 同时按住的时间（毫秒）
        unsigned int chargeMs           // 蓄力时长（毫秒，对应70-90%蓄力）
    );
    CHIAKI_EXPORT void chipShot(
        float angle_rad,            // 左摇杆方向角度
        int16_t strength,           // 左摇杆力度
        unsigned int l1HoldMs,      // L1 按住的时间（毫秒）
        unsigned int chargeMs       // 圆圈键蓄力时长（毫秒）
    );

#ifdef __cplusplus
}
#endif

#endif // CHIAKI_LIB_WRAPPER_H
