/* dpvm: error; T15.417-T19.641; $DVS:time$ */

#ifndef DPVM_ERROR
#define DPVM_ERROR

#define int enum {
#define _Z 0};
#include "../dpvm/common/errors.dpvmh"
#undef _Z
#undef int

#define DPVM_ERROR_IS_RETRYABLE(e) ((e) == DPVM_ERROR_FINISHED || (e) == DPVM_ERROR_TERMINATED \
		|| (e) == DPVM_ERROR_NO_MEMORY || (e) == DPVM_ERROR_CREATE_OBJECT || (e) == DPVM_ERROR_STORE_ERROR \
		|| (e) == DPVM_ERROR_MAP || (e) == DPVM_ERROR_IO)

#endif
