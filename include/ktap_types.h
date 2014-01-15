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
#endif

/*
 * The first argument type of kdebug.probe_by_id()
 * The value is a userspace memory pointer.
 * Maybe embed it info trunk file in future.
 */
typedef struct ktap_eventdef_info {
	int nr;  /* the number of events id */
	int *id_arr; /* events id array */
	char *filter;
} ktap_eventdef_info;

typedef struct ktap_parm {
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
} ktap_parm;

/*
 * Ioctls that can be done on a ktap fd:
 * todo: use _IO macro in include/uapi/asm-generic/ioctl.h
 */
#define KTAP_CMD_IOC_VERSION		('$' + 0)
#define KTAP_CMD_IOC_RUN		('$' + 1)
#define KTAP_CMD_IOC_EXIT		('$' + 3)

#define KTAP_ENV	"_ENV"

#define KTAP_VERSION_MAJOR       "0"
#define KTAP_VERSION_MINOR       "4"

#define KTAP_VERSION    "ktap " KTAP_VERSION_MAJOR "." KTAP_VERSION_MINOR
#define KTAP_AUTHOR    "Jovi Zhangwei <jovi.zhangwei@gmail.com>"
#define KTAP_COPYRIGHT  KTAP_VERSION "  Copyright (C) 2012-2013, " KTAP_AUTHOR

#define MYINT(s)        (s[0] - '0')
#define VERSION         (MYINT(KTAP_VERSION_MAJOR) * 16 + MYINT(KTAP_VERSION_MINOR))
#define FORMAT          0 /* this is the official format */

#define KTAP_SIGNATURE  "\033ktap"

/* data to catch conversion errors */
#define KTAPC_TAIL      "\x19\x93\r\n\x1a\n"

/* size in bytes of header of binary files */
#define KTAPC_HEADERSIZE	(sizeof(KTAP_SIGNATURE) - sizeof(char) + 2 + \
				 6 + sizeof(KTAPC_TAIL) - sizeof(char))

typedef long ktap_number;
#define kp_number2int(i, n)	((i) = (int)(n))

typedef int ktap_instruction;

typedef union ktap_gcobject ktap_gcobject;

#define CommonHeader ktap_gcobject *next; u8 tt;

typedef union ktap_string {
	int dummy;  /* ensures maximum alignment for strings */
	struct {
		CommonHeader;
		u8 extra;  /* reserved words for short strings; "has hash" for longs */
		unsigned int hash;
		size_t len;  /* number of characters in string */
	} tsv;
	/* short string is stored here, just after tsv */
} ktap_string;


struct ktap_state;
typedef int (*ktap_cfunction) (struct ktap_state *ks);

typedef struct ktap_value {
	union {
		ktap_gcobject *gc;    /* collectable objects */
		void *p;         /* light userdata */
		int b;           /* booleans */
		ktap_cfunction f; /* light C functions */
		ktap_number n;         /* numbers */
	} val;
	int type;
} ktap_value;

typedef ktap_value * StkId;


/*
 * Description of an upvalue for function prototypes
 */
typedef struct ktap_upvaldesc {
	ktap_string *name;  /* upvalue name (for debug information) */
	u8 instack;  /* whether it is in stack */
	u8 idx;  /* index of upvalue (in stack or in outer function's list) */
} ktap_upvaldesc;

/*
 * Description of a local variable for function prototypes
 * (used for debug information)
 */
typedef struct ktap_locvar {
	ktap_string *varname;
	int startpc;  /* first point where variable is active */
	int endpc;    /* first point where variable is dead */
} ktap_locvar;


typedef struct ktap_upval {
	CommonHeader;
	ktap_value *v;  /* points to stack or to its own value */
	union {
		ktap_value value;  /* the value (when closed) */
		struct {  /* double linked list (when open) */
			struct ktap_upval *prev;
			struct ktap_upval *next;
		} l;
	} u;
} ktap_upval;


#define KTAP_MAX_STACK_ENTRIES 100

typedef struct ktap_btrace {
	CommonHeader;
	unsigned int nr_entries;
	/* entries stored in here, after nr_entries */
} ktap_btrace;

typedef struct ktap_closure {
	CommonHeader;
	u8 nupvalues;
	struct ktap_proto *p;
	struct ktap_upval *upvals[1];  /* list of upvalues */
} ktap_closure;

typedef struct ktap_proto {
	CommonHeader;
	ktap_value *k;  /* constants used by the function */
	ktap_instruction *code;
	struct ktap_proto **p;  /* functions defined inside the function */
	int *lineinfo;  /* map from opcodes to source lines (debug information) */
	struct ktap_locvar *locvars;  /* information about local variables (debug information) */
	struct ktap_upvaldesc *upvalues;  /* upvalue information */
	ktap_closure *cache;  /* last created closure with this prototype */
	ktap_string  *source;  /* used for debug information */
	int sizeupvalues;  /* size of 'upvalues' */
	int sizek;  /* size of `k' */
	int sizecode;
	int sizelineinfo;
	int sizep;  /* size of `p' */
	int sizelocvars;
	int linedefined;
	int lastlinedefined;
	u8 numparams;  /* number of fixed parameters */
	u8 is_vararg;
	u8 maxstacksize;  /* maximum stack used by this function */
} ktap_proto;


/*
 * information about a call
 */
typedef struct ktap_callinfo {
	StkId func;  /* function index in the stack */
	StkId top;  /* top for this function */
	struct ktap_callinfo *prev, *next;  /* dynamic call link */
	short nresults;  /* expected number of results from this function */
	u8 callstatus;
	int extra;
	union {
		struct {  /* only for ktap functions */
			StkId base;  /* base for this function */
			const unsigned int *savedpc;
		} l;
		struct {  /* only for C functions */
			int ctx;  /* context info. in case of yields */
			u8 status;
		} c;
	} u;
} ktap_callinfo;


/*
 * ktap_tab
 */
typedef struct ktap_tkey {
	struct ktap_tnode *next;  /* for chaining */
	ktap_value tvk;
} ktap_tkey;


typedef struct ktap_tnode {
	ktap_value i_val;
	ktap_tkey i_key;
} ktap_tnode;


typedef struct ktap_stat_data {
	int count;
	int sum;
	int min, max;
} ktap_stat_data;


typedef struct ktap_tab {
	CommonHeader;
#ifdef __KERNEL__
	arch_spinlock_t lock;
#endif
	u8 lsizenode;  /* log2 of size of `node' array */
	int sizearray;  /* size of `array' array */
	ktap_value *array;  /* array part */
	ktap_tnode *node;
	ktap_tnode *lastfree;  /* any free position is before this position */

	int with_stats;  /* for aggregation table: ptable */
	ktap_stat_data *sd_arr;
	ktap_stat_data *sd_rec;

	ktap_tnode *sorted;  /* sorted table, with linked node list */
	ktap_tnode *sort_head;

	ktap_gcobject *gclist;
} ktap_tab;

#define lmod(s,size)	((int)((s) & ((size)-1)))

/* parallel table */
typedef struct ktap_ptab {
	CommonHeader;
	ktap_tab **tbl; /* percpu table */
	ktap_tab *agg;
} ktap_ptab;

typedef struct ktap_stringtable {
	ktap_gcobject **hash;
	int nuse;
	int size;
} ktap_stringtable;

#ifdef CONFIG_KTAP_FFI
typedef int csymbol_id;
typedef struct csymbol csymbol;

/* global ffi state maintained in each ktap vm instance */
typedef struct ffi_state {
	ktap_tab *ctable;
	int csym_nr;
	csymbol *csym_arr;
} ffi_state;

/* instance of csymbol */
typedef struct ktap_cdata {
	CommonHeader;
	csymbol_id id;
	union {
		uint64_t i;
		struct {
			void *addr;
			int nmemb;	/* number of memory block */
		} p;			/* pointer data */
		void *st;		/* struct member data */
	} u;
} ktap_cdata;
#endif

typedef struct ktap_stats {
	int mem_allocated;
	int nr_mem_allocate;
	int nr_mem_free;
	int events_hits;
	int events_missed;
} ktap_stats;

#define KTAP_STATS(ks)	this_cpu_ptr(G(ks)->stats)

enum {
	KTAP_PERCPU_DATA_STATE,
	KTAP_PERCPU_DATA_STACK,
	KTAP_PERCPU_DATA_BUFFER,
	KTAP_PERCPU_DATA_BUFFER2,
	KTAP_PERCPU_DATA_BTRACE,

	KTAP_PERCPU_DATA_MAX
};

typedef struct ktap_global_state {
	ktap_stringtable strt;  /* hash table for strings */
	ktap_value registry;
	unsigned int seed; /* randonized seed for hashes */

	ktap_gcobject *allgc; /* list of all collectable objects */

	ktap_upval uvhead; /* head of double-linked list of all open upvalues */

	struct ktap_state *mainthread;
#ifdef __KERNEL__
	/* global percpu data(like stack) */
	void __percpu *pcpu_data[KTAP_PERCPU_DATA_MAX][PERF_NR_CONTEXTS];

	int __percpu *recursion_context[PERF_NR_CONTEXTS];

	arch_spinlock_t str_lock; /* string operation lock */

	ktap_parm *parm;
	pid_t trace_pid;
	struct task_struct *trace_task;
	cpumask_var_t cpumask;
	struct ring_buffer *buffer;
	struct dentry *trace_pipe_dentry;
	int nr_builtin_cfunction;
	ktap_value *cfunction_tbl;
	struct task_struct *task;
	int trace_enabled;
	struct list_head timers;
	struct list_head probe_events_head;
	int exit;
	int wait_user;
	ktap_closure *trace_end_closure;
	struct ktap_stats __percpu *stats;
	struct kmem_cache *pevent_cache;
#ifdef CONFIG_KTAP_FFI
	ffi_state  ffis;
#endif
#endif
	int error;
} ktap_global_state;

typedef struct ktap_state {
	CommonHeader;
	ktap_global_state *g;
	int stop;
	StkId top;
	ktap_callinfo *ci;
	const unsigned long *oldpc;
	StkId stack_last;
	StkId stack;
	ktap_gcobject *openupval;
	ktap_callinfo baseci;

	/* list of temp collectable objects, free when thread exit */
	ktap_gcobject *gclist;

#ifdef __KERNEL__
	struct ktap_event *current_event;
#endif
} ktap_state;

#define G(ks)   (ks->g)

typedef struct ktap_rawobj {
	CommonHeader;
	void *v;
} ktap_rawobj;

typedef struct gcheader {
	CommonHeader;
} gcheader;

/*
 * Union of all collectable objects
 */
union ktap_gcobject {
	gcheader gch;  /* common header */
	union ktap_string ts;
	struct ktap_closure cl;
	struct ktap_tab h;
	struct ktap_ptab ph;
	struct ktap_proto p;
	struct ktap_upval uv;
	struct ktap_state th;  /* thread */
 	struct ktap_btrace bt;  /* backtrace object */
	struct ktap_rawobj rawobj;
#ifdef CONFIG_KTAP_FFI
	struct ktap_cdata cd;
#endif
};

#define gch(o)			(&(o)->gch)

/* macros to convert a GCObject into a specific value */
#define rawgco2ts(o)		(&((o)->ts))

#define gco2ts(o)		(&rawgco2ts(o)->tsv)
#define gco2uv(o)		(&((o)->uv))
#define obj2gco(v)		((ktap_gcobject *)(v))
#define check_exp(c, e)		(e)


/* predefined values in the registry */
#define KTAP_RIDX_MAINTHREAD	1
#define KTAP_RIDX_GLOBALS	2
#define KTAP_RIDX_LAST		KTAP_RIDX_GLOBALS

#define KTAP_TYPE_NIL		0
#define KTAP_TYPE_BOOLEAN	1
#define KTAP_TYPE_LIGHTUSERDATA	2
#define KTAP_TYPE_NUMBER	3
#define KTAP_TYPE_STRING	4
#define KTAP_TYPE_SHRSTR	(KTAP_TYPE_STRING | (0 << 4))/* short strings */
#define KTAP_TYPE_LNGSTR	(KTAP_TYPE_STRING | (1 << 4))/* long strings */
#define KTAP_TYPE_TABLE		5
#define KTAP_TYPE_FUNCTION	6
#define KTAP_TYPE_CLOSURE	(KTAP_TYPE_FUNCTION | (0 << 4))  /* closure */
#define KTAP_TYPE_CFUNCTION	(KTAP_TYPE_FUNCTION | (1 << 4))  /* light C function */
#define KTAP_TYPE_THREAD	7
#define KTAP_TYPE_PROTO		8
#define KTAP_TYPE_UPVAL		9
#define KTAP_TYPE_EVENT		10
#define KTAP_TYPE_BTRACE	11
#define KTAP_TYPE_PTABLE	12
#define KTAP_TYPE_STATDATA	13
#define KTAP_TYPE_CDATA		14
#define KTAP_TYPE_RAW		15
/*
 * type number is ok so far, but it may collide later between
 * 16+ and | (1 << 4), so be careful on this.
 */

#define ttype(o)		((o->type) & 0x3F)
#define settype(obj, t)		((obj)->type = (t))

/* raw type tag of a TValue */
#define rttype(o)		((o)->type)

/* tag with no variants (bits 0-3) */
#define novariant(x)		((x) & 0x0F)

/* type tag of a TValue with no variants (bits 0-3) */
#define ttypenv(o)		(novariant(rttype(o)))

#define val_(o)			((o)->val)
#define gcvalue(o)		(val_(o).gc)

#define bvalue(o)		(val_(o).b)
#define nvalue(o)		(val_(o).n)
#define hvalue(o)		(&val_(o).gc->h)
#define phvalue(o)		(&val_(o).gc->ph)
#define clvalue(o)		(&val_(o).gc->cl)

#define getstr(ts)		(const char *)((ts) + 1)
#define eqshrstr(a, b)		((a) == (b))
#define rawtsvalue(o)		(&val_(o).gc->ts)
#define svalue(o)		getstr(rawtsvalue(o))

#define pvalue(o)		(&val_(o).p)
#define sdvalue(o)		((ktap_stat_data *)val_(o).p)
#define fvalue(o)		(val_(o).f)
#define evalue(o)		(val_(o).p)
#define btvalue(o)		(&val_(o).gc->bt)
#define cdvalue(o)		(&val_(o).gc->cd)

#define is_nil(o)		((o)->type == KTAP_TYPE_NIL)
#define is_boolean(o)		((o)->type == KTAP_TYPE_BOOLEAN)
#define is_false(o)		(is_nil(o) || (is_boolean(o) && bvalue(o) == 0))
#define is_shrstring(o)		((o)->type == KTAP_TYPE_SHRSTR)
#define is_string(o)		(((o)->type & 0x0F) == KTAP_TYPE_STRING)
#define is_number(o)		((o)->type == KTAP_TYPE_NUMBER)
#define is_table(o)		((o)->type == KTAP_TYPE_TABLE)
#define is_ptable(o)		((o)->type == KTAP_TYPE_PTABLE)
#define is_statdata(o)		((o)->type == KTAP_TYPE_STATDATA)
#define is_event(o)		((o)->type == KTAP_TYPE_EVENT)
#define is_btrace(o)		((o)->type == KTAP_TYPE_BTRACE)
#define is_needclone(o)		is_btrace(o)
#ifdef CONFIG_KTAP_FFI
#define is_cdata(o)		((o)->type == KTAP_TYPE_CDATA)
#endif


#define set_nil(obj) \
	{ ktap_value *io = (obj); io->val.n = 0; settype(io, KTAP_TYPE_NIL); }

#define set_boolean(obj, x) \
	{ ktap_value *io = (obj); io->val.b = (x); settype(io, KTAP_TYPE_BOOLEAN); }

#define set_number(obj, x) \
	{ ktap_value *io = (obj); io->val.n = (x); settype(io, KTAP_TYPE_NUMBER); }

#define set_statdata(obj, x) \
	{ ktap_value *io = (obj); \
	  io->val.p = (x); settype(io, KTAP_TYPE_STATDATA); }

#define set_string(obj, x) \
	{ ktap_value *io = (obj); \
	  ktap_string *x_ = (x); \
	  io->val.gc = (ktap_gcobject *)x_; settype(io, x_->tsv.tt); }

#define set_closure(obj, x) \
	{ ktap_value *io = (obj); \
	  io->val.gc = (ktap_gcobject *)x; settype(io, KTAP_TYPE_CLOSURE); }

#define set_cfunction(obj, x) \
	{ ktap_value *io = (obj); val_(io).f = (x); settype(io, KTAP_TYPE_CFUNCTION); }

#define set_table(obj, x) \
	{ ktap_value *io = (obj); \
	  val_(io).gc = (ktap_gcobject *)(x); settype(io, KTAP_TYPE_TABLE); }

#define set_ptable(obj, x) \
	{ ktap_value *io = (obj); \
	  val_(io).gc = (ktap_gcobject *)(x); settype(io, KTAP_TYPE_PTABLE); }

#define set_thread(obj, x) \
	{ ktap_value *io = (obj); \
	  val_(io).gc = (ktap_gcobject *)(x); settype(io, KTAP_TYPE_THREAD); }

#define set_event(obj, x) \
	{ ktap_value *io = (obj); val_(io).p = (x); settype(io, KTAP_TYPE_EVENT); }

#define set_btrace(obj, x) \
	{ ktap_value *io = (obj); \
	  val_(io).gc = (ktap_gcobject *)(x); settype(io, KTAP_TYPE_BTRACE); }

#ifdef CONFIG_KTAP_FFI
#define set_cdata(obj, x) \
	{ ktap_value *io=(obj); \
	  val_(io).gc = (ktap_gcobject *)(x); settype(io, KTAP_TYPE_CDATA); }
#endif

#define set_obj(obj1, obj2) \
        { const ktap_value *io2 = (obj2); ktap_value *io1 = (obj1); \
          io1->val = io2->val; io1->type = io2->type; }

#define rawequalobj(t1, t2) \
	(((t1)->type == (t2)->type) && kp_obj_equal(NULL, t1, t2))

#define incr_top(ks) {ks->top++;}

#define NUMADD(a, b)    ((a) + (b))
#define NUMSUB(a, b)    ((a) - (b))
#define NUMMUL(a, b)    ((a) * (b))
#define NUMDIV(a, b)    ((a) / (b))
#define NUMUNM(a)       (-(a))
#define NUMEQ(a, b)     ((a) == (b))
#define NUMLT(a, b)     ((a) < (b))
#define NUMLE(a, b)     ((a) <= (b))
#define NUMISNAN(a)     (!NUMEQ((a), (a)))

/* todo: floor and pow in kernel */
#define NUMMOD(a, b)    ((a) % (b))
#define NUMPOW(a, b)    (pow(a, b))

/*
 * KTAP_QL describes how error messages quote program elements.
 * CHANGE it if you want a different appearance.
 */
#define KTAP_QL(x)      "'" x "'"
#define KTAP_QS         KTAP_QL("%s")

#define STRINGIFY(type) #type

/*
 * make header for precompiled chunks
 * if you change the code below be sure to update load_header and FORMAT above
 * and KTAPC_HEADERSIZE in ktap_types.h
 */
static inline void kp_header(u8 *h)
{
	int x = 1;

	memcpy(h, KTAP_SIGNATURE, sizeof(KTAP_SIGNATURE) - sizeof(char));
	h += sizeof(KTAP_SIGNATURE) - sizeof(char);
	*h++ = (u8)VERSION;
	*h++ = (u8)FORMAT;
	*h++ = (u8)(*(char*)&x);                    /* endianness */
	*h++ = (u8)(sizeof(int));
	*h++ = (u8)(sizeof(size_t));
	*h++ = (u8)(sizeof(ktap_instruction));
	*h++ = (u8)(sizeof(ktap_number));
	*h++ = (u8)(((ktap_number)0.5) == 0); /* is ktap_number integral? */
	memcpy(h, KTAPC_TAIL, sizeof(KTAPC_TAIL) - sizeof(char));
}

#endif /* __KTAP_TYPES_H__ */

