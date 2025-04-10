// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/chiakilibwrapper.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
ChiakiDiscoveryService service ;
ChiakiDiscoveryService service_ipv6 ;

CHIAKI_EXPORT ChiakiErrorCode discovery_ps(ChiakiDiscoveryServiceCb cb, ChiakiLog *log)
{
    ChiakiErrorCode err;  // 在函数开头定义 err

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
       
        CHIAKI_LOGE(log, "DiscoveryManager failed to init Discovery Service IPV4");
        return err;
    }
    else
    {
   
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
    }
    else
    {
    }

    return CHIAKI_ERR_SUCCESS;
}