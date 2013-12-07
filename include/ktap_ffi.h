#ifndef __KTAP_FFI_H__
#define __KTAP_FFI_H__

#ifdef CONFIG_KTAP_FFI

#include "../include/ktap_types.h"

/*
 * Types design in FFI module
 *
 * ktap_cdata is an instance of csymbol, so it's a combination of csymbol
 * and it's actual data in memory.
 *
 * csymbol structs are globally unique and readonly type that represent C
 * types.  For non scalar C types like struct and function, helper structs are
 * used to store detailed information. See csymbol_func and csymbol_struct for
 * more information.
 */

typedef enum {
	/* 0 - 4 */
	FFI_VOID,
	FFI_UINT8,
	FFI_INT8,
	FFI_UINT16,
	FFI_INT16,
	/* 5 - 9 */
	FFI_UINT32,
	FFI_INT32,
	FFI_UINT64,
	FFI_INT64,
	FFI_PTR,
	/* 10 - 12 */
	FFI_FUNC,
	FFI_STRUCT,
	FFI_UNKNOWN,
} ffi_type;
#define NUM_FFI_TYPE ((int)FFI_UNKNOWN)


/* following struct and macros are added for C typedef
 * size and alignment calculation */
typedef struct {
	size_t size;
	size_t align;
	const char *name;
} ffi_mode;
extern const ffi_mode const ffi_type_modes[];

#define ffi_type_size(t) (ffi_type_modes[t].size)
#define ffi_type_align(t) (ffi_type_modes[t].align)
#define ffi_type_name(t) (ffi_type_modes[t].name)


/* start of csymbol definition */
#define CSYM_NAME_MAX_LEN 64

typedef struct csymbol_func {
	void *addr;		/* function address */
	csymbol_id ret_id;	/* function return type */
	int arg_nr;		/* number of arguments */
	csymbol_id *arg_ids;	/* function argument types */
	unsigned has_var_arg;	/* is this a var arg function? */
} csymbol_func;

typedef struct struct_member {
	char name[CSYM_NAME_MAX_LEN];	/* name for this struct member */
	csymbol_id id;			/* type for this struct member */
} struct_member;

typedef struct csymbol_struct {
	int memb_nr;			/* number of members */
	struct_member *members;		/* array for each member definition */
	size_t size;			/* bytes used to store struct */
	/* alignment of the struct, 0 indicates uninitialization */
	size_t align;
} csymbol_struct;


/* wrapper struct for all C symbols */
typedef struct csymbol {
	char name[CSYM_NAME_MAX_LEN];	/* name for this symbol */
	ffi_type type;			/* type for this symbol  */
	/* following members are used only for non scalar C types */
	union {
		csymbol_id p;		/* pointer type */
		csymbol_func f;		/* C function type */
		csymbol_struct st;	/* struct type */
		csymbol_id td;		/* typedef type */
	} u;
} csymbol;

/* lookup csymbol address by it's id */
inline csymbol *ffi_get_csym_by_id(ktap_state *ks, csymbol_id id);
#define id_to_csym(ks, id) (ffi_get_csym_by_id(ks, id))

/* helper macros for struct csymbol */
#define csym_type(cs) ((cs)->type)
#define csym_name(cs) ((cs)->name)

/*
 * helper macros for pointer symbol
 */
#define csym_ptr_deref_id(cs) ((cs)->u.p)
#define csym_set_ptr_deref_id(cs, id) ((cs)->u.p = (id))
/* following macro gets csymbol address */
#define csym_ptr_deref(ks, cs) (id_to_csym(ks, csym_ptr_deref_id(cs)))

/*
 * helper macros for function symbol
 * csym_* accepts csymbol type
 * csymf_* accepts csymbol_func type
 */
#define csymf_addr(csf) ((csf)->addr)
#define csymf_ret_id(csf) ((csf)->ret_id)
#define csymf_arg_nr(csf) ((csf)->arg_nr)
#define csymf_arg_ids(csf) ((csf)->arg_ids)
/* get csymbol id for the nth argument */
#define csymf_arg_id(csf, n) ((csf)->arg_ids[n])
#define csym_func(cs) (&((cs)->u.f))
#define csym_func_addr(cs) (csymf_addr(csym_func(cs)))
#define csym_func_arg_ids(cs) (csymf_arg_ids(csym_func(cs)))
/* following macros get csymbol address */
#define csymf_ret(ks, csf) (id_to_csym(ks, csymf_ret_id(csf)))
/* get csymbol address for the nth argument */
#define csymf_arg(ks, csf, n) (id_to_csym(ks, csymf_arg_id(csf, n)))
#define csym_func_arg(ks, cs, n) (csymf_arg(ks, csym_func(cs), n))

/*
 * helper macors for struct symbol
 * csym_* accepts csymbol type
 * csymst_* accepts csymbol_struct type
 */
#define csymst_mb_nr(csst) ((csst)->memb_nr)
#define csym_struct(cs) (&(cs)->u.st)
#define csym_struct_mb(cs) (csymst_mb(ks, csym_struct(cs), n))
/* following macro gets csymbol address for the nth struct member */
#define csymst_mb(ks, csst, n) (id_to_csym(ks, (csst)->members[n].id))


/*
 * helper macros for ktap_cdata type
 */
#define cd_csym_id(cd) ((cd)->id)
#define cd_set_csym_id(cd, id) (cd_csym_id(cd) = (id))
#define cd_csym(ks, cd) (id_to_csym(ks, cd_csym_id(cd)))
#define cd_type(ks, cd) (cd_csym(ks, cd)->type)

#define cd_int(cd) ((cd)->u.i)
#define cd_ptr(cd) ((cd)->u.p)
#define cd_struct(cd) ((cd)->u.st)


#ifdef __KERNEL__
size_t csym_size(ktap_state *ks, csymbol *sym);
size_t csym_align(ktap_state *ks, csymbol *sym);
size_t csym_struct_offset(ktap_state *ks, csymbol_struct *csst, int idx);
void init_csym_struct(ktap_state *ks, csymbol_struct *csst);

void kp_ffi_free_symbol(ktap_state *ks);
csymbol_id ffi_get_csym_id(ktap_state *ks, char *name);

ktap_cdata *kp_cdata_new(ktap_state *ks);
void kp_cdata_dump(ktap_state *ks, ktap_cdata *cd);
ktap_cdata *kp_cdata_new_ptr(ktap_state *ks, void *addr, csymbol_id id);
ktap_cdata *kp_cdata_new_struct(ktap_state *ks, void *val, csymbol_id id);

int kp_ffi_call(ktap_state *ks, csymbol_func *cf);
#endif /* for __KERNEL__ */

#else

static void __maybe_unused kp_ffi_free_symbol(ktap_state *ks)
{
	return;
}

#endif /* CONFIG_KTAP_FFI */

#endif /* __KTAP_FFI_H__ */
