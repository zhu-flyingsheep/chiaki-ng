// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/chiakilibwrapper.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#ifdef _WIN32
#include <winsock2.h>
#include <iphlpapi.h>
#else
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#endif

#define PING_MS 500
#define HOSTS_MAX 16
#define DROP_PINGS 3

// 假设的全局变量
ChiakiDiscoveryService service;
ChiakiDiscoveryService service_ipv6;

bool service_active = false;
bool service_active_ipv6 = false;

CHIAKI_EXPORT ChiakiErrorCode discovery_ps(ChiakiDiscoveryServiceCb cb, ChiakiLog *log)
{
    ChiakiErrorCode err; // 在函数开头定义 err

    ChiakiDiscoveryServiceOptions options = {0};
    options.ping_ms = PING_MS;
    options.hosts_max = HOSTS_MAX;
    options.host_drop_pings = DROP_PINGS;
    options.cb = cb;
    options.cb_user = NULL;

    struct sockaddr_in in_addr = {0};
    in_addr.sin_family = AF_INET;
    in_addr.sin_addr.s_addr = 0xffffffff; // 255.255.255.255
    struct sockaddr_storage addr;
    memcpy(&addr, &in_addr, sizeof(in_addr));
    options.send_addr = &addr;
    options.send_addr_size = sizeof(in_addr);
    options.send_host = NULL;
    options.broadcast_addrs = NULL;
    options.broadcast_num = 0;

    int *broadcast_addresses = NULL;
    int broadcast_addresses_count = 0;
    int status = 0;

#ifdef _WIN32
    uint8_t loc_address[4] = {0};
    uint8_t loc_mask[4] = {0};
    uint8_t loc_broadcast[4] = {0};
    PIP_ADAPTER_INFO pAdapterInfo;
    PIP_ADAPTER_INFO pAdapter = NULL;
    DWORD dwRetVal = 0;
    ULONG ulOutBufLen = sizeof(IP_ADAPTER_INFO);
    pAdapterInfo = (IP_ADAPTER_INFO *)malloc(sizeof(IP_ADAPTER_INFO));
    if (pAdapterInfo == NULL)
    {
        CHIAKI_LOGE(log, "Error allocating memory needed to call GetAdaptersinfo\n");
        return -1;
    }
    if (GetAdaptersInfo(pAdapterInfo, &ulOutBufLen) == ERROR_BUFFER_OVERFLOW)
    {
        free(pAdapterInfo);
        pAdapterInfo = (IP_ADAPTER_INFO *)malloc(ulOutBufLen);
        if (pAdapterInfo == NULL)
        {
            CHIAKI_LOGE(log, "Error allocating memory needed to call GetAdaptersinfo\n");
            return -1;
        }
    }

    if ((dwRetVal = GetAdaptersInfo(pAdapterInfo, &ulOutBufLen)) == NO_ERROR)
    {
        pAdapter = pAdapterInfo;
        while (pAdapter)
        {
            if (pAdapter->Type != IF_TYPE_IEEE80211 && pAdapter->Type != MIB_IF_TYPE_ETHERNET)
            {
                pAdapter = pAdapter->Next;
                continue;
            }
            for (IP_ADDR_STRING *str = &pAdapter->IpAddressList; str != NULL; str = str->Next)
            {
                if (strcmp(str->IpAddress.String, "") == 0)
                {
                    continue;
                }
                if (strcmp(str->IpAddress.String, "0.0.0.0") == 0)
                {
                    continue;
                }
                inet_pton(AF_INET, str->IpAddress.String, &loc_address);
                inet_pton(AF_INET, str->IpMask.String, &loc_mask);
                for (int i = 0; i < 4; i++)
                {
                    loc_broadcast[i] = loc_address[i] | ~loc_mask[i];
                }
                uint32_t final = 0;
                memcpy(&final, &loc_broadcast, 4);
                int found = 0;
                for (int i = 0; i < broadcast_addresses_count; i++)
                {
                    if (broadcast_addresses[i] == final)
                    {
                        found = 1;
                        break;
                    }
                }
                if (!found)
                {
                    broadcast_addresses = (int *)realloc(broadcast_addresses, (broadcast_addresses_count + 1) * sizeof(int));
                    broadcast_addresses[broadcast_addresses_count++] = final;
                }
                status = 1;
            }
            pAdapter = pAdapter->Next;
        }
    }
    else
    {
        CHIAKI_LOGE(log, "GetAdaptersInfo failed with error: %d\n", dwRetVal);
    }
    if (pAdapterInfo)
        free(pAdapterInfo);
#else
    struct ifaddrs *local_addrs, *current_addr;
    struct sockaddr_in *res4 = NULL;

    struct addrinfo hints;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if (getifaddrs(&local_addrs) != 0)
    {
        CHIAKI_LOGE(log, "Couldn't get local address");
        return -1;
    }
    for (current_addr = local_addrs; current_addr != NULL; current_addr = current_addr->ifa_next)
    {
        if (current_addr->ifa_addr == NULL)
            continue;
        if ((current_addr->ifa_flags & (IFF_UP | IFF_RUNNING | IFF_LOOPBACK | IFF_BROADCAST)) != (IFF_UP | IFF_RUNNING | IFF_BROADCAST))
            continue;
        if (current_addr->ifa_addr->sa_family == AF_INET)
        {
            res4 = (struct sockaddr_in *)current_addr->ifa_broadaddr;
            int found = 0;
            for (int i = 0; i < broadcast_addresses_count; i++)
            {
                if (broadcast_addresses[i] == res4->sin_addr.s_addr)
                {
                    found = 1;
                    break;
                }
            }
            if (!found)
            {
                broadcast_addresses = (int *)realloc(broadcast_addresses, (broadcast_addresses_count + 1) * sizeof(int));
                broadcast_addresses[broadcast_addresses_count++] = res4->sin_addr.s_addr;
            }
            status = 1;
        }
    }
    freeifaddrs(local_addrs);
#endif

    if (status)
    {
        options.broadcast_addrs = (struct sockaddr_storage *)malloc(broadcast_addresses_count * sizeof(struct sockaddr_storage));
        if (!options.broadcast_addrs)
        {
            CHIAKI_LOGE(log, "Error allocating memory for broadcast addresses!");
            free(broadcast_addresses);
            return -1;
        }
        for (int i = 0; i < broadcast_addresses_count; i++)
        {
            struct sockaddr_in in_addr_broadcast = {0};
            in_addr_broadcast.sin_family = AF_INET;
            in_addr_broadcast.sin_addr.s_addr = broadcast_addresses[i];
            memcpy(&options.broadcast_addrs[i], &in_addr_broadcast, sizeof(in_addr_broadcast));
            options.broadcast_num++;
        }
    }
    else
    {
        CHIAKI_LOGW(log, "No external broadcast addresses found!");
    }

    err = chiaki_discovery_service_init(&service, &options, log);
    if (options.broadcast_addrs)
        free(options.broadcast_addrs);
    if (broadcast_addresses)
        free(broadcast_addresses);
    if (err != CHIAKI_ERR_SUCCESS)
    {
        service_active = false;
        CHIAKI_LOGE(log, "DiscoveryManager failed to init Discovery Service IPV4");
        return err;
    }
    else
    {
        service_active = true;
    }

    ChiakiDiscoveryServiceOptions options_ipv6 = {};
    options_ipv6.ping_ms = PING_MS;
    options_ipv6.hosts_max = HOSTS_MAX;
    options_ipv6.host_drop_pings = DROP_PINGS;
    options_ipv6.cb = cb;
    options_ipv6.cb_user = NULL;

    struct sockaddr_in6 in_addr_ipv6 = {};
    in_addr_ipv6.sin6_family = AF_INET6;
    inet_pton(AF_INET6, "FF02::1", &in_addr_ipv6.sin6_addr);
    struct sockaddr_storage addr_ipv6;
    memcpy(&addr_ipv6, &in_addr_ipv6, sizeof(in_addr_ipv6));
    options_ipv6.send_addr = &addr_ipv6;
    options_ipv6.send_addr_size = sizeof(in_addr_ipv6);
    options_ipv6.send_host = NULL;

    err = chiaki_discovery_service_init(&service_ipv6, &options_ipv6, log);
    if (err != CHIAKI_ERR_SUCCESS)
    {
        CHIAKI_LOGE(log, "DiscoveryManager failed to init Discovery Service IPV6");
        service_active_ipv6 = false;
    }
    else
    {
        service_active_ipv6 = true;
    }

    return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT bool wakeup_ps(const char *host, const char *regist_key, bool ps5, ChiakiLog *log)
{
    size_t key_size = strlen(regist_key);
    char *key = (char *)malloc(key_size + 1);
    if (key == NULL)
    {
        CHIAKI_LOGE(log, "Memory allocation failed");

        return false;
    }
    strcpy(key, regist_key);

    for (size_t i = 0; i < key_size; i++)
    {
        if (key[i] == '\0')
        {
            key[i] = '\0';
            break;
        }
    }

    uint64_t credential = 0;
    char *endptr;
    credential = strtoull(key, &endptr, 16);
    if (*endptr != '\0' || key_size > 16)
    {
        CHIAKI_LOGE(log, "DiscoveryManager got invalid regist key for wakeup");
        return false;
    }

    char *ipv6 = strchr(host, ':');
    int err;
    if (ipv6)
    {
        err = chiaki_discovery_wakeup(log, service_active_ipv6 ? &service_ipv6.discovery : NULL, host, credential, ps5);
    }
    else
    {
        err = chiaki_discovery_wakeup(log, service_active ? &service.discovery : NULL, host, credential, ps5);
    }

    if (err != CHIAKI_ERR_SUCCESS)
    {
        CHIAKI_LOGE(log, "Failed to send Packet: %s\n", chiaki_error_string(err));
        return false;
    }

    return true;
}
// 假设 to_ulong 和 to_ulonglong 函数的简单实现
uint32_t to_ulong(const char *str)
{
    return (uint32_t)strtoul(str, NULL, 10);
}

uint64_t to_ulonglong(const char *str, int *ok)
{
    char *endptr;
    uint64_t result = strtoull(str, &endptr, 10);
    if (*endptr != '\0')
    {
        *ok = 0;
    }
    else
    {
        *ok = 1;
    }
    return result;
}

CHIAKI_EXPORT ChiakiRegist *regist_ps(const char *host, const char *psn_id, const char *pin, const char *cpin, bool broadcast, int target, ChiakiRegistCb cb, ChiakiLog *log, void *cb_user)
{
    ChiakiRegistInfo info;
    memset(&info, 0, sizeof(ChiakiRegistInfo));
    info.host = host;
    info.target = target;
    info.broadcast = broadcast;
    info.pin = to_ulong(pin);
    info.console_pin = to_ulong(cpin);
    info.holepunch_info = NULL;
    info.rudp = NULL;

    if (target == CHIAKI_TARGET_PS4_8)
    {
        info.psn_online_id = psn_id;
    }
    else
    {
        int ok;
        uint64_t intId = to_ulonglong(psn_id, &ok);
        if (!ok)
        {
            char message[256];
            snprintf(message, sizeof(message), "请检查psnID是否正确%d", CHIAKI_PSN_ACCOUNT_ID_SIZE);
            CHIAKI_LOGE(log, message);
            return NULL;
        }

        for (int i = 0; i < CHIAKI_PSN_ACCOUNT_ID_SIZE; ++i)
        {
            info.psn_account_id[i] = (intId >> (i * 8)) & 0xFF;
        }

        info.psn_online_id = NULL;
    }

    // 动态分配 ChiakiRegist 对象的内存
    ChiakiRegist *chiaki_regist = (ChiakiRegist *)malloc(sizeof(ChiakiRegist));
    if (chiaki_regist == NULL)
    {
        CHIAKI_LOGE(log, "malloc failed!");
        return NULL;
    }

    ChiakiErrorCode result = chiaki_regist_start(chiaki_regist, log, &info, cb, cb_user);
    if (result != CHIAKI_ERR_SUCCESS)
    {
        chiaki_regist_fini(chiaki_regist);
        chiaki_regist_stop(chiaki_regist);
        char error_msg[256];
        switch (result)
        {
        case CHIAKI_ERR_UNKNOWN:
            snprintf(error_msg, sizeof(error_msg), "regist failed,error code: CHIAKI_ERR_UNKNOWN");
            break;
        case CHIAKI_ERR_PARSE_ADDR:
            snprintf(error_msg, sizeof(error_msg), "regist failed,error code: CHIAKI_ERR_PARSE_ADDR");
            break;
        case CHIAKI_ERR_THREAD:
            snprintf(error_msg, sizeof(error_msg), "regist failed,error code: CHIAKI_ERR_THREAD");
            break;
        case CHIAKI_ERR_MEMORY:
            snprintf(error_msg, sizeof(error_msg), "regist failed,error code: CHIAKI_ERR_MEMORY");
            break;
        case CHIAKI_ERR_OVERFLOW:
            snprintf(error_msg, sizeof(error_msg), "regist failed,error code: CHIAKI_ERR_OVERFLOW");
            break;
        case CHIAKI_ERR_NETWORK:
            snprintf(error_msg, sizeof(error_msg), "regist failed,error code: CHIAKI_ERR_NETWORK");
            break;
        case CHIAKI_ERR_CONNECTION_REFUSED:
            snprintf(error_msg, sizeof(error_msg), "regist failed,error code: CHIAKI_ERR_CONNECTION_REFUSED");
            break;
        case CHIAKI_ERR_HOST_DOWN:
            snprintf(error_msg, sizeof(error_msg), "regist failed,error code: CHIAKI_ERR_HOST_DOWN");
            break;
        case CHIAKI_ERR_HOST_UNREACH:
            snprintf(error_msg, sizeof(error_msg), "regist failed,error code: CHIAKI_ERR_HOST_UNREACH");
            break;
        case CHIAKI_ERR_DISCONNECTED:
            snprintf(error_msg, sizeof(error_msg), "regist failed,error code: CHIAKI_ERR_DISCONNECTED");
            break;
        case CHIAKI_ERR_INVALID_DATA:
            snprintf(error_msg, sizeof(error_msg), "regist failed,error code: CHIAKI_ERR_INVALID_DATA");
            break;
        case CHIAKI_ERR_BUF_TOO_SMALL:
            snprintf(error_msg, sizeof(error_msg), "regist failed,error code: CHIAKI_ERR_BUF_TOO_SMALL");
            break;
        case CHIAKI_ERR_MUTEX_LOCKED:
            snprintf(error_msg, sizeof(error_msg), "regist failed,error code: CHIAKI_ERR_MUTEX_LOCKED");
            break;
        case CHIAKI_ERR_CANCELED:
            snprintf(error_msg, sizeof(error_msg), "regist failed,error code: CHIAKI_ERR_CANCELED");
            break;
        case CHIAKI_ERR_TIMEOUT:
            snprintf(error_msg, sizeof(error_msg), "regist failed,error code: CHIAKI_ERR_TIMEOUT");
            break;
        case CHIAKI_ERR_INVALID_RESPONSE:
            snprintf(error_msg, sizeof(error_msg), "regist failed,error code: CHIAKI_ERR_INVALID_RESPONSE");
            break;
        case CHIAKI_ERR_INVALID_MAC:
            snprintf(error_msg, sizeof(error_msg), "regist failed,error code: CHIAKI_ERR_INVALID_MAC");
            break;
        case CHIAKI_ERR_UNINITIALIZED:
            snprintf(error_msg, sizeof(error_msg), "regist failed,error code: CHIAKI_ERR_UNINITIALIZED");
            break;
        case CHIAKI_ERR_FEC_FAILED:
            snprintf(error_msg, sizeof(error_msg), "regist failed,error code: CHIAKI_ERR_FEC_FAILED");
            break;
        case CHIAKI_ERR_VERSION_MISMATCH:
            snprintf(error_msg, sizeof(error_msg), "regist failed,error code: CHIAKI_ERR_VERSION_MISMATCH");
            break;
        case CHIAKI_ERR_HTTP_NONOK:
            snprintf(error_msg, sizeof(error_msg), "regist failed,error code: CHIAKI_ERR_HTTP_NONOK");
            break;
        default:
            snprintf(error_msg, sizeof(error_msg), "regist failed: %d", result);
        }
        CHIAKI_LOGE(log, error_msg);
        return NULL;
    }
    else
    {
        CHIAKI_LOGI(log, "regist success!");
    }

    return chiaki_regist;
}

// 十六进制字符转数字
int hex_char_to_int(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    else if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    else if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    return -1;
}

// 十六进制字符串转 uint8_t 数组
void hex_string_to_uint8_array(const char *str, uint8_t *arr, size_t arr_size)
{
    size_t str_len = strlen(str);
    if (str_len != arr_size * 2)
    {
        return;
    }
    for (size_t i = 0; i < arr_size; i++)
    {
        int high = hex_char_to_int(str[i * 2]);
        int low = hex_char_to_int(str[i * 2 + 1]);
        if (high == -1 || low == -1)
        {
            return;
        }
        arr[i] = (high << 4) | low;
    }
}

static void MyFfmpegFrameCb(ChiakiFfmpegDecoder *decoder, void *user);

CHIAKI_EXPORT ChiakiErrorCode pull_frame(const char *host,
                                         const char *string_rp_key,
                                         const char *string_rp_regist_key,
                                         ChiakiTarget target,
                                         ChiakiLog *log)
{
    uint8_t morning[16];
    uint8_t regist_key[16];
    // 十六进制字符串转 uint8_t 数组
    hex_string_to_uint8_array(string_rp_key, morning, 16);
    hex_string_to_uint8_array(string_rp_regist_key, regist_key, 16);
    ChiakiConnectInfo connect_info = {0};
    connect_info.host = host;

    connect_info.ps5 = chiaki_target_is_ps5(target);
    connect_info.auto_regist = false;
    connect_info.holepunch_session = false;
    connect_info.packet_loss_max = 0.0f;

    memset(&connect_info.video_profile, 0, sizeof(connect_info.video_profile));
    connect_info.video_profile.width = 1280;
    connect_info.video_profile.height = 720;
    connect_info.video_profile.max_fps = 60;
    connect_info.video_profile.bitrate = 1000;
    connect_info.video_profile.codec = CHIAKI_CODEC_H264;
    connect_info.video_profile_auto_downgrade = true;
    connect_info.enable_keyboard = false;
    connect_info.enable_dualsense = true;
    connect_info.audio_video_disabled = CHIAKI_NONE_DISABLED;

    connect_info.packet_loss_max = 0.050000f;
    ChiakiErrorCode err;

    memcpy(connect_info.regist_key, regist_key, sizeof(regist_key));
    memcpy(connect_info.morning, morning, sizeof(morning));

    ChiakiSession *session = (ChiakiSession *)malloc(sizeof(ChiakiSession));
    if (session == NULL)
    {
        CHIAKI_LOGE(log, "Session malloc failed");
        chiaki_session_fini(session);
        return err;
    }

    ChiakiFfmpegDecoder *ffmpeg_decoder = (ChiakiFfmpegDecoder *)malloc(sizeof(ChiakiFfmpegDecoder));
    if (ffmpeg_decoder == NULL)
    {
        CHIAKI_LOGE(log, "ffmpeg_decoder malloc failed");
        return -1;
    }

    err = chiaki_ffmpeg_decoder_init(ffmpeg_decoder,
                                     log,
                                     CHIAKI_CODEC_H264,
                                     "d3d11va",
                                     NULL,
                                     MyFfmpegFrameCb,
                                     session);

    if (err != CHIAKI_ERR_SUCCESS)
    {
        CHIAKI_LOGE(log, "ffmpeg_decoder init failed: %s", chiaki_error_string(err));
        chiaki_ffmpeg_decoder_fini(ffmpeg_decoder);
        free(ffmpeg_decoder);
        return err;
    }

    err = chiaki_session_init(session, &connect_info, log);
    if (err != CHIAKI_ERR_SUCCESS)
    {
        CHIAKI_LOGE(log, "Session init failed: %s", chiaki_error_string(err));
        return err;
    }

    chiaki_session_set_video_sample_cb(session, chiaki_ffmpeg_decoder_video_sample_cb, ffmpeg_decoder);

    err = chiaki_session_start(session);
    if (err != CHIAKI_ERR_SUCCESS)
    {
        CHIAKI_LOGE(log, "Session start failed: %s", chiaki_error_string(err));
        chiaki_session_fini(session);
        return err;
    }
    return CHIAKI_ERR_SUCCESS; // 返回成功状态
}

// 全局状态
static struct
{
    FrameCallback callback;
    void *userdata;
    struct SwsContext *sws_ctx;
    int last_width;
    int last_height;
} g_ctx = {0};

CHIAKI_EXPORT int VideoCallbackInit()
{
    if (g_ctx.sws_ctx)
    {
        sws_freeContext(g_ctx.sws_ctx);
        g_ctx.sws_ctx = NULL;
    }
    g_ctx.last_width = 0;
    g_ctx.last_height = 0;
    return 0;
}

CHIAKI_EXPORT void VideoSetCallback(FrameCallback callback, void *userdata)
{
    g_ctx.callback = callback;
    g_ctx.userdata = userdata;
}

CHIAKI_EXPORT void VideoCallbackFree()
{
    VideoCallbackInit(); // 重用初始化逻辑清理资源
}

// 全局变量保存当前待释放的帧（仅支持单帧活跃）
static AVFrame *g_current_rgb_frame = NULL;

// 释放当前帧的导出函数
CHIAKI_EXPORT void ReleaseCurrentFrame()
{
    if (g_current_rgb_frame)
    {
        av_frame_free(&g_current_rgb_frame);
        g_current_rgb_frame = NULL;
    }
}

CHIAKI_EXPORT void VideoProcessFrame(AVFrame *frame, enum AVPixelFormat pixformat)
{
    if (!g_ctx.callback || !frame)
        return;

    // 每次临时创建 SwsContext，避免全局缓存
    struct SwsContext *sws_ctx = sws_getContext(
        frame->width, frame->height, pixformat,
        frame->width, frame->height, AV_PIX_FMT_BGR48,
        SWS_BICUBIC, NULL, NULL, NULL);
    if (!sws_ctx)
    {
        return;
    }

    AVFrame *rgb_frame = av_frame_alloc();
    if (!rgb_frame)
    {
        sws_freeContext(sws_ctx);
        return;
    }

    rgb_frame->format = AV_PIX_FMT_BGR48;
    rgb_frame->width = frame->width;
    rgb_frame->height = frame->height;

    if (av_frame_get_buffer(rgb_frame, 0) < 0)
    {
        av_frame_free(&rgb_frame);
        sws_freeContext(sws_ctx);
        return;
    }

    sws_scale(sws_ctx,
              frame->data, frame->linesize, 0, frame->height,
              rgb_frame->data, rgb_frame->linesize);
    // 保存当前帧
    g_current_rgb_frame = rgb_frame;
    if (g_ctx.callback)
    {
        g_ctx.callback(
            rgb_frame->data[0],
            rgb_frame->width,
            rgb_frame->height,
            rgb_frame->linesize[0],
            g_ctx.userdata,
            (void *)ReleaseCurrentFrame // 直接传递释放函数指针
        );
    }
    sws_freeContext(sws_ctx);
}

static void MyFfmpegFrameCb(ChiakiFfmpegDecoder *decoder, void *session)
{
    ChiakiSession *sess = (ChiakiSession *)session;

    if (!decoder)
    {
        CHIAKI_LOGE(sess->log, "Session has no FFmpeg decoder\n");
        return;
    }
    int32_t frames_lost;
    AVFrame *frame = chiaki_ffmpeg_decoder_pull_frame(decoder, &frames_lost);
    if (!frame)
        return;

    // 手动定义支持零拷贝的格式数组
    static const int zero_copy_formats[] = {
        AV_PIX_FMT_VULKAN,
#ifdef __linux__
        AV_PIX_FMT_VAAPI,
#endif
        -1 // 数组结束标志
    };

    int i;
    int zero_copy_supported = 0;
    for (i = 0; zero_copy_formats[i] != -1; i++)
    {
        if (zero_copy_formats[i] == frame->format)
        {
            zero_copy_supported = 1;
            break;
        }
    }

    if (frame->hw_frames_ctx && (!zero_copy_supported))
    {
        AVFrame *sw_frame = av_frame_alloc();
        if (av_hwframe_transfer_data(sw_frame, frame, 0) < 0)
        {
            CHIAKI_LOGE(sess->log, "Failed to transfer frame from hardware\n");
            av_frame_unref(frame);
            av_frame_free(&sw_frame);
            return;
        }
        av_frame_copy_props(sw_frame, frame);
        av_frame_unref(frame);
        frame = sw_frame;
    }
    enum AVPixelFormat pixformat = chiaki_ffmpeg_decoder_get_pixel_format(decoder);
    // 在这里可以添加处理 frame 的代码
    VideoProcessFrame(frame, pixformat); // 只需添加这一行
    // 释放 frame
    av_frame_free(&frame);
}