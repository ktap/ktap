#include "../../include/ktap_ffi.h"
#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#include <stddef.h>
#endif

#define CTYPE_MODE_HELPER(name, type)	\
struct _##name##_align {		\
	type t1;			\
	char c;				\
	type t2;			\
};

#define CTYPE_MODE(name)				\
{							\
	offsetof(struct _##name##_align, c),		\
	offsetof(struct _##name##_align, t2) -		\
		offsetof(struct _##name##_align, c),	\
	#name					\
}

#define CTYPE_MODE_NAME(name) _##name##_mode

/* ffi_ctype_mode should be corresponded to ffi_ctype */
CTYPE_MODE_HELPER(uint8, uint8_t);
CTYPE_MODE_HELPER(int8, int8_t);
CTYPE_MODE_HELPER(uint16, uint16_t);
CTYPE_MODE_HELPER(int16, int16_t);
CTYPE_MODE_HELPER(uint32, uint32_t);
CTYPE_MODE_HELPER(int32, int32_t);
CTYPE_MODE_HELPER(uint64, uint64_t);
CTYPE_MODE_HELPER(int64, int64_t);
CTYPE_MODE_HELPER(pointer, void*);

const ffi_mode ffi_type_modes[NUM_FFI_TYPE+1] = {
	{0, 1, "void"},
	CTYPE_MODE(uint8),
	CTYPE_MODE(int8),
	CTYPE_MODE(uint16),
	CTYPE_MODE(int16),
	CTYPE_MODE(uint32),
	CTYPE_MODE(int32),
	CTYPE_MODE(uint64),
	CTYPE_MODE(int64),
	CTYPE_MODE(pointer),
	{0, 1, "function"},
	{0, 1, "struct"},
	{0, 1, "unknown"},
};
