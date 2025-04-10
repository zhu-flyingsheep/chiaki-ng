// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL


#ifndef CHIAKI_LIB_WRAPPER_H
#define CHIAKI_LIB_WRAPPER_H

#include "discoveryservice.h"

#ifdef __cplusplus
extern "C" {
#endif

CHIAKI_EXPORT ChiakiErrorCode discovery_ps(ChiakiDiscoveryServiceCb cb, ChiakiLog *log);

#ifdef __cplusplus
}
#endif

#endif //CHIAKI_LIB_WRAPPER_H
