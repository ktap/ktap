/*
 * ktapc.h
 * only can be included by userspace compiler
 */

typedef int bool;
#define false 0
#define true 1

#define MAX_INT         ((int)(~0U>>1))
#define UCHAR_MAX	255

#define MAX_SIZET  ((size_t)(~(size_t)0)-2)

#define KTAP_ERRSYNTAX 3

/*
 * KTAP_IDSIZE gives the maximum size for the description of the source
 * of a function in debug information.
 * CHANGE it if you want a different size.
 */
#define KTAP_IDSIZE      60


#define FIRST_RESERVED  257

/*
 * maximum depth for nested C calls and syntactical nested non-terminals
 * in a program. (Value must fit in an unsigned short int.)
 */
#define KTAP_MAXCCALLS          200

#define KTAP_MULTRET     (-1)


#define SHRT_MAX	UCHAR_MAX

#define MAXUPVAL   UCHAR_MAX


/* maximum stack for a ktap function */
#define MAXSTACK        250

#define islalpha(c)   (isalpha(c) || (c) == '_')
#define islalnum(c)   (isalnum(c) || (c) == '_')

#define isreserved(s) ((s)->tsv.tt == KTAP_TSHRSTR && (s)->tsv.extra > 0)

#define ktap_numeq(a,b)		((a)==(b))
#define ktap_numisnan(L,a)	(!ktap_numeq((a), (a)))

#define ktap_numunm(a)		(-(a))

/*
 * ** Comparison and arithmetic functions
 * */

#define KTAP_OPADD       0       /* ORDER TM */
#define KTAP_OPSUB       1
#define KTAP_OPMUL       2
#define KTAP_OPDIV       3
#define KTAP_OPMOD       4
#define KTAP_OPPOW       5
#define KTAP_OPUNM       6

#define KTAP_OPEQ        0
#define KTAP_OPLT        1
#define KTAP_OPLE        2


/*
 * WARNING: if you change the order of this enumeration,
 * grep "ORDER RESERVED"
 */
enum RESERVED {
	/* terminal symbols denoted by reserved words */
	TK_TRACE = FIRST_RESERVED, TK_TRACE_END,
	TK_AND, TK_BREAK,
	TK_DO, TK_ELSE, TK_ELSEIF, TK_END, TK_FALSE, TK_FOR, TK_FUNCTION,
	TK_GOTO, TK_IF, TK_IN, TK_LOCAL, TK_NIL, TK_NOT, TK_OR, TK_REPEAT,
	TK_RETURN, TK_THEN, TK_TRUE, TK_UNTIL, TK_WHILE,
	/* other terminal symbols */
	TK_CONCAT, TK_DOTS, TK_EQ, TK_GE, TK_LE, TK_NE, TK_DBCOLON, TK_EOS,
	TK_NUMBER, TK_NAME, TK_STRING
};

/* number of reserved words */
#define NUM_RESERVED    ((int)(TK_WHILE-FIRST_RESERVED + 1))

#define EOZ     (0)                    /* end of stream */

typedef union {
	ktap_Number r;
	Tstring *ts;
} SemInfo;  /* semantics information */


typedef struct Token {
	int token;
	SemInfo seminfo;
} Token;

typedef struct Mbuffer {
	char *buffer;
	size_t n;
	size_t buffsize;
} Mbuffer;

#define mbuff_init(buff)	((buff)->buffer = NULL, (buff)->buffsize = 0)
#define mbuff(buff)		((buff)->buffer)
#define mbuff_reset(buff)	((buff)->n = 0, memset((buff)->buffer, 0, (buff)->buffsize))
#define mbuff_len(buff)		((buff)->n)
#define mbuff_size(buff)	((buff)->buffsize)

#define mbuff_resize(buff, size) \
	(ktapc_realloc((buff)->buffer, (buff)->buffsize, size, char), \
	(buff)->buffsize = size)

#define mbuff_free(buff)        mbuff_resize(buff, 0)


/*
 * state of the lexer plus state of the parser when shared by all
 * functions
 */
typedef struct LexState {
	unsigned char *ptr; /* source file reading position */
	int current;  /* current character (charint) */
	int linenumber;  /* input line counter */
	int lastline;  /* line of last token `consumed' */
	Token t;  /* current token */
	Token lookahead;  /* look ahead token */
	struct FuncState *fs;  /* current function (parser) */
	Mbuffer *buff;  /* buffer for tokens */
	struct Dyndata *dyd;  /* dynamic structures used by the parser */
	Tstring *source;  /* current source name */
	Tstring *envn;  /* environment variable name */
	char decpoint;  /* locale decimal point */
	int nCcalls;
} LexState;


/*
 * Expression descriptor
 */
typedef enum {
	VVOID,        /* no value */
	VNIL,
	VTRUE,
	VFALSE,
	VK,           /* info = index of constant in `k' */
	VKNUM,        /* nval = numerical value */
	VNONRELOC,    /* info = result register */
	VLOCAL,       /* info = local register */
	VUPVAL,       /* info = index of upvalue in 'upvalues' */
	VINDEXED,     /* t = table register/upvalue; idx = index R/K */
	VJMP,         /* info = instruction pc */
	VRELOCABLE,   /* info = instruction pc */
	VCALL,        /* info = instruction pc */
	VVARARG       /* info = instruction pc */
} expkind;


#define vkisvar(k)      (VLOCAL <= (k) && (k) <= VINDEXED)
#define vkisinreg(k)    ((k) == VNONRELOC || (k) == VLOCAL)

typedef struct expdesc {
	expkind k;
	union {
		struct {  /* for indexed variables (VINDEXED) */
			short idx;  /* index (R/K) */
			u8 t;  /* table (register or upvalue) */
			u8 vt;  /* whether 't' is register (VLOCAL) or upvalue (VUPVAL) */
		} ind;
		int info;  /* for generic use */
		ktap_Number nval;  /* for VKNUM */
	} u;
	int t;  /* patch list of `exit when true' */
	int f;  /* patch list of `exit when false' */
} expdesc;


typedef struct Vardesc {
	short idx;  /* variable index in stack */
} Vardesc;


/* description of pending goto statements and label statements */
typedef struct Labeldesc {
	Tstring *name;  /* label identifier */
	int pc;  /* position in code */
	int line;  /* line where it appeared */
	u8 nactvar;  /* local level where it appears in current block */
} Labeldesc;


/* list of labels or gotos */
typedef struct Labellist {
	Labeldesc *arr;  /* array */
	int n;  /* number of entries in use */
	int size;  /* array size */
} Labellist;


/* dynamic structures used by the parser */
typedef struct Dyndata {
	struct {  /* list of active local variables */
		Vardesc *arr;
		int n;
		int size;
	} actvar;
	Labellist gt;  /* list of pending gotos */
	Labellist label;   /* list of active labels */
} Dyndata;


/* control of blocks */
struct BlockCnt;  /* defined in lparser.c */


/* state needed to generate code for a given function */
typedef struct FuncState {
	Proto *f;  /* current function header */
	Table *h;  /* table to find (and reuse) elements in `k' */
	struct FuncState *prev;  /* enclosing function */
	struct LexState *ls;  /* lexical state */
	struct BlockCnt *bl;  /* chain of current blocks */
	int pc;  /* next position to code (equivalent to `ncode') */
	int lasttarget;   /* 'label' of last 'jump label' */
	int jpc;  /* list of pending jumps to `pc' */
	int nk;  /* number of elements in `k' */
	int np;  /* number of elements in `p' */
	int firstlocal;  /* index of first local var (in Dyndata array) */
	short nlocvars;  /* number of elements in 'f->locvars' */
	u8 nactvar;  /* number of active local variables */
	u8 nups;  /* number of upvalues */
	u8 freereg;  /* first free register */
} FuncState;




/*
 * Marks the end of a patch list. It is an invalid value both as an absolute
 * address, and as a list link (would link an element to itself).
 */
#define NO_JUMP (-1)


/*
 * grep "ORDER OPR" if you change these enums  (ORDER OP)
 */
typedef enum BinOpr {
	OPR_ADD, OPR_SUB, OPR_MUL, OPR_DIV, OPR_MOD, OPR_POW,
	OPR_CONCAT,
	OPR_EQ, OPR_LT, OPR_LE,
	OPR_NE, OPR_GT, OPR_GE,
	OPR_AND, OPR_OR,
	OPR_NOBINOPR
} BinOpr;


typedef enum UnOpr { OPR_MINUS, OPR_NOT, OPR_LEN, OPR_NOUNOPR } UnOpr;


#define getcode(fs,e)   ((fs)->f->code[(e)->u.info])

#define codegen_codeAsBx(fs,o,A,sBx)       codegen_codeABx(fs,o,A,(sBx)+MAXARG_sBx)

#define codegen_setmultret(fs,e)   codegen_setreturns(fs, e, KTAP_MULTRET)

#define codegen_jumpto(fs,t)       codegen_patchlist(fs, codegen_jump(fs), t)


#define ktapc_realloc(v, osize, nsize, t) \
        ((v) = (t *)ktapc_reallocv(v, osize * sizeof(t), nsize * sizeof(t)))

#define ktapc_reallocvector(v,oldn,n,t)	ktapc_realloc(v,oldn,n,t)


#define ktapc_growvector(v,nelems,size,t,limit,e) \
          if ((nelems)+1 > (size)) \
            ((v)=(t *)ktapc_growaux(v,&(size),sizeof(t),limit,e))


Tstring *lex_newstring(LexState *ls, const char *str, size_t l);
const char *lex_token2str(LexState *ls, int token);
void lex_syntaxerror(LexState *ls, const char *msg);
void lex_setinput(LexState *ls, unsigned char *ptr, Tstring *source, int firstchar);
void lex_next(LexState *ls);
int lex_lookahead(LexState *ls);
ktap_closure *ktapc_parser(unsigned char *pos, const char *name);
Tstring *ktapc_ts_new(const char *str);
int ktapc_ts_eqstr(Tstring *a, Tstring *b);
Tstring *ktapc_ts_newlstr(const char *str, size_t l);
Proto *ktapc_newproto();
Table *ktapc_table_new();
ktap_value *ktapc_table_set(Table *t, const ktap_value *key);
ktap_closure *ktapc_newlclosure(int n);
char *ktapc_sprintf(const char *fmt, ...);

void *ktapc_reallocv(void *block, size_t osize, size_t nsize);
void *ktapc_growaux(void *block, int *size, size_t size_elems, int limit,
		    const char *what);

void ktapio_exit(void);
int ktapio_create(void *);

Tstring *ktapc_parse_eventdef(Tstring *eventdef);

#define ktapc_equalobj(t1, t2)	kp_equalobjv(NULL, t1, t2)

