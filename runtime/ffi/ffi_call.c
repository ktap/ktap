/*
 * ffi_call.c - foreign function calling library support for ktap
 *
 * This file is part of ktap by Jovi Zhangwei.
 *
 * Copyright (C) 2012-2013 Jovi Zhangwei <jovi.zhangwei@gmail.com>.
 *
 * ktap is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * ktap is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/ctype.h>
#include <linux/slab.h>
#include "../../include/ktap_types.h"
#include "../../include/ktap_ffi.h"
#include "../ktap.h"
#include "../kp_vm.h"
#include "../kp_obj.h"

static int ffi_type_check(ktap_state_t *ks, csymbol_func *csf, int idx)
{
	StkId arg;
	csymbol *cs;

	if (idx >= csymf_arg_nr(csf))
		return 0;
	arg = kp_arg(ks, idx + 1);
	cs = csymf_arg(ks, csf, idx);

	if (!kp_cdata_type_match(ks, cs, arg))
		return 0;
	else {
		kp_error(ks, "Cannot convert to csymbol %s for arg %d\n",
				csym_name(cs), idx);
		return -1;
	}
}

static csymbol *ffi_get_arg_csym(ktap_state_t *ks, csymbol_func *csf, int idx)
{
	StkId arg;
	csymbol *cs;

	if (idx < csymf_arg_nr(csf))
		return csymf_arg(ks, csf, idx);

	arg = kp_arg(ks, idx + 1);
	cs = id_to_csym(ks, ffi_get_csym_id(ks, "void *"));
	switch (ttypenv(arg)) {
	case KTAP_TYPE_LIGHTUSERDATA:
	case KTAP_TYPE_BOOLEAN:
	case KTAP_TYPE_NUMBER:
	case KTAP_TYPE_STRING:
		return cs;
	case KTAP_TYPE_CDATA:
		return cd_csym(ks, cdvalue(arg));
	default:
		kp_error(ks, "Error: Cannot get type for arg %d\n", idx);
		return cs;
	}
}

static void ffi_unpack(ktap_state_t *ks, char *dst, csymbol_func *csf,
		int idx, int align)
{
	StkId arg = kp_arg(ks, idx + 1);
	csymbol *cs = ffi_get_arg_csym(ks, csf, idx);
	size_t size = csym_size(ks, cs);

	/* initialize the destination section */
	memset(dst, 0, ALIGN(size, align));

	kp_cdata_unpack(ks, dst, cs, arg);
}

#ifdef __x86_64

enum arg_status {
	IN_REGISTER,
	IN_MEMORY,
	IN_STACK,
};

#define ALIGN_STACK(v, a) ((void *)(ALIGN(((uint64_t)v), a)))
#define STACK_ALIGNMENT 8
#define REDZONE_SIZE 128
#define GPR_SIZE (sizeof(void *))
#define MAX_GPR 6
#define MAX_GPR_SIZE (MAX_GPR * GPR_SIZE)
#define NEWSTACK_SIZE 512

#define ffi_call_arch(ks, cf, rvalue) ffi_call_x86_64(ks, cf, rvalue)

extern void ffi_call_assem_x86_64(void *stack, void *temp_stack,
					void *func_addr, void *rvalue, ffi_type rtype);

static void ffi_call_x86_64(ktap_state_t *ks, csymbol_func *csf, void *rvalue)
{
	int i;
	int gpr_nr;
	int arg_bytes; /* total bytes needed for exceeded args in stack */
	int mem_bytes; /* total bytes needed for memory storage */
	char *stack, *stack_p, *gpr_p, *arg_p, *mem_p, *tmp_p;
	int arg_nr;
	csymbol *rsym;
	ffi_type rtype;
	size_t rsize;
	bool ret_in_memory;
	/* New stack to call C function */
	char space[NEWSTACK_SIZE];

	arg_nr = kp_arg_nr(ks);
	rsym = csymf_ret(ks, csf);
	rtype = csym_type(rsym);
	rsize = csym_size(ks, rsym);
	ret_in_memory = false;
	if (rtype == FFI_STRUCT || rtype == FFI_UNION) {
		if (rsize > 16) {
			rvalue = kp_malloc(ks, rsize);
			rtype = FFI_VOID;
			ret_in_memory = true;
		} else {
			/* much easier to always copy 16 bytes from registers */
			rvalue = kp_malloc(ks, 16);
		}
	}

	gpr_nr = 0;
	arg_bytes = mem_bytes = 0;
	if (ret_in_memory)
		gpr_nr++;
	/* calculate bytes needed for stack */
	for (i = 0; i < arg_nr; i++) {
		csymbol *cs = ffi_get_arg_csym(ks, csf, i);
		size_t size = csym_size(ks, cs);
		size_t align = csym_align(ks, cs);
		enum arg_status st = IN_REGISTER;
		int n_gpr_nr = 0;
		if (size > 32) {
			st = IN_MEMORY;
			n_gpr_nr = 1;
		} else if (size > 16)
			st = IN_STACK;
		else
			n_gpr_nr = ALIGN(size, GPR_SIZE) / GPR_SIZE;

		if (gpr_nr + n_gpr_nr > MAX_GPR) {
			if (st == IN_MEMORY)
				arg_bytes += GPR_SIZE;
			else
				st = IN_STACK;
		} else
			gpr_nr += n_gpr_nr;
		if (st == IN_STACK) {
			arg_bytes = ALIGN(arg_bytes, align);
			arg_bytes += size;
			arg_bytes = ALIGN(arg_bytes, STACK_ALIGNMENT);
		}
		if (st == IN_MEMORY) {
			mem_bytes = ALIGN(mem_bytes, align);
			mem_bytes += size;
			mem_bytes = ALIGN(mem_bytes, STACK_ALIGNMENT);
		}
	}

	/* apply space to fake stack for C function call */
	if (16 + REDZONE_SIZE + MAX_GPR_SIZE + arg_bytes +
			mem_bytes + 6 * 8 >= NEWSTACK_SIZE) {
		kp_error(ks, "Unable to handle that many arguments by now\n");
		return;
	}
	stack = space;
	/* 128 bytes below %rsp is red zone */
	/* stack should be 16-bytes aligned */
	stack_p = ALIGN_STACK(stack + REDZONE_SIZE, 16);
	/* save general purpose registers here */
	gpr_p = stack_p;
	memset(gpr_p, 0, MAX_GPR_SIZE);
	/* save arguments in stack here */
	arg_p = gpr_p + MAX_GPR_SIZE;
	/* save arguments in memory here */
	mem_p = arg_p + arg_bytes;
	/* set additional space as temporary space */
	tmp_p = mem_p + mem_bytes;

	/* copy arguments here */
	gpr_nr = 0;
	if (ret_in_memory) {
		memcpy(gpr_p, &rvalue, GPR_SIZE);
		gpr_p += GPR_SIZE;
		gpr_nr++;
	}
	for (i = 0; i < arg_nr; i++) {
		csymbol *cs = ffi_get_arg_csym(ks, csf, i);
		size_t size = csym_size(ks, cs);
		size_t align = csym_align(ks, cs);
		enum arg_status st = IN_REGISTER;
		int n_gpr_nr = 0;
		if (size > 32) {
			st = IN_MEMORY;
			n_gpr_nr = 1;
		} else if (size > 16)
			st = IN_STACK;
		else
			n_gpr_nr = ALIGN(size, GPR_SIZE) / GPR_SIZE;

		if (st == IN_MEMORY)
			mem_p = ALIGN_STACK(mem_p, align);
		/* Tricky way about storing it above mem_p. It won't overflow
		 * because temp region can be temporarily used if necesseary. */
		ffi_unpack(ks, mem_p, csf, i, GPR_SIZE);
		if (gpr_nr + n_gpr_nr > MAX_GPR) {
			if (st == IN_MEMORY) {
				memcpy(arg_p, &mem_p, GPR_SIZE);
				arg_p += GPR_SIZE;
			} else
				st = IN_STACK;
		} else {
			memcpy(gpr_p, mem_p, n_gpr_nr * GPR_SIZE);
			gpr_p += n_gpr_nr * GPR_SIZE;
			gpr_nr += n_gpr_nr;
		}
		if (st == IN_STACK) {
			arg_p = ALIGN_STACK(arg_p, align);
			memcpy(arg_p, mem_p, size);
			arg_p += size;
			arg_p = ALIGN_STACK(arg_p, STACK_ALIGNMENT);
		}
		if (st == IN_MEMORY) {
			mem_p += size;
			mem_p = ALIGN_STACK(mem_p, STACK_ALIGNMENT);
		}
	}

	kp_verbose_printf(ks, "Stack location: %p -redzone- %p -general purpose "
			"register used- %p -zero- %p -stack for argument- %p"
			" -memory for argument- %p -temp stack-\n",
			stack, stack_p, gpr_p, stack_p + MAX_GPR_SIZE,
			arg_p, mem_p);
	kp_verbose_printf(ks, "GPR number: %d; arg in stack: %d; "
			"arg in mem: %d\n",
			gpr_nr, arg_bytes, mem_bytes);
	kp_verbose_printf(ks, "Return: address %p type %d\n", rvalue, rtype);
	kp_verbose_printf(ks, "Number of register used: %d\n", gpr_nr);
	kp_verbose_printf(ks, "Start FFI call on %p\n", csf->addr);
	ffi_call_assem_x86_64(stack_p, tmp_p, csf->addr, rvalue, rtype);
}

#else /* non-supported platform */

#define ffi_call(ks, cf, rvalue) ffi_call_unsupported(ks, cf, rvalue)

static void ffi_call_unsupported(ktap_state_t *ks,
		csymbol_func *csf, void *rvalue)
{
	kp_error(ks, "unsupported architecture.\n");
}

#endif /* end for platform-specific setting */


static int ffi_set_return(ktap_state_t *ks, void *rvalue, csymbol_id ret_id)
{
	ktap_cdata_t *cd;
	ffi_type type = csym_type(id_to_csym(ks, ret_id));

	/* push return value to ktap stack */
	switch (type) {
	case FFI_VOID:
		return 0;
	case FFI_UINT8:
	case FFI_INT8:
	case FFI_UINT16:
	case FFI_INT16:
	case FFI_UINT32:
	case FFI_INT32:
	case FFI_UINT64:
	case FFI_INT64:
		set_number(ks->top, (ktap_number)rvalue);
		break;
	case FFI_PTR:
		cd = kp_cdata_new_ptr(ks, rvalue, -1, ret_id, 0);
		set_cdata(ks->top, cd);
		break;
	case FFI_STRUCT:
	case FFI_UNION:
		cd = kp_cdata_new_record(ks, rvalue, ret_id);
		set_cdata(ks->top, cd);
		break;
	case FFI_FUNC:
	case FFI_UNKNOWN:
		kp_error(ks, "Error: Have not support ffi_type %s\n",
				ffi_type_name(type));
		return 0;
	}
	incr_top(ks);
	return 1;
}

/*
 * Call C into function
 * First argument should be function symbol address, argument types
 * and return type.
 * Left arguments should be arguments for calling the C function.
 * Types between Ktap and C are converted automatically.
 * Only support x86_64 function call by now
 */
int ffi_call(ktap_state_t *ks, csymbol_func *csf)
{
	int i;
	int expected_arg_nr, arg_nr;
	ktap_closure_t *cl;
	void *rvalue;

	expected_arg_nr = csymf_arg_nr(csf);
	arg_nr = kp_arg_nr(ks);

	/* check stack status for C call */
	if (!csf->has_var_arg && expected_arg_nr != arg_nr) {
		kp_error(ks, "wrong argument number %d, which should be %d\n",
				arg_nr, expected_arg_nr);
		goto out;
	}
	if (csf->has_var_arg && expected_arg_nr > arg_nr) {
		kp_error(ks, "argument number %d, which should be bigger than %d\n",
				arg_nr, expected_arg_nr);
		goto out;
	}

	/* maybe useful later, leave it here first */
	cl = clvalue(kp_arg(ks, arg_nr + 1));

	/* check the argument types */
	for (i = 0; i < arg_nr; i++) {
		if (ffi_type_check(ks, csf, i) < 0)
			goto out;
	}

	/* platform-specific calling workflow */
	ffi_call_arch(ks, csf, &rvalue);
	kp_verbose_printf(ks, "Finish FFI call\n");

out:
	return ffi_set_return(ks, rvalue, csymf_ret_id(csf));
}
