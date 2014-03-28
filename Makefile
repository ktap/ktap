
#
# Define NO_LIBELF if you do not want libelf dependency (e.g. cross-builds)
# (this will also disable resolve resolving symbols in DSO functionality)
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
RUNTIME = runtime

KTAP_LIBS = -lpthread

LIB_OBJS += $(RUNTIME)/lib_base.o $(RUNTIME)/lib_kdebug.o \
		$(RUNTIME)/lib_timer.o $(RUNTIME)/lib_ansi.o \
		$(RUNTIME)/lib_table.o $(RUNTIME)/lib_net.o

ifndef amalg
RUNTIME_OBJS += $(RUNTIME)/ktap.o $(RUNTIME)/kp_bcread.o $(RUNTIME)/kp_obj.o \
		$(RUNTIME)/kp_str.o $(RUNTIME)/kp_mempool.o \
		$(RUNTIME)/kp_tab.o $(RUNTIME)/kp_vm.o \
		$(RUNTIME)/kp_transport.o $(RUNTIME)/kp_events.o $(LIB_OBJS)
else
RUNTIME_OBJS += $(RUNTIME)/amalg.o
endif

obj-m		+= ktapvm.o
ktapvm-y	:= $(RUNTIME_OBJS)

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

$(UDIR)/kp_main.o: $(UDIR)/kp_main.c $(INC)/* KTAP-CFLAGS
	$(QUIET_CC)$(CC) $(DEBUGINFO_FLAG) $(KTAPC_CFLAGS) -o $@ -c $<
$(UDIR)/kp_lex.o: $(UDIR)/kp_lex.c $(INC)/* KTAP-CFLAGS
	$(QUIET_CC)$(CC) $(DEBUGINFO_FLAG) $(KTAPC_CFLAGS) -o $@ -c $<
$(UDIR)/kp_parse.o: $(UDIR)/kp_parse.c $(INC)/* KTAP-CFLAGS
	$(QUIET_CC)$(CC) $(DEBUGINFO_FLAG) $(KTAPC_CFLAGS) -o $@ -c $<
$(UDIR)/kp_bcwrite.o: $(UDIR)/kp_bcwrite.c $(INC)/* KTAP-CFLAGS
	$(QUIET_CC)$(CC) $(DEBUGINFO_FLAG) $(KTAPC_CFLAGS) -o $@ -c $<
$(UDIR)/kp_reader.o: $(UDIR)/kp_reader.c $(INC)/* KTAP-CFLAGS
	$(QUIET_CC)$(CC) $(DEBUGINFO_FLAG) $(KTAPC_CFLAGS) -o $@ -c $<
$(UDIR)/kp_util.o: $(UDIR)/kp_util.c $(INC)/* KTAP-CFLAGS
	$(QUIET_CC)$(CC) $(DEBUGINFO_FLAG) $(KTAPC_CFLAGS) -o $@ -c $<
$(UDIR)/kp_parse_events.o: $(UDIR)/kp_parse_events.c $(INC)/* KTAP-CFLAGS
	$(QUIET_CC)$(CC) $(DEBUGINFO_FLAG) $(KTAPC_CFLAGS) -o $@ -c $<
ifndef NO_LIBELF
$(UDIR)/kp_symbol.o: $(UDIR)/kp_symbol.c KTAP-CFLAGS
	$(QUIET_CC)$(CC) $(DEBUGINFO_FLAG) $(KTAPC_CFLAGS) -o $@ -c $<
endif

KTAPOBJS =
KTAPOBJS += $(UDIR)/kp_main.o
KTAPOBJS += $(UDIR)/kp_lex.o
KTAPOBJS += $(UDIR)/kp_parse.o
KTAPOBJS += $(UDIR)/kp_bcwrite.o
KTAPOBJS += $(UDIR)/kp_reader.o
KTAPOBJS += $(UDIR)/kp_util.o
KTAPOBJS += $(UDIR)/kp_parse_events.o
ifndef NO_LIBELF
KTAPOBJS += $(UDIR)/kp_symbol.o
endif

ktap: $(KTAPOBJS) KTAP-CFLAGS
	$(QUIET_LINK)$(CC) $(KTAPC_CFLAGS) -o $@ $(KTAPOBJS) $(KTAP_LIBS)

KMISC := /lib/modules/$(KVERSION)/ktapvm/

install: mod ktap
	make modules_install ktapvm.ko
	install -c ktap /usr/bin/
	mkdir -p ~/.vim/ftdetect
	mkdir -p ~/.vim/syntax
	cp vim/ftdetect/ktap.vim ~/.vim/ftdetect/
	cp vim/syntax/ktap.vim ~/.vim/syntax/

load:
	insmod ktapvm.ko

unload:
	rmmod ktapvm

reload:
	make unload; make load

test: FORCE
	#start testing
	prove -j4 -r test/

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) clean
	$(RM) ktap KTAP-CFLAGS


PHONY += FORCE
FORCE:

TRACK_FLAGS = KTAP
ifdef amalg
TRACK_FLAGS += AMALG
endif
ifdef NO_LIBELF
TRACK_FLAGS += NO_LIBELF
endif

KTAP-CFLAGS: FORCE
	@FLAGS='$(TRACK_FLAGS)'; \
	if test x"$$FLAGS" != x"`cat KTAP-CFLAGS 2>/dev/null`" ; then \
		echo "$$FLAGS" >KTAP-CFLAGS; \
	fi

#generate tags/etags/cscope index for editor.
define all_sources
        (find . -name '*.[ch]' -print)
endef

.PHONY: tags
tags:
	$(all_sources) | xargs ctags

.PHONY: etags
etags:
	$(all_sources) | xargs etags

.PHONY: cscope
cscope:
	$(all_sources) > cscope.files
	cscope -k -b
