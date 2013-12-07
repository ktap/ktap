
#
# Define NO_LIBELF if you do not want libelf dependency (e.g. cross-builds)
# (this will also disable resolve resolving symbols in DSO functionality)
#
# Define FFI if you want to compile ktap with FFI support. By default This
# toggle is off.
#
# Define amalg to enable amalgamation build, This compiles the ktapvm as
# one huge C file and allows GCC to generate faster and shorter code. Alas,
# this requires lots of memory during the build.
# Recommend to use amalgmation build as default.
amalg = 1

# Do not instrument the tracer itself:
ifdef CONFIG_FUNCTION_TRACER
ORIG_CFLAGS := $(KBUILD_CFLAGS)
KBUILD_CFLAGS = $(subst -pg,,$(ORIG_CFLAGS))
endif

all: mod ktap

INC = include
INTP = interpreter


FFIDIR = $(INTP)/ffi
KTAP_LIBS = -lpthread

LIB_OBJS += $(INTP)/lib_base.o $(INTP)/lib_kdebug.o $(INTP)/lib_timer.o \
		$(INTP)/lib_ansi.o

ifndef amalg
ifdef FFI
FFI_OBJS += $(FFIDIR)/ffi_call.o $(FFIDIR)/ffi_type.o $(FFIDIR)/ffi_symbol.o \
    $(FFIDIR)/cdata.o $(FFIDIR)/ffi_util.o
INTP_OBJS += $(FFI_OBJS)
LIB_OBJS += $(INTP)/lib_ffi.o
endif
INTP_OBJS += $(INTP)/ktap.o $(INTP)/kp_load.o $(INTP)/kp_obj.o \
		$(INTP)/kp_str.o $(INTP)/kp_tab.o $(INTP)/kp_vm.o \
		$(INTP)/kp_opcode.o $(INTP)/kp_transport.o \
		$(LIB_OBJS)
else
INTP_OBJS += $(INTP)/kp_amalg.o
endif

ifdef FFI
ifeq ($(KBUILD_MODULES), 1)
ifdef CONFIG_X86_64
# call_x86_64.o is compiled from call_x86_64.S
INTP_OBJS += $(FFIDIR)/call_x86_64.o
else
$(error ktap FFI only supports x86_64 for now!)
endif
endif


ccflags-y	+= -DCONFIG_KTAP_FFI
endif

obj-m		+= ktapvm.o
ktapvm-y	:= $(INTP_OBJS)

KVERSION ?= $(shell uname -r)
KERNEL_SRC ?= /lib/modules/$(KVERSION)/build
PWD := $(shell pwd)
mod:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) modules

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) modules_install

KTAPC_CFLAGS = -Wall -O2


# try-cc
# Usage: option = $(call try-cc, source-to-build, cc-options, msg)
ifneq ($(V),1)
TRY_CC_OUTPUT= > /dev/null 2>&1
endif
TRY_CC_MSG=echo "    CHK $(3)" 1>&2;

try-cc = $(shell sh -c							\
         'TMP="/tmp/.$$$$";						\
          $(TRY_CC_MSG)							\
          echo "$(1)" |							\
          $(CC) -x c - $(2) -o "$$TMP" $(TRY_CC_OUTPUT) && echo y;	\
          rm -f "$$TMP"')


define SOURCE_LIBELF
#include <libelf.h>

int main(void)
{
        Elf *elf = elf_begin(0, ELF_C_READ, 0);
        return (long)elf;
}
endef

FLAGS_LIBELF = -lelf

ifdef NO_LIBELF
	KTAPC_CFLAGS += -DNO_LIBELF
else
ifneq ($(call try-cc,$(SOURCE_LIBELF),$(FLAGS_LIBELF),libelf),y)
    $(warning No libelf found, disables symbol resolving, please install elfutils-libelf-devel/libelf-dev);
    NO_LIBELF := 1
    KTAPC_CFLAGS += -DNO_LIBELF
else
    KTAP_LIBS += -lelf
endif
endif

UDIR = userspace

$(UDIR)/lex.o: $(UDIR)/lex.c $(INC)/*
	$(QUIET_CC)$(CC) $(DEBUGINFO_FLAG) $(KTAPC_CFLAGS) -o $@ -c $<
$(UDIR)/parser.o: $(UDIR)/parser.c $(INC)/*
	$(QUIET_CC)$(CC) $(DEBUGINFO_FLAG) $(KTAPC_CFLAGS) -o $@ -c $<
$(UDIR)/code.o: $(UDIR)/code.c $(INC)/*
	$(QUIET_CC)$(CC) $(DEBUGINFO_FLAG) $(KTAPC_CFLAGS) -o $@ -c $<
$(UDIR)/dump.o: $(UDIR)/dump.c $(INC)/*
	$(QUIET_CC)$(CC) $(DEBUGINFO_FLAG) $(KTAPC_CFLAGS) -o $@ -c $<
$(UDIR)/main.o: $(UDIR)/main.c $(INC)/*
	$(QUIET_CC)$(CC) $(DEBUGINFO_FLAG) $(KTAPC_CFLAGS) -o $@ -c $<
$(UDIR)/util.o: $(UDIR)/util.c $(INC)/*
	$(QUIET_CC)$(CC) $(DEBUGINFO_FLAG) $(KTAPC_CFLAGS) -o $@ -c $<
$(UDIR)/ktapio.o: $(UDIR)/ktapio.c $(INC)/*
	$(QUIET_CC)$(CC) $(DEBUGINFO_FLAG) $(KTAPC_CFLAGS) -o $@ -c $<
$(UDIR)/eventdef.o: $(UDIR)/eventdef.c $(INC)/*
	$(QUIET_CC)$(CC) $(DEBUGINFO_FLAG) $(KTAPC_CFLAGS) -o $@ -c $<
$(UDIR)/kp_opcode.o: $(INTP)/kp_opcode.c $(INC)/*
	$(QUIET_CC)$(CC) $(DEBUGINFO_FLAG) $(KTAPC_CFLAGS) -o $@ -c $<
$(UDIR)/kp_tab.o: $(INTP)/kp_tab.c $(INC)/*
	$(QUIET_CC)$(CC) $(DEBUGINFO_FLAG) $(KTAPC_CFLAGS) -o $@ -c $<
$(UDIR)/kp_str.o: $(INTP)/kp_str.c $(INC)/*
	$(QUIET_CC)$(CC) $(DEBUGINFO_FLAG) $(KTAPC_CFLAGS) -o $@ -c $<
$(UDIR)/kp_obj.o: $(INTP)/kp_obj.c $(INC)/*
	$(QUIET_CC)$(CC) $(DEBUGINFO_FLAG) $(KTAPC_CFLAGS) -o $@ -c $<
ifndef NO_LIBELF
$(UDIR)/symbol.o: $(UDIR)/symbol.c
	$(QUIET_CC)$(CC) $(DEBUGINFO_FLAG) $(KTAPC_CFLAGS) -o $@ -c $<
endif
ifdef FFI
KTAPC_CFLAGS += -DCONFIG_KTAP_FFI
$(UDIR)/ffi_type.o: $(INTP)/ffi/ffi_type.c $(INC)/*
	$(QUIET_CC)$(CC) $(DEBUGINFO_FLAG) $(KTAPC_CFLAGS) -o $@ -c $<
$(UDIR)/ffi/cparser.o: $(UDIR)/ffi/cparser.c $(INC)/*
	$(QUIET_CC)$(CC) $(DEBUGINFO_FLAG) $(KTAPC_CFLAGS) -o $@ -c $<
$(UDIR)/ffi/ctype.o: $(UDIR)/ffi/ctype.c $(INC)/*
	$(QUIET_CC)$(CC) $(DEBUGINFO_FLAG) $(KTAPC_CFLAGS) -o $@ -c $<
endif


KTAPOBJS =
KTAPOBJS += $(UDIR)/lex.o
KTAPOBJS += $(UDIR)/parser.o
KTAPOBJS += $(UDIR)/code.o
KTAPOBJS += $(UDIR)/dump.o
KTAPOBJS += $(UDIR)/main.o
KTAPOBJS += $(UDIR)/util.o
KTAPOBJS += $(UDIR)/ktapio.o
KTAPOBJS += $(UDIR)/eventdef.o
KTAPOBJS += $(UDIR)/kp_opcode.o
KTAPOBJS += $(UDIR)/kp_tab.o
KTAPOBJS += $(UDIR)/kp_str.o
KTAPOBJS += $(UDIR)/kp_obj.o
ifndef NO_LIBELF
KTAPOBJS += $(UDIR)/symbol.o
endif
ifdef FFI
KTAPOBJS += $(UDIR)/ffi_type.o
KTAPOBJS += $(UDIR)/ffi/cparser.o
KTAPOBJS += $(UDIR)/ffi/ctype.o
endif

ktap: $(KTAPOBJS)
	$(QUIET_LINK)$(CC) $(KTAPC_CFLAGS) -o $@ $(KTAPOBJS) $(KTAP_LIBS)

KMISC := /lib/modules/$(KVERSION)/ktapvm/

install: mod ktap
	install -d $(KMISC)
	install -m 644 -c *.ko /lib/modules/$(KVERSION)/ktapvm/
	/sbin/depmod -a

load:
	insmod ktapvm.ko

unload:
	rmmod ktapvm

test: FORCE
	cd test; sh ./run_test.sh; cd -

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) clean
	$(RM) ktap

PHONY += FORCE
FORCE:

