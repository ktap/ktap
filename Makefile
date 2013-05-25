all: mod ktap

INTP = interpreter

LIBDIR = $(INTP)/library

LIB_OBJS = 
LIB_OBJS += $(LIBDIR)/baselib.o $(LIBDIR)/oslib.o $(LIBDIR)/kdebug.o $(LIBDIR)/timer.o

INTP_OBJS = 
INTP_OBJS += $(INTP)/ktap.o $(INTP)/loader.o $(INTP)/object.o $(INTP)/tstring.o \
	    $(INTP)/table.o $(INTP)/vm.o $(INTP)/opcode.o $(INTP)/strfmt.o $(INTP)/transport.o \
	    $(LIB_OBJS)

obj-m		+= ktapvm.o
ktapvm-y	:= $(INTP_OBJS)

KVERSION = $(shell uname -r)
mod:
	$(MAKE) -C /lib/modules/$(KVERSION)/build M=$(PWD) modules

UDIR = userspace

$(UDIR)/lex.o: $(UDIR)/lex.c
	$(QUIET_CC)$(CC) -g -o $@ -c $<
$(UDIR)/parser.o: $(UDIR)/parser.c
	$(QUIET_CC)$(CC) -g -o $@ -c $<
$(UDIR)/code.o: $(UDIR)/code.c
	$(QUIET_CC)$(CC) -g -o $@ -c $<
$(UDIR)/dump.o: $(UDIR)/dump.c
	$(QUIET_CC)$(CC) -g -o $@ -c $<
$(UDIR)/main.o: $(UDIR)/main.c
	$(QUIET_CC)$(CC) -g -o $@ -c $<
$(UDIR)/util.o: $(UDIR)/util.c
	$(QUIET_CC)$(CC) -g -o $@ -c $<
$(UDIR)/ktapio.o: $(UDIR)/ktapio.c
	$(QUIET_CC)$(CC) -g -o $@ -c $<
$(UDIR)/opcode.o: $(INTP)/opcode.c
	$(QUIET_CC)$(CC) -g -o $@ -c $<
$(UDIR)/table.o: $(INTP)/table.c
	$(QUIET_CC)$(CC) -g -o $@ -c $<
$(UDIR)/tstring.o: $(INTP)/tstring.c
	$(QUIET_CC)$(CC) -g -o $@ -c $<
$(UDIR)/object.o: $(INTP)/object.c
	$(QUIET_CC)$(CC) -g -o $@ -c $<

KTAPOBJS =
KTAPOBJS += $(UDIR)/lex.o
KTAPOBJS += $(UDIR)/parser.o
KTAPOBJS += $(UDIR)/code.o
KTAPOBJS += $(UDIR)/dump.o
KTAPOBJS += $(UDIR)/main.o
KTAPOBJS += $(UDIR)/util.o
KTAPOBJS += $(UDIR)/ktapio.o
KTAPOBJS += $(UDIR)/opcode.o
KTAPOBJS += $(UDIR)/table.o
KTAPOBJS += $(UDIR)/tstring.o
KTAPOBJS += $(UDIR)/object.o

ktap: $(KTAPOBJS)
	$(QUIET_LINK)$(CC) -g -O2 -o $@ $(KTAPOBJS) -lpthread

clean:
	$(MAKE) -C /lib/modules/$(KVERSION)/build M=$(PWD) clean
	$(RM) ktap

