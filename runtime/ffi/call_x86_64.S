/*
 * call_x86_64.S - assembly code to call C function and handle return value
 *
 * This file is part of ktap by Jovi Zhangwei
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


#ifdef __x86_64

	.file "call_x86_64.S"
	.text

/*	ffi_call_assem_x86_64(void *stack, void *temp_stack,
 *		void *rvalue, void *func_addr, ffi_type rftype)
 *	@stack: base address of register values and new stack
 *	@temp_stack: stack to store temporary values
 *	@func_addr: Function address
 *	@rvalue: where to put return value
 *	@rftype: FFI type of return value
 */
	.align 2
	.globl	ffi_call_assem_x86_64
	.type	ffi_call_assem_x86_64,@function

ffi_call_assem_x86_64:
	movq	(%rsp), %rax	/* save return address */
	/* move stuffs to temp memory region(void *temp_stack) */
	movq	%rcx, (%rsi)	/* save pointer to return value */
	movq	%r8, 8(%rsi)	/* save return_ffi_type */
	movq	%rbp, 16(%rsi)	/* save %rbp */
	movq	%rax, 24(%rsi)	/* save return address */
	movq	%rsp, 32(%rsi)	/* save %rsp */
	movq	%rsi, %rbp	/* point %rbp to temp memory region */

	movq	%rdx, %r11	/* move function address to %r11 */

	movq	%rdi, %r10	/* set %r10 point to register region */
	movq	(%r10), %rdi	/* load registers */
	movq	8(%r10), %rsi
	movq	16(%r10), %rdx
	movq	24(%r10), %rcx
	movq	32(%r10), %r8
	movq	40(%r10), %r9
	xorq	%rax, %rax

	leaq	48(%r10), %rsp

	callq	*%r11

	movq	32(%rbp), %rsp	/* restore %rsp */
	movq	24(%rbp), %rcx	/* restore return address */
	movq	%rcx, (%rsp)

	movq	(%rbp), %rcx	/* get pointer to return value */
	movq	8(%rbp), %r8	/* get return_ffi_type */
	movq	16(%rbp), %rbp	/* restore rbp */

	leaq	.Lreturn_table(%rip), %r11	/* start address of return_table */
	movslq	(%r11, %r8, 8), %r11	/* fetch target address from table */
	jmpq	*%r11			/* jump according to value in table */

	.align 8
.Lreturn_table:
	.quad	.Lreturn_void		/* FFI_VOID */
	.quad	.Lreturn_uint8		/* FFI_UINT8 */
	.quad	.Lreturn_int8		/* FFI_INT8 */
	.quad	.Lreturn_uint16		/* FFI_UINT16 */
	.quad	.Lreturn_int16		/* FFI_INT16 */
	.quad	.Lreturn_uint32		/* FFI_UINT32 */
	.quad	.Lreturn_int32		/* FFI_INT32 */
	.quad	.Lreturn_uint64		/* FFI_UINT64 */
	.quad	.Lreturn_int64		/* FFI_INT64 */
	.quad	.Lreturn_ptr		/* FFI_PTR */
	.quad	.Lreturn_func		/* FFI_FUNC */
	.quad	.Lreturn_struct		/* FFI_STRUCT */
	.quad	.Lreturn_unknown	/* FFI_UNKNOWN */

	.align 8
.Lreturn_void:
.Lreturn_func:
.Lreturn_unknown:
	retq
	.align 8
.Lreturn_uint8:
	movzbq	%al, %rax
	movq	%rax, (%rcx)
	retq
	.align 8
.Lreturn_int8:
	movsbq	%al, %rax
	movq	%rax, (%rcx)
	retq
	.align 8
.Lreturn_uint16:
	movzwq	%ax, %rax
	movq	%rax, (%rcx)
	retq
	.align 8
.Lreturn_int16:
	movswq	%ax, %rax
	movq	%rax, (%rcx)
	retq
	.align 8
.Lreturn_uint32:
	movl	%eax, %eax
	movq	%rax, (%rcx)
	retq
	.align 8
.Lreturn_int32:
	movslq	%eax, %rax
	movq	%rax, (%rcx)
	retq
	.align 8
.Lreturn_uint64:
.Lreturn_int64:
.Lreturn_ptr:
	movq	%rax, (%rcx)
	retq
/* Struct type indicates that struct is put into at most two registers,
 * and 16 bytes space is always available
 */
	.align 8
.Lreturn_struct:
	movq	%rax, (%rcx)
	movq	%rdx, 8(%rcx)
	retq

#endif /* end for __x86_64 */
