// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL


#ifndef CHIAKI_LIB_WRAPPER_H
#define CHIAKI_LIB_WRAPPER_H

#include "discoveryservice.h"
#include "regist.h"

#ifdef __cplusplus
extern "C" {
#endif

CHIAKI_EXPORT ChiakiErrorCode discovery_ps(ChiakiDiscoveryServiceCb cb, ChiakiLog *log);


CHIAKI_EXPORT ChiakiRegist*  regist_ps(const char *host, const char *psn_id, const char *pin, const char *cpin, bool broadcast, int target, ChiakiRegistCb cb, ChiakiLog *log,void *cb_user);





#ifdef __cplusplus
}
#endif

#endif //CHIAKI_LIB_WRAPPER_H
