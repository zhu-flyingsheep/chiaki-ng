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

    // 回调函数类型定义
    // param data:     RGB24格式帧数据指针 (内存布局: 连续排列的width*height*3字节)
    // param width:    帧宽度
    // param height:   帧高度
    // param stride:   每行字节数 (通常为width*3)
    // param userdata: 用户自定义指针 (用于传递C#对象上下文)
    typedef void (*FrameCallback)(const uint8_t *data, int width, int height, int stride, void *userdata, void *release_func);

    // 初始化视频回调系统 (需在调用其他函数前执行)
    // return: 0成功, 非零错误码
    CHIAKI_EXPORT int VideoCallbackInit();

    // 设置全局帧回调
    // param callback: 回调函数指针
    // param userdata: 透传给回调的用户数据
    CHIAKI_EXPORT void VideoSetCallback(FrameCallback callback, void *userdata);

    // 清理视频回调系统资源
    CHIAKI_EXPORT void VideoCallbackFree();
    CHIAKI_EXPORT void ReleaseCurrentFrame(); // 释放当前帧的导出函数
    // 帧处理入口函数 (需在获取AVFrame后调用)
    // param frame: 从FFmpeg获取的AVFrame指针
    CHIAKI_EXPORT void VideoProcessFrame(AVFrame *frame,enum AVPixelFormat  pixformat);

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

    // 全局回调变量
    static FrameCallback g_frame_callback = NULL;
    static void *g_userdata = NULL;
    static struct SwsContext *g_sws_ctx = NULL;
    static ChiakiFfmpegDecoder *ffmpeg_decoder;
    CHIAKI_EXPORT ChiakiErrorCode pull_frame(const char *host,
                                             const char *string_rp_key,
                                             const char *rp_regist_key,
                                             ChiakiTarget target,
                                             ChiakiLog *log);

#ifdef __cplusplus
}
#endif

#endif // CHIAKI_LIB_WRAPPER_H
