#ifndef __KTAP_TYPES_H__
#define __KTAP_TYPES_H__

/* opcode is copied from lua initially */

#ifdef __KERNEL__
#include <linux/module.h>
#include <linux/slab.h>
#else
typedef char u8;
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#endif

struct ktap_user_parm {
	char *trunk;
	int trunk_len;
	char *argstr;
	int arglen;
};

enum {
	KTAP_CMD_VERSION,
	KTAP_CMD_RUN,
	KTAP_CMD_USER_COMPLETE = 50,
	KTAP_CMD_NO_GC,
	KTAP_CMD_GC_FULL,
	KTAP_CMD_GC_MINOR,
	KTAP_CMD_GC_THREASH,
	KTAP_CMD_GC_NOW,
	KTAP_CMD_EVENTS_LIST,
	KTAP_CMD_STRICT_MODE,
	KTAP_CMD_MEM_LIMIT,
	KTAP_CMD_SET_STACKSIZE,
	KTAP_CMD_DEBUG,
	KTAP_CMD_EXIT
};


#define KTAP_ENV	"_ENV"

#define KTAP_VERSION_MAJOR       "0"
#define KTAP_VERSION_MINOR       "1"

#define KTAP_VERSION    "ktap " KTAP_VERSION_MAJOR "." KTAP_VERSION_MINOR
#define KTAP_COPYRIGHT  KTAP_VERSION "  Copyright (C) 2012-2013, Jovi Zhang"
#define KTAP_AUTHORS    "Jovi Zhang (bookjovi@gmail.com)"

#define MYINT(s)        (s[0] - '0')
#define VERSION         (MYINT(KTAP_VERSION_MAJOR) * 16 + MYINT(KTAP_VERSION_MINOR))
#define FORMAT          0 /* this is the official format */

#define KTAP_SIGNATURE  "\033ktap"

/* data to catch conversion errors */
#define KTAPC_TAIL      "\x19\x93\r\n\x1a\n"

/* size in bytes of header of binary files */
#define KTAPC_HEADERSIZE	(sizeof(KTAP_SIGNATURE) - sizeof(char) + 2 + 6 + \
				 sizeof(KTAPC_TAIL) - sizeof(char))


typedef int Instruction;

typedef union Gcobject Gcobject;

#define CommonHeader Gcobject *next; u8 tt; u8 marked;

struct ktap_State;
typedef int (*ktap_cfunction) (struct ktap_State *ks);

typedef union Tstring {
	int dummy;  /* ensures maximum alignment for strings */
	struct {
		CommonHeader;
		u8 extra;  /* reserved words for short strings; "has hash" for longs */
		unsigned int hash;
		size_t len;  /* number of characters in string */
	} tsv;
} Tstring;

#define getstr(ts)	(const char *)((ts) + 1)
#define eqshrstr(a,b)	((a) == (b))

#define svalue(o)       getstr(rawtsvalue(o))


union value {
	Gcobject *gc;    /* collectable objects */
	void *p;         /* light userdata */
	int b;           /* booleans */
	ktap_cfunction f; /* light C functions */
	int n;         /* numbers */
};


typedef struct Tvalue {
	union value val;
	int type;
} Tvalue;

typedef Tvalue * StkId;



typedef union Udata {
	struct {
		CommonHeader;
		size_t len;  /* number of bytes */
	} uv;
} Udata;

/*
 * Description of an upvalue for function prototypes
 */
typedef struct Upvaldesc {
	Tstring *name;  /* upvalue name (for debug information) */
	u8 instack;  /* whether it is in stack */
	u8 idx;  /* index of upvalue (in stack or in outer function's list) */
} Upvaldesc;

/*
 * Description of a local variable for function prototypes
 * (used for debug information)
 */
typedef struct LocVar {
	Tstring *varname;
	int startpc;  /* first point where variable is active */
	int endpc;    /* first point where variable is dead */
} LocVar;


typedef struct Upval {
	CommonHeader;
	Tvalue *v;  /* points to stack or to its own value */
	union {
		Tvalue value;  /* the value (when closed) */
		struct {  /* double linked list (when open) */
			struct Upval *prev;
			struct Upval *next;
		} l;
	} u;
} Upval;


#define ClosureHeader \
	CommonHeader; u8 nupvalues; Gcobject *gclist

typedef struct CClosure {
	ClosureHeader;
	ktap_cfunction f;
	Tvalue upvalue[1];  /* list of upvalues */
} CClosure;


typedef struct LClosure {
	ClosureHeader;
	struct Proto *p;
	struct Upval *upvals[1];  /* list of upvalues */
} LClosure;


typedef struct Closure {
	struct CClosure c;
	struct LClosure l;
} Closure;


typedef struct Proto {
	CommonHeader;
	Tvalue *k;  /* constants used by the function */
	unsigned int *code;
	struct Proto **p;  /* functions defined inside the function */
	int *lineinfo;  /* map from opcodes to source lines (debug information) */
	struct LocVar *locvars;  /* information about local variables (debug information) */
	struct Upvaldesc *upvalues;  /* upvalue information */
	Closure *cache;  /* last created closure with this prototype */
	Tstring  *source;  /* used for debug information */
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
} Proto;


/*
 * information about a call
 */
typedef struct Callinfo {
	StkId func;  /* function index in the stack */
	StkId top;  /* top for this function */
	struct Callinfo *prev, *next;  /* dynamic call link */
	short nresults;  /* expected number of results from this function */
	u8 callstatus;
	int extra;
	union {
		struct {  /* only for Lua functions */
			StkId base;  /* base for this function */
			const unsigned int *savedpc;
		} l;
		struct {  /* only for C functions */
			int ctx;  /* context info. in case of yields */
			u8 status;
		} c;
	} u;
} Callinfo;


/*
 * Tables
 */
typedef union Tkey {
	struct {
		union value value_;
		int tt_;
		struct Node *next;  /* for chaining */
	} nk;
	Tvalue tvk;
} Tkey;


typedef struct Node {
	Tvalue i_val;
	Tkey i_key;
} Node;


typedef struct Table {
	CommonHeader;
	u8 flags;  /* 1<<p means tagmethod(p) is not present */
	u8 lsizenode;  /* log2 of size of `node' array */
	int sizearray;  /* size of `array' array */
	Tvalue *array;  /* array part */
	Node *node;
	Node *lastfree;  /* any free position is before this position */
	Gcobject *gclist;
} Table;

#define lmod(s,size)	((int)((s) & ((size)-1)))


typedef struct Stringtable {
	Gcobject **hash;
	int nuse;
	int size;
} Stringtable;

typedef struct global_State {
	Stringtable strt;  /* hash table for strings */
	Tvalue registry;
	unsigned int seed; /* randonized seed for hashes */
	u8 gcstate; /* state of garbage collector */
	u8 gckind; /* kind of GC running */
	u8 gcrunning; /* true if GC is running */

	Gcobject *allgc; /* list of all collectable objects */

	Upval uvhead; /* head of double-linked list of all open upvalues */

	struct ktap_State *mainthread;
#ifdef __KERNEL__
	int nr_builtin_cfunction;
	Tvalue *cfunction_tbl;
	struct task_struct *task;
	struct rchan *ktap_chan;
	int trace_enabled;
	struct list_head timers;
	struct list_head probe_events_head;
	struct completion *user_completion;
#endif
} global_State;

typedef struct ktap_State {
	CommonHeader;
	u8 status;
	global_State *g;
	StkId top;
	Callinfo *ci;
	const unsigned long *oldpc;
	StkId stack_last;
	StkId stack;
	int stacksize;

	Gcobject *openupval;
	Gcobject *gclist;

	Callinfo baseci;

	int debug;
	int version;
	int gcrunning;

	Gcobject *localgc; /* list of temp collectable objects, free when thread exit */
	char buff[128];  /* temporary buffer for string concatentation */
#ifdef __KERNEL__
	struct ktap_event *current_event;
#endif
} ktap_State;


typedef struct gcheader {
	CommonHeader;
} gcheader;

/*
 * Union of all collectable objects
 */
union Gcobject {
  gcheader gch;  /* common header */
  union Tstring ts;
  union Udata u;
  struct Closure cl;
  struct Table h;
  struct Proto p;
  struct Upval uv;
  struct ktap_State th;  /* thread */
};

#define gch(o)	(&(o)->gch)
/* macros to convert a GCObject into a specific value */
#define rawgco2ts(o)	(&((o)->ts))
#define gco2ts(o)       (&rawgco2ts(o)->tsv)

#define gco2uv(o)	(&((o)->uv))

#define obj2gco(v)	((Gcobject *)(v))


#ifdef __KERNEL__
#define ktap_assert(s)
#else
#define ktap_assert(s)
#if 0
#define ktap_assert(s)	\
	do {	\
		if (!s) {	\
			printf("assert failed %s, %d\n", __func__, __LINE__);\
			exit(0);	\
		}	\
	} while(0)
#endif
#endif

#define check_exp(c,e)                (e)


typedef int ktap_Number;


#define ktap_number2int(i,n)   ((i)=(int)(n))


/* predefined values in the registry */
#define KTAP_RIDX_MAINTHREAD     1
#define KTAP_RIDX_GLOBALS        2
#define KTAP_RIDX_LAST           KTAP_RIDX_GLOBALS


#define KTAP_TNONE		(-1)

#define KTAP_TNIL		0
#define KTAP_TBOOLEAN		1
#define KTAP_TLIGHTUSERDATA	2
#define KTAP_TNUMBER		3
#define KTAP_TSTRING		4
#define KTAP_TSHRSTR		(KTAP_TSTRING | (0 << 4))  /* short strings */
#define KTAP_TLNGSTR		(KTAP_TSTRING | (1 << 4))  /* long strings */
#define KTAP_TTABLE		5
#define KTAP_TFUNCTION		6
#define KTAP_TLCL		(KTAP_TFUNCTION | (0 << 4))  /* closure */
#define KTAP_TLCF		(KTAP_TFUNCTION | (1 << 4))  /* light C function */
#define KTAP_TCCL		(KTAP_TFUNCTION | (2 << 4))  /* C closure */
#define KTAP_TUSERDATA		7
#define KTAP_TTHREAD		8

#define KTAP_NUMTAGS		9

#define KTAP_TPROTO		11
#define KTAP_TUPVAL		12

#define KTAP_TEVENT		13


#define ttype(o)	((o->type) & 0x3F)
#define settype(obj, t)	((obj)->type = (t))



/* raw type tag of a TValue */
#define rttype(o)       ((o)->type)

/* tag with no variants (bits 0-3) */
#define novariant(x)    ((x) & 0x0F)

/* type tag of a TValue with no variants (bits 0-3) */
#define ttypenv(o)      (novariant(rttype(o)))

#define val_(o)		((o)->val)

#define bvalue(o)	(val_(o).b)
#define nvalue(o)	(val_(o).n)
#define hvalue(o)	(&val_(o).gc->h)
#define CLVALUE(o)	(&val_(o).gc->cl.l)
#define clcvalue(o)	(&val_(o).gc->cl.c)
#define clvalue(o)	(&val_(o).gc->cl)
#define rawtsvalue(o)	(&val_(o).gc->ts)
#define pvalue(o)	(&val_(o).p)
#define fvalue(o)	(val_(o).f)
#define rawuvalue(o)	(&val_(o).gc->u)
#define uvalue(o)	(&rawuvalue(o)->uv)
#define evalue(o)	(val_(o).p)

#define gcvalue(o)	(val_(o).gc)

#define isnil(o)	(o->type == KTAP_TNIL)
#define isboolean(o)	(o->type == KTAP_TBOOLEAN)
#define isfalse(o)	(isnil(o) || (isboolean(o) && bvalue(o) == 0))

#define ttisshrstring(o)	((o)->type == KTAP_TSHRSTR)
#define ttisstring(o)		(((o)->type & 0x0F) == KTAP_TSTRING)
#define ttisnumber(o)		((o)->type == KTAP_TNUMBER)
#define ttisfunc(o)		((o)->type == KTAP_TFUNCTION)
#define ttisnil(o)		((o)->type == KTAP_TNIL)
#define ttisboolean(o)		((o)->type == KTAP_TBOOLEAN)
#define ttisequal(o1,o2)        ((o1)->type == (o2)->type)
#define ttisevent(o)		((o)->type == KTAP_TEVENT)



#define setnilvalue(obj) {Tvalue *io = (obj); settype(io, KTAP_TNIL);}

#define setbvalue(obj, x) \
  {Tvalue *io = (obj); io->val.b = (x); settype(io, KTAP_TBOOLEAN); }

#define setnvalue(obj, x) \
  { Tvalue *io = (obj); io->val.n = (x); settype(io, KTAP_TNUMBER); }

#define setsvalue(obj, x) \
  { Tvalue *io = (obj); \
    Tstring *x_ = (x); \
    io->val.gc = (Gcobject *)x_; settype(io, x_->tsv.tt); }

#define setcllvalue(obj, x) \
  { Tvalue *io = (obj); \
    io->val.gc = (Gcobject *)x; settype(io, KTAP_TLCL); }

#define sethvalue(obj,x) \
  { Tvalue *io=(obj); \
    val_(io).gc = (Gcobject *)(x); settype(io, KTAP_TTABLE); }

#define setfvalue(obj,x) \
  { Tvalue *io=(obj); val_(io).f=(x); settype(io, KTAP_TLCF); }

#define setthvalue(L,obj,x) \
  { Tvalue *io=(obj); \
    val_(io).gc = (Gcobject *)(x); settype(io, KTAP_TTHREAD); }

#define setevalue(obj, x) \
  { Tvalue *io=(obj); val_(io).p = (x); settype(io, KTAP_TEVENT); }

#define setobj(ks,obj1,obj2) \
        { const Tvalue *io2=(obj2); Tvalue *io1=(obj1); \
          io1->val = io2->val; io1->type = io2->type; }

#define rawequalobj(t1, t2) \
	(ttisequal(t1, t2) && kp_equalobjv(NULL, t1, t2))

#define equalobj(ks, t1, t2) rawequalobj(t1, t2)

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


Tstring *kp_tstring_newlstr(ktap_State *ks, const char *str, size_t l);
Tstring *kp_tstring_newlstr_local(ktap_State *ks, const char *str, size_t l);
Tstring *kp_tstring_new(ktap_State *ks, const char *str);
Tstring *kp_tstring_new_local(ktap_State *ks, const char *str, size_t l);
int kp_tstring_eqstr(Tstring *a, Tstring *b);
unsigned int kp_string_hash(const char *str, size_t l, unsigned int seed);
int kp_tstring_eqlngstr(Tstring *a, Tstring *b);
int kp_tstring_cmp(const Tstring *ls, const Tstring *rs);
void kp_tstring_resize(ktap_State *ks, int newsize);
void kp_tstring_freeall(ktap_State *ks);
Tstring *kp_tstring_assemble(ktap_State *ks, const char *str, size_t l);

Tvalue *kp_table_set(ktap_State *ks, Table *t, const Tvalue *key);
Table *kp_table_new(ktap_State *ks);
const Tvalue *kp_table_getint(Table *t, int key);
void kp_table_setint(ktap_State *ks, Table *t, int key, Tvalue *v);
const Tvalue *kp_table_get(Table *t, const Tvalue *key);
void kp_table_setvalue(ktap_State *ks, Table *t, const Tvalue *key, Tvalue *val);
void kp_table_resize(ktap_State *ks, Table *t, int nasize, int nhsize);
void kp_table_resizearray(ktap_State *ks, Table *t, int nasize);
void kp_table_free(ktap_State *ks, Table *t);
int kp_table_length(ktap_State *ks, Table *t);
void kp_table_dump(ktap_State *ks, Table *t);
void kp_table_histogram(ktap_State *ks, Table *t);
int kp_table_next(ktap_State *ks, Table *t, StkId key);

void kp_obj_dump(ktap_State *ks, const Tvalue *v);
void kp_showobj(ktap_State *ks, const Tvalue *v);
int kp_objlen(ktap_State *ks, const Tvalue *rb);
Gcobject *kp_newobject(ktap_State *ks, int type, size_t size, Gcobject **list);
int kp_equalobjv(ktap_State *ks, const Tvalue *t1, const Tvalue *t2);
Closure *kp_newlclosure(ktap_State *ks, int n);
Proto *kp_newproto(ktap_State *ks);
Upval *kp_newupval(ktap_State *ks);
void kp_free_all_gcobject(ktap_State *ks);
void kp_header(u8 *h);

int kp_str2d(const char *s, size_t len, ktap_Number *result);

#define kp_realloc(ks, v, osize, nsize, t) \
	((v) = (t *)kp_reallocv(ks, v, osize * sizeof(t), nsize * sizeof(t)))


/* todo: print callchain to user in kernel */
#define kp_runerror(ks, args...) \
	do { \
		kp_printf(ks, args);	\
		kp_exit(ks);	\
	} while(0)

#ifdef __KERNEL__
#define G(ks)   (ks->g)

#define KTAP_ALLOC_FLAGS ((GFP_KERNEL | __GFP_NORETRY | __GFP_NOWARN) \
			 & ~__GFP_WAIT)

#define kp_malloc(ks, size)			kmalloc(size, KTAP_ALLOC_FLAGS)
#define kp_free(ks, block)			kfree(block)
#define kp_reallocv(ks, block, osize, nsize)	krealloc(block, nsize, KTAP_ALLOC_FLAGS)
#define kp_zalloc(ks, size)			kzalloc(size, KTAP_ALLOC_FLAGS)
void kp_printf(ktap_State *ks, const char *fmt, ...);
#else
/*
 * this is used for ktapc tstring operation, tstring need G(ks)->strt
 * and G(ks)->seed, so ktapc need to init those field
 */
#define G(ks)   (&dummy_global_state)
extern global_State dummy_global_state;

#define kp_malloc(ks, size)			malloc(size)
#define kp_free(ks, block)			free(block)
#define kp_reallocv(ks, block, osize, nsize)	realloc(block, nsize)
#define kp_printf(ks, args...)			printf(args)
#define kp_exit(ks)				exit(EXIT_FAILURE)

#define DEFINE_SPINLOCK
#define spin_lock
#define spin_unlock
#endif

/*
 * KTAP_QL describes how error messages quote program elements.
 * CHANGE it if you want a different appearance.
 */
#define KTAP_QL(x)      "'" x "'"
#define KTAP_QS         KTAP_QL("%s")

#endif /* __KTAP_TYPES_H__ */

