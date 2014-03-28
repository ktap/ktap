/*
 * ktap types definition.
 *
 * Copyright (C) 2012-2014 Jovi Zhangwei <jovi.zhangwei@gmail.com>.
 * Copyright (C) 2005-2014 Mike Pall.
 * Copyright (C) 1994-2008 Lua.org, PUC-Rio.
 */

#ifndef __KTAP_TYPES_H__
#define __KTAP_TYPES_H__

#ifdef __KERNEL__
#include <linux/perf_event.h>
#else
typedef char u8;
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
typedef int ptrdiff_t;
#endif

#include "../include/ktap_bc.h"

/* Various VM limits. */
#define KP_MAX_MEMPOOL_SIZE	10000	/* Max. mempool size(Kbytes). */
#define KP_MAX_STR	512		/* Max. string length. */
#define KP_MAX_STRNUM	9999		/* Max. string number. */

#define KP_MAX_STRTAB	(1<<26)		/* Max. string table size. */
#define KP_MAX_HBITS	26		/* Max. hash bits. */
#define KP_MAX_ABITS	28		/* Max. bits of array key. */
#define KP_MAX_ASIZE	((1<<(KP_MAX_ABITS-1))+1)  /* Max. array part size. */
#define KP_MAX_COLOSIZE	16		/* Max. elems for colocated array. */

#define KP_MAX_LINE	1000		/* Max. source code line number. */
#define KP_MAX_XLEVEL	200		/* Max. syntactic nesting level. */
#define KP_MAX_BCINS	(1<<26)		/* Max. # of bytecode instructions. */
#define KP_MAX_SLOTS	250		/* Max. # of slots in a ktap func. */
#define KP_MAX_LOCVAR	200		/* Max. # of local variables. */
#define KP_MAX_UPVAL	60		/* Max. # of upvalues. */

#define KP_MAX_CACHED_CFUNCTION	128	/* Max. cached global cfunction */

#define KP_MAX_STACK_DEPTH	50	/* Max. stack depth */

/*
 * The first argument type of kdebug.trace_by_id()
 * The value is a userspace memory pointer.
 * Maybe embed it into the trunk file in future.
 */
typedef struct ktap_eventdesc {
	int nr;  /* the number of events id */
	int *id_arr; /* events id array */
	char *filter;
} ktap_eventdesc_t;


/* ktap option for each script */
typedef struct ktap_option {
	char *trunk; /* __user */
	int trunk_len;
	int argc;
	char **argv; /* __user */
	int verbose;
	int trace_pid;
	int workload;
	int trace_cpu;
	int print_timestamp;
	int quiet;
	int dry_run;
} ktap_option_t;

/*
 * Ioctls that can be done on a ktap fd:
 * todo: use _IO macro in include/uapi/asm-generic/ioctl.h
 */
#define KTAP_CMD_IOC_VERSION		('$' + 0)
#define KTAP_CMD_IOC_RUN		('$' + 1)
#define KTAP_CMD_IOC_EXIT		('$' + 3)

#define KTAP_VERSION_MAJOR       "0"
#define KTAP_VERSION_MINOR       "4"

#define KTAP_VERSION    "ktap " KTAP_VERSION_MAJOR "." KTAP_VERSION_MINOR
#define KTAP_AUTHOR    "Jovi Zhangwei <jovi.zhangwei@gmail.com>"
#define KTAP_COPYRIGHT  KTAP_VERSION "  Copyright (C) 2012-2014, " KTAP_AUTHOR

#define MYINT(s)        (s[0] - '0')
#define VERSION         (MYINT(KTAP_VERSION_MAJOR) * 16 + MYINT(KTAP_VERSION_MINOR))

typedef long ktap_number;
typedef int ktap_instr_t;
typedef union ktap_obj ktap_obj_t;

struct ktap_state;
typedef int (*ktap_cfunction) (struct ktap_state *ks);

/* ktap_val_t is basic value type in ktap stack, for reference all objects */
typedef struct ktap_val {
	union {
		ktap_obj_t *gc;		/* collectable objects, str/tab/... */
		void *p;		/* light userdata */
		ktap_cfunction f;	/* light C functions */
		ktap_number n;		/* numbers */
		struct {
			uint16_t depth;	/* stack depth */
			uint16_t skip;	/* skip stack entries */
		} stack;
	} val;
	union {
		int type;		/* type for above val */
		const unsigned int *pcr;/* Overlaps PC for ktap frames.*/
	};
} ktap_val_t;

typedef ktap_val_t *StkId;

#define GCHeader ktap_obj_t *nextgc; u8 gct;

typedef struct ktap_str {
	GCHeader;
	u8 reserved;  /* Used by lexer for fast lookup of reserved words. */
	u8 extra;
	unsigned int hash;
	int len;  /* number of characters in string */
} ktap_str_t;

typedef struct ktap_upval {
	GCHeader;
	uint8_t closed;	/* Set if closed (i.e. uv->v == &uv->u.value). */
	uint8_t immutable;	/* Immutable value. */
	union {
		ktap_val_t tv; /* If closed: the value itself. */
		struct { /* If open: double linked list, anchored at thread. */
			struct ktap_upval *prev;
			struct ktap_upval *next;
		};
	};
	ktap_val_t *v;  /* Points to stack slot (open) or above (closed). */
} ktap_upval_t;


typedef struct ktap_func {
	GCHeader;
	u8 nupvalues;
	BCIns *pc;
	struct ktap_proto *p;
	struct ktap_upval *upvals[1];  /* list of upvalues */
} ktap_func_t;

typedef struct ktap_proto {
	GCHeader;
	uint8_t numparams;	/* Number of parameters. */
	uint8_t framesize;	/* Fixed frame size. */
	int sizebc;		/* Number of bytecode instructions. */
	ktap_obj_t *gclist;
	void *k;	/* Split constant array (points to the middle). */
	void *uv;	/* Upvalue list. local slot|0x8000 or parent uv idx. */
	int sizekgc;	/* Number of collectable constants. */
	int sizekn;	/* Number of lua_Number constants. */
	int sizept;	/* Total size including colocated arrays. */
	uint8_t sizeuv;	/* Number of upvalues. */
	uint8_t flags;	/* Miscellaneous flags (see below). */

	/* --- The following fields are for debugging/tracebacks only --- */
	ktap_str_t *chunkname;	/* Chunk name this function was defined in. */
	BCLine firstline;	/* First line of the function definition. */
	BCLine numline;	/* Number of lines for the function definition. */
	void *lineinfo;	/* Compressed map from bytecode ins. to source line. */
	void *uvinfo;	/* Upvalue names. */
	void *varinfo;	/* Names and compressed extents of local variables. */
} ktap_proto_t;

/* Flags for prototype. */
#define PROTO_CHILD		0x01	/* Has child prototypes. */
#define PROTO_VARARG		0x02	/* Vararg function. */
#define PROTO_FFI		0x04	/* Uses BC_KCDATA for FFI datatypes. */
#define PROTO_NOJIT		0x08	/* JIT disabled for this function. */
#define PROTO_ILOOP		0x10	/* Patched bytecode with ILOOP etc. */
/* Only used during parsing. */
#define PROTO_HAS_RETURN	0x20	/* Already emitted a return. */
#define PROTO_FIXUP_RETURN	0x40	/* Need to fixup emitted returns. */
/* Top bits used for counting created closures. */
#define PROTO_CLCOUNT		0x20	/* Base of saturating 3 bit counter. */
#define PROTO_CLC_BITS		3
#define PROTO_CLC_POLY		(3*PROTO_CLCOUNT)  /* Polymorphic threshold. */

#define PROTO_UV_LOCAL		0x8000	/* Upvalue for local slot. */
#define PROTO_UV_IMMUTABLE	0x4000	/* Immutable upvalue. */

#define proto_kgc(pt, idx)	(((ktap_obj_t *)(pt)->k)[idx])
#define proto_bc(pt)		((BCIns *)((char *)(pt) + sizeof(ktap_proto_t)))
#define proto_bcpos(pt, pc)	((BCPos)((pc) - proto_bc(pt)))
#define proto_uv(pt)		((uint16_t *)(pt)->uv)

#define proto_chunkname(pt)	((pt)->chunkname)
#define proto_lineinfo(pt)	((const void *)(pt)->lineinfo)
#define proto_uvinfo(pt)	((const uint8_t *)(pt)->uvinfo)
#define proto_varinfo(pt)	((const uint8_t *)(pt)->varinfo)


typedef struct ktap_node_t {
	ktap_val_t val; /* Value object. Must be first field. */
	ktap_val_t key; /* Key object. */
	struct ktap_node_t *next;  /* hash chain */
} ktap_node_t;

/* ktap_tab */
typedef struct ktap_tab {
	GCHeader;
#ifdef __KERNEL__
	arch_spinlock_t lock;
#endif
	ktap_val_t *array;    /* Array part. */
	ktap_node_t *node;    /* Hash part. */
	ktap_node_t *freetop; /* any free position is before this position */

	uint32_t asize;		/* Size of array part (keys [0, asize-1]). */
	uint32_t hmask;		/* log2 of size of `node' array */

	uint32_t hnum;		/* number of all nodes */
} ktap_tab_t;

typedef struct ktap_stats {
	int mem_allocated;
	int nr_mem_allocate;
	int events_hits;
	int events_missed;
} ktap_stats_t;

#define KTAP_STATS(ks)	this_cpu_ptr(G(ks)->stats)


#define KTAP_RUNNING	0 /* normal running state */
#define KTAP_TRACE_END	1 /* running in trace_end function */
#define KTAP_EXIT	2 /* normal exit, set when call exit() */
#define KTAP_ERROR	3 /* error state, called by kp_error */

typedef struct ktap_global_state {
	void *mempool;		/* string memory pool */
	void *mp_freepos;	/* free position in memory pool */
	int mp_size;		/* memory pool size */
#ifdef __KERNEL__
	arch_spinlock_t mp_lock;/* mempool lock */
#endif

	ktap_str_t **strhash;	/* String hash table (hash chain anchors). */
	int strmask;		/* String hash mask (size of hash table-1). */
	int strnum;		/* Number of strings in hash table. */
#ifdef __KERNEL__
	arch_spinlock_t str_lock; /* string operation lock */
#endif

	ktap_val_t registry;
	ktap_tab_t *gtab;	/* global table contains cfunction and args */
	ktap_obj_t *allgc; /* list of all collectable objects */
	ktap_upval_t uvhead; /* head of list of all open upvalues */

	struct ktap_state *mainthread; /*main state */
	int state; /* status of ktapvm, KTAP_RUNNING, KTAP_TRACE_END, etc */
#ifdef __KERNEL__
	/* reserved global percpu data */
	void __percpu *percpu_state[PERF_NR_CONTEXTS];
	void __percpu *percpu_print_buffer[PERF_NR_CONTEXTS];
	void __percpu *percpu_temp_buffer[PERF_NR_CONTEXTS];

	/* for recursion tracing check */
	int __percpu *recursion_context[PERF_NR_CONTEXTS];

	ktap_option_t *parm; /* ktap options */
	pid_t trace_pid;
	struct task_struct *trace_task;
	cpumask_var_t cpumask;
	struct ring_buffer *buffer;
	struct dentry *trace_pipe_dentry;
	struct task_struct *task;
	int trace_enabled;
	int wait_user; /* flag to indicat waiting user consume content */

	struct list_head timers; /* timer list */
	struct ktap_stats __percpu *stats; /* memory allocation stats */
	struct list_head events_head; /* probe event list */

	ktap_func_t *trace_end_closure; /* trace_end closure */

	/* C function table for fast call */
	int nr_builtin_cfunction;
	ktap_cfunction gfunc_tbl[KP_MAX_CACHED_CFUNCTION];
#endif
} ktap_global_state_t;


typedef struct ktap_state {
	ktap_global_state_t *g;	/* global state */
	int stop;		/* don't enter tracing handler if stop is 1 */
	StkId top;		/* stack top */
	StkId func;		/* execute light C function */
	StkId stack_last;	/* last stack pointer */
	StkId stack;		/* ktap stack, percpu pre-reserved */
	ktap_upval_t *openupval;/* opened upvals list */

#ifdef __KERNEL__
	/* current fired event which allocated on stack */
	struct ktap_event_data *current_event;
#endif
} ktap_state_t;

#define G(ks)   (ks->g)

/*
 * Union of all collectable objects
 */
union ktap_obj {
	struct { GCHeader } gch;
	struct ktap_str ts;
	struct ktap_func fn;
	struct ktap_tab h;
	struct ktap_proto pt;
	struct ktap_upval uv;
	struct ktap_state th;  /* thread */
};

#define gch(o)			(&(o)->gch)

/* macros to convert a ktap_obj_t into a specific value */
#define gco2ts(o)		(&((o)->ts))
#define gco2uv(o)		(&((o)->uv))
#define obj2gco(v)		((ktap_obj_t *)(v))

/* predefined values in the registry */
#define KTAP_RIDX_GLOBALS	1
#define KTAP_RIDX_LAST		KTAP_RIDX_GLOBALS

/* ktap object types */
#define KTAP_TNIL		(~0u)
#define KTAP_TFALSE		(~1u)
#define KTAP_TTRUE		(~2u)
#define KTAP_TNUM		(~3u)
#define KTAP_TLIGHTUD		(~4u)
#define KTAP_TSTR		(~5u)
#define KTAP_TUPVAL		(~6u)
#define KTAP_TPROTO		(~7u)
#define KTAP_TFUNC		(~8u)
#define KTAP_TCFUNC		(~9u)
#define KTAP_TCDATA		(~10u)
#define KTAP_TTAB		(~11u)
#define KTAP_TUDATA		(~12u)

/* Specfic types */
#define KTAP_TEVENTSTR		(~13u) /* argstr */
#define KTAP_TKSTACK		(~14u) /* stack(), not intern to string yet */
#define KTAP_TKIP		(~15u) /* kernel function ip addres */
#define KTAP_TUIP		(~16u) /* userspace function ip addres */

/* This is just the canonical number type used in some places. */
#define KTAP_TNUMX		(~17u)


#define itype(o)		((o)->type)
#define setitype(o, t)		((o)->type = (t))

#define val_(o)			((o)->val)
#define gcvalue(o)		(val_(o).gc)

#define nvalue(o)		(val_(o).n)
#define boolvalue(o)		(KTAP_TFALSE - (o)->type)
#define hvalue(o)		(&val_(o).gc->h)
#define phvalue(o)		(&val_(o).gc->ph)
#define clvalue(o)		(&val_(o).gc->fn)
#define ptvalue(o)		(&val_(o).gc->pt)

#define getstr(ts)		(const char *)((ts) + 1)
#define rawtsvalue(o)		(&val_(o).gc->ts)
#define svalue(o)		getstr(rawtsvalue(o))

#define pvalue(o)		(&val_(o).p)
#define fvalue(o)		(val_(o).f)

#define is_nil(o)		(itype(o) == KTAP_TNIL)
#define is_false(o)		(itype(o) == KTAP_TFALSE)
#define is_true(o)		(itype(o) == KTAP_TTRUE)
#define is_bool(o)		(is_false(o) || is_true(o))
#define is_string(o)		(itype(o) == KTAP_TSTR)
#define is_number(o)		(itype(o) == KTAP_TNUM)
#define is_table(o)		(itype(o) == KTAP_TTAB)
#define is_proto(o)		(itype(o) == KTAP_TPROTO)
#define is_function(o)		(itype(o) == KTAP_TFUNC)
#define is_cfunc(o)		(itype(o) == KTAP_TCFUNC)
#define is_eventstr(o)		(itype(o) == KTAP_TEVENTSTR)
#define is_kip(o)		(itype(o) == KTAP_TKIP)

#define set_nil(o)		((o)->type = KTAP_TNIL)
#define set_bool(o, x)		((o)->type = KTAP_TFALSE-(uint32_t)(x))

static inline void set_number(ktap_val_t *o, ktap_number n)
{
	setitype(o, KTAP_TNUM);
	o->val.n = n;
}

static inline void set_string(ktap_val_t *o, const ktap_str_t *str)
{
	setitype(o, KTAP_TSTR);
	o->val.gc = (ktap_obj_t *)str;
}

static inline void set_table(ktap_val_t *o, ktap_tab_t *tab)
{
	setitype(o, KTAP_TTAB);
	o->val.gc = (ktap_obj_t *)tab;
}

static inline void set_proto(ktap_val_t *o, ktap_proto_t *pt)
{
	setitype(o, KTAP_TPROTO);
	o->val.gc = (ktap_obj_t *)pt;
}

static inline void set_kstack(ktap_val_t *o, uint16_t depth, uint16_t skip)
{
	setitype(o, KTAP_TKSTACK);
	o->val.stack.depth = depth;
	o->val.stack.skip = skip;
}

static inline void set_func(ktap_val_t *o, ktap_func_t *fn)
{
	setitype(o, KTAP_TFUNC);
	o->val.gc = (ktap_obj_t *)fn;
}

static inline void set_cfunc(ktap_val_t *o, ktap_cfunction fn)
{
	setitype(o, KTAP_TCFUNC);
	o->val.f = fn;
}

static inline void set_eventstr(ktap_val_t *o)
{
	setitype(o, KTAP_TEVENTSTR);
}

static inline void set_ip(ktap_val_t *o, unsigned long addr)
{
	setitype(o, KTAP_TKIP);
	o->val.n = addr;
}


#define set_obj(o1, o2)		{ *(o1) = *(o2); }

#define incr_top(ks)		{ks->top++;}

/*
 * KTAP_QL describes how error messages quote program elements.
 * CHANGE it if you want a different appearance.
 */
#define KTAP_QL(x)      "'" x "'"
#define KTAP_QS         KTAP_QL("%s")

#endif /* __KTAP_TYPES_H__ */

