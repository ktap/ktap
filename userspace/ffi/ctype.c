#include "../../include/ktap_types.h"
#include "../../include/ktap_opcodes.h"
#include "../ktapc.h"
#include "../cparser.h"


/* for ktap vm */
cp_csymbol_state csym_state;

#define cs_nr (csym_state.cs_nr)
#define cs_arr_size (csym_state.cs_arr_size)
#define cs_arr (csym_state.cs_arr)

csymbol *cp_id_to_csym(int id)
{
	return &cs_arr[id];
}


typedef struct cp_ctype_entry {
	char name[MAX_TYPE_NAME_LEN];
	struct cp_ctype ct;
} cp_ctype_entry;

#define DEFAULT_CTYPE_ARR_SIZE 100
static int cte_nr;
static int cte_arr_size;
static cp_ctype_entry *cte_arr;


/* stack to help maintain state during parsing */
typedef struct cp_ctype_stack {
	int size;
	int top;
	cp_ctype_entry *stack;
} ctype_stack;


static ctype_stack cts;

#define ct_stack(id) (&(cts.stack[id]))
#define ct_stack_ct(id) (&(cts.stack[id].ct))



int cp_ctype_reg_csymbol(csymbol *cs);


size_t ctype_size(const struct cp_ctype *ct)
{
	if (ct->pointers - ct->is_array) {
		return sizeof(void*) * (ct->is_array ? ct->array_size : 1);

	} else if (!ct->is_defined || ct->type == VOID_TYPE) {
		cp_error("can't calculate size of an undefined type");
		return 0;
	} else if (ct->variable_size_known) {
		assert(ct->is_variable_struct && !ct->is_array);
		return ct->base_size + ct->variable_increment;
	} else if (ct->is_variable_array || ct->is_variable_struct) {
		cp_error("internal error: calc size of variable type with "
				"unknown size");
		return 0;
	} else {
		return ct->base_size * (ct->is_array ? ct->array_size : 1);
	}
}

#define MAX_STACK_SIZE 100
int ctype_stack_grow(int size)
{
	struct cp_ctype_entry *new_st;

	assert(cts.size + size < MAX_STACK_SIZE);

	new_st = realloc(cts.stack, (cts.size+size)*sizeof(cp_ctype_entry));
	if (new_st)
		cts.stack = new_st;
	else
		return -1;

	cts.size += size;

	return size;
}

int ctype_stack_free_space()
{
	return cts.size - cts.top;
}

void ctype_stack_reset()
{
	cts.top = 0;
}

/* push ctype to stack, create new csymbol if needed */
void cp_push_ctype_with_name(struct cp_ctype *ct, const char *name, int nlen)
{
	int i;
	struct cp_ctype *nct;

	if (ctype_stack_free_space() < 1)
		ctype_stack_grow(4);

	/* we have to check pointer here because does type lookup by name
	 * before parsing '*', and for pointers, ct will always be the
	 * original type */
	if (ct->pointers) {
		for (i = 0; i < cte_nr; i++) {
			nct = &(cte_arr[i].ct);
			if (nct->type == ct->type &&
					nct->pointers == ct->pointers) {
				break;
			}
		}

		if (i == cte_nr) {
			/* pointer type not found
			 * create a new pointer symbol for this type */
			/* associate ctype with new csymbol */
			ct->ffi_cs_id = cp_symbol_build_pointer(ct);
			/* register wit new pointer name */
			cp_ctype_reg_type(csym_name(ct_ffi_cs(ct)), ct);
		} else {
			/* pointer type already registered, reinstantiate ct */
			*ct = cte_arr[i].ct;
		}
	}
	memset(ct_stack(cts.top), 0, sizeof(cp_ctype_entry));
	ct_stack(cts.top)->ct = *ct;
	if (name)
		strncpy(ct_stack(cts.top)->name, name, nlen);
	cts.top++;
}

void cp_push_ctype(struct cp_ctype *ct)
{
	cp_push_ctype_with_name(ct, NULL, 0);
}

void cp_set_defined(struct cp_ctype *ct)
{
	ct->is_defined = 1;

	/* @TODO: update ctypes and cdatas that were created before the
	 * definition came in */
}

void cp_ctype_dump_stack()
{
	int i;
	struct cp_ctype *ct;

	printf("---------------------------\n");
	printf("start of ctype stack (%d) dump: \n", cts.top);
	for (i = 0; i < cts.top; i++) {
		ct = ct_stack_ct(i);
		printf("[%d] -> cp_ctype: %d, sym_type: %d, pointer: %d "
			"symbol_id: %d, name: %s\n",
			i, ct->type,
			csym_type(ct_ffi_cs(ct)), ct->pointers, ct->ffi_cs_id,
			ct_stack(i)->name);
	}
}

int ctype_reg_table_grow()
{
	cp_ctype_entry *new_arr;

	new_arr = realloc(cte_arr, sizeof(cp_ctype_entry)*cte_arr_size*2);
	if (!new_arr)
		cp_error("failed to allocate memory for ctype array\n");

	cte_arr_size = cte_arr_size * 2;
	return 0;
}

/* return index in csymbol array */
int cp_ctype_reg_csymbol(csymbol *cs)
{
	if (cs_nr >= cs_arr_size) {
		cs_arr_size *= 2;
		cs_arr = realloc(cs_arr, cs_arr_size*sizeof(csymbol));
		if (!cs_arr)
			cp_error("failed to extend csymbol array!\n");
	}

	cs_arr[cs_nr] = *cs;
	cs_nr++;

	return cs_nr-1;
}

void __cp_symbol_dump_struct(csymbol *cs)
{
	int i;
	csymbol *ncs;
	csymbol_struct *stcs = csym_struct(cs);

	printf("=== [%s] definition ==================\n", csym_name(cs));
	for (i = 0; i < stcs->memb_nr; i++) {
		printf("\t(%d) ", i);
		printf("csym_id: %d, ", stcs->members[i].id);
		ncs = &cs_arr[stcs->members[i].id];
		printf("name: %s, ffi_ctype: %d, %s\n",
			stcs->members[i].name, ncs->type, csym_name(ncs));
	}
}

void cp_symbol_dump_struct(int id)
{
	__cp_symbol_dump_struct(&cs_arr[id]);
}

int cp_symbol_build_struct(const char *stname)
{
	int i, id, memb_size;
	cp_ctype_entry *cte;
	csymbol nst;
	struct_member *st_membs;
	csymbol_struct *stcs;

	if (cts.top <= 0 || !stname) {
		cp_error("invalid struct definition.\n");
	}

	memb_size = cts.top;
	st_membs = malloc(memb_size*sizeof(struct_member));
	if (!st_membs)
		cp_error("failed to allocate memory for struct members.\n");
	memset(st_membs, 0, memb_size*sizeof(struct_member));

	nst.type = FFI_STRUCT;
	strcpy(nst.name, stname);

	stcs = csym_struct(&nst);
	stcs->memb_nr = memb_size;
	stcs->members = st_membs;

	for (i = 0; i < memb_size; i++) {
		assert(i < cts.top);
		cte = ct_stack(i);
		if (cte->name)
			strcpy(st_membs[i].name, cte->name);
		st_membs[i].id = ct_stack_ct(i)->ffi_cs_id;
	}

	id = cp_ctype_reg_csymbol(&nst);

	ctype_stack_reset();

	return id;
}

/* build pointer symbol from given csymbol */
int cp_symbol_build_pointer(struct cp_ctype *ct)
{
	int id, ret;
	csymbol ncspt;
	csymbol *ref_cs = ct_ffi_cs(ct);

	/* TODO: Check correctness of multi-level pointer 24.11.2013(unihorn) */
	memset(&ncspt, 0, sizeof(csymbol));
	ncspt.type = FFI_PTR;
	ret = sprintf(ncspt.name, "%s *", csym_name(ref_cs));
	assert(ret < MAX_TYPE_NAME_LEN);

	csym_set_ptr_deref_id(&ncspt, ct->ffi_cs_id);
	id = cp_ctype_reg_csymbol(&ncspt);

	return id;
}

void __cp_symbol_dump_func(csymbol *cs)
{
	int i;
	csymbol *ncs;
	csymbol_func *fcs = csym_func(cs);

	printf("=== [%s] function definition =============\n", csym_name(cs));
	ncs = cp_csymf_ret(fcs);
	printf("address: %p\n", fcs->addr);
	printf("return type: \n");
	printf("\tcsym_id: %d, ffi_ctype: %d, %s\n",
			fcs->ret_id, ncs->type, csym_name(ncs));
	printf("args type (%d): \n", fcs->arg_nr);
	for (i = 0; i < csymf_arg_nr(fcs); i++) {
	    printf("\t (%d) ", i);
	    printf("csym_id: %d, ", fcs->arg_ids[i]);
	    ncs = cp_csymf_arg(fcs, i);
	    printf("ffi_ctype: %d, %s\n", ncs->type, csym_name(ncs));
	}
}

void cp_symbol_dump_func(int id)
{
	__cp_symbol_dump_func(&cs_arr[id]);
}

int cp_symbol_build_func(struct cp_ctype *type, const char *fname, int fn_size)
{
	int i = 1, arg_nr, id;
	int *argsym_id_arr;
	csymbol nfcs;
	csymbol_func *fcs;

	if (cts.top == 0 || fn_size < 0 || !fname) {
		cp_error("invalid function definition.\n");
	}

	argsym_id_arr = NULL;
	memset(&nfcs, 0, sizeof(csymbol));
	csym_type(&nfcs) = FFI_FUNC;

	strncpy(csym_name(&nfcs), fname, fn_size);

	fcs = csym_func(&nfcs);
	fcs->has_var_arg = type->has_var_arg;
	/* Type needed for handling variable args handle */
	if (fcs->has_var_arg && !ctype_lookup_type("void *"))
		cp_symbol_build_pointer(ctype_lookup_type("void"));

	/* Fetch start address of function  */
	fcs->addr = (void *)find_kernel_symbol(csym_name(&nfcs));
	if (!fcs->addr)
		cp_error("wrong function address for %s\n", csym_name(&nfcs));

	/* bottom of the stack is return type */
	fcs->ret_id = ct_stack_ct(0)->ffi_cs_id;

	/* the rest is argument type */
	if (cts.top == 1) {
		/* function takes no argument */
		arg_nr = 0;
	} else {
		arg_nr = cts.top - 1;
		argsym_id_arr = malloc(arg_nr * sizeof(int));
		if (!argsym_id_arr)
			cp_error("failed to allocate memory for function args.\n");
		for (i = 0; i < arg_nr; i++) {
			argsym_id_arr[i] = ct_stack_ct(i+1)->ffi_cs_id;
		}
	}
	fcs->arg_nr = arg_nr;
	fcs->arg_ids = argsym_id_arr;

	id = cp_ctype_reg_csymbol(&nfcs);

	/* clear stack since we have consumed all the ctypes */
	ctype_stack_reset();

	return id;
}

struct cp_ctype *cp_ctype_reg_type(char *name, struct cp_ctype *ct)
{
	if (cte_nr >= cte_arr_size)
		ctype_reg_table_grow();

	memset(cte_arr[cte_nr].name, 0, MAX_TYPE_NAME_LEN);
	strcpy(cte_arr[cte_nr].name, name);

	cte_arr[cte_nr].ct = *ct;
	cte_nr++;

	return &(cte_arr[cte_nr-1].ct);
}

#if 0
/* TODO: used for size calculation */
static ffi_type ffi_int_type(ktap_state *ks, int size, bool sign)
{
	switch(size) {
	case 1:
		if (!sign)
			return FFI_UINT8;
		else
			return FFI_INT8;
	case 2:
		if (!sign)
			return FFI_UINT16;
		else
			return FFI_INT16;
	case 4:
		if (!sign)
			return FFI_UINT32;
		else
			return FFI_INT32;
	case 8:
		if (!sign)
			return FFI_UINT64;
		else
			return FFI_INT64;
	default:
		kp_error(ks, "Error: Have not support int type of size %d\n", size);
		return FFI_UNKNOWN;
	}

	/* NEVER reach here, silence compiler */
	return -1;
}
#endif


static inline void ct_set_type(struct cp_ctype *ct, int type, int is_unsigned)
{
	ct->type = type;
	ct->is_unsigned = is_unsigned;
}

static void init_builtin_type(struct cp_ctype *ct, ffi_type ftype)
{
	csymbol cs;
	int cs_id;

	csym_type(&cs) = ftype;
	strncpy(csym_name(&cs), ffi_type_name(ftype), CSYM_NAME_MAX_LEN);
	cs_id = cp_ctype_reg_csymbol(&cs);

	memset(ct, 0, sizeof(*ct));
	ct->ffi_cs_id = cs_id;
	switch (ftype) {
	case FFI_VOID:		ct_set_type(ct, VOID_TYPE, 0); break;
	case FFI_UINT8:		ct_set_type(ct, INT8_TYPE, 1); break;
	case FFI_INT8:		ct_set_type(ct, INT8_TYPE, 0); break;
	case FFI_UINT16:	ct_set_type(ct, INT16_TYPE, 1); break;
	case FFI_INT16:		ct_set_type(ct, INT16_TYPE, 0); break;
	case FFI_UINT32:	ct_set_type(ct, INT32_TYPE, 1); break;
	case FFI_INT32:		ct_set_type(ct, INT32_TYPE, 0); break;
	case FFI_UINT64:	ct_set_type(ct, INT64_TYPE, 1); break;
	case FFI_INT64:		ct_set_type(ct, INT64_TYPE, 0); break;
	default:		break;
	}
	ct->base_size = ffi_type_size(ftype);
	ct->align_mask = ffi_type_align(ftype) - 1;
	ct->is_defined = 1;
}

/*
 * lookup and register builtin C type on demand
 * You should ensure that the type with name doesn't appear in
 * csymbol table before calling.
 */
struct cp_ctype *ctype_lookup_builtin_type(char *name)
{
	struct cp_ctype ct;

	if (!strncmp(name, "void", sizeof("void"))) {
		init_builtin_type(&ct, FFI_VOID);
		return cp_ctype_reg_type("void", &ct);
	} else if (!strncmp(name, "int8_t", sizeof("int8_t"))) {
		init_builtin_type(&ct, FFI_INT8);
		return cp_ctype_reg_type("int8_t", &ct);
	} else if (!strncmp(name, "uint8_t", sizeof("uint8_t"))) {
		init_builtin_type(&ct, FFI_UINT8);
		return cp_ctype_reg_type("uint8_t", &ct);
	} else if (!strncmp(name, "int16_t", sizeof("int16_t"))) {
		init_builtin_type(&ct, FFI_INT16);
		return cp_ctype_reg_type("int16_t", &ct);
	} else if (!strncmp(name, "uint16_t", sizeof("uint16_t"))) {
		init_builtin_type(&ct, FFI_UINT16);
		return cp_ctype_reg_type("uint16_t", &ct);
	} else if (!strncmp(name, "int32_t", sizeof("int32_t"))) {
		init_builtin_type(&ct, FFI_INT32);
		return cp_ctype_reg_type("int32_t", &ct);
	} else if (!strncmp(name, "uint32_t", sizeof("uint32_t"))) {
		init_builtin_type(&ct, FFI_UINT32);
		return cp_ctype_reg_type("uint32_t", &ct);
	} else if (!strncmp(name, "int64_t", sizeof("int64_t"))) {
		init_builtin_type(&ct, FFI_INT64);
		return cp_ctype_reg_type("int64_t", &ct);
	} else if (!strncmp(name, "uint64_t", sizeof("uint64_t"))) {
		init_builtin_type(&ct, FFI_UINT64);
		return cp_ctype_reg_type("uint64_t", &ct);
	} else {
		/* no builtin type matched */
		return NULL;
	}
}

/* start ctype reg table */
struct cp_ctype *ctype_lookup_type(char *name)
{
	int i;
	struct cp_ctype *ct;

	for (i = 0; i < cte_nr; i++) {
		ct = &cte_arr[i].ct;
		if (!strcmp(name, cte_arr[i].name))
			return ct;
	}

	/* see if it's a builtin C type
	 * return NULL if still no match */
	return ctype_lookup_builtin_type(name);
}

cp_csymbol_state *ctype_get_csym_state(void)
{
	return &csym_state;
}

#define DEFAULT_STACK_SIZE 20
#define DEFAULT_SYM_ARR_SIZE 20
int cp_ctype_init()
{
	cts.size = DEFAULT_STACK_SIZE;
	cts.top = 0;
	cts.stack = malloc(sizeof(cp_ctype_entry)*DEFAULT_STACK_SIZE);

	cs_nr = 0;
	cs_arr_size = DEFAULT_SYM_ARR_SIZE;
	cs_arr = malloc(sizeof(csymbol)*DEFAULT_SYM_ARR_SIZE);
	memset(cs_arr, 0, sizeof(csymbol)*DEFAULT_SYM_ARR_SIZE);

	cte_nr = 0;
	cte_arr_size = DEFAULT_CTYPE_ARR_SIZE;
	cte_arr = malloc(sizeof(cp_ctype_entry)*DEFAULT_CTYPE_ARR_SIZE);

	return 0;
}

int cp_ctype_free()
{
	int i;
	csymbol *cs;

	if (cts.stack)
		free(cts.stack);

	if (cs_arr) {
		for (i = 0; i < cs_nr; i++) {
			cs = &cs_arr[i];
			if (csym_type(cs) == FFI_FUNC) {
				if (csym_func(cs)->arg_ids)
					free(csym_func(cs)->arg_ids);
			} else if (csym_type(cs) == FFI_STRUCT) {
				if (csym_struct(cs)->members)
					free(csym_struct(cs)->members);
			}
		}
		free(cs_arr);
	}

	if (cte_arr) {
		free(cte_arr);
	}

	return 0;
}
