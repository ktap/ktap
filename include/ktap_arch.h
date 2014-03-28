#ifndef __KTAP_ARCH__
#define __KTAP_ARCH__

#ifdef __KERNEL__
#include <linux/types.h>
#include <asm/byteorder.h>

#if defined(__LITTLE_ENDIAN)
#define KP_LE				1
#define KP_BE				0
#define KP_ENDIAN_SELECT(le, be)        le
#elif defined(__BIG_ENDIAN)
#define KP_LE				0
#define KP_BE				1
#define KP_ENDIAN_SELECT(le, be)        be
#endif

#else /* __KERNEL__ */

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define KP_LE				1
#define KP_BE				0
#define KP_ENDIAN_SELECT(le, be)        le
#elif __BYTE_ORDER == __BIG_ENDIAN
#define KP_LE				0
#define KP_BE				1
#define KP_ENDIAN_SELECT(le, be)        be
#else
#error "could not determine byte order"
#endif

#endif
#endif
