#ifndef __KTAP_ERR_H__
#define __KTAP_ERR_H__

typedef enum {
#define ERRDEF(name, msg) \
	KP_ERR_##name, KP_ERR_##name##_ = KP_ERR_##name + sizeof(msg)-1,
#include "ktap_errmsg.h"
	KP_ERR__MAX
} ErrMsg;

#endif
