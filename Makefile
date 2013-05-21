
LIBDIR = library

LIBOBJS = 
LIBOBJS += $(LIBDIR)/baselib.o $(LIBDIR)/oslib.o $(LIBDIR)/kdebug.o $(LIBDIR)/timer.o


obj-m		+= ktapvm.o
ktapvm-y	:= ktap.o loader.o object.o tstring.o table.o vm.o \
		   opcode.o strfmt.o transport.o $(LIBOBJS)

KVERSION = $(shell uname -r)
all:
	make -C /lib/modules/$(KVERSION)/build M=$(PWD) modules

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
$(UDIR)/opcode.o: opcode.c
	$(QUIET_CC)$(CC) -g -o $@ -c $<
$(UDIR)/table.o: table.c
	$(QUIET_CC)$(CC) -g -o $@ -c $<
$(UDIR)/tstring.o: tstring.c
	$(QUIET_CC)$(CC) -g -o $@ -c $<
$(UDIR)/object.o: object.c
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
	make -C /lib/modules/$(KVERSION)/build M=$(PWD) clean
	rm -rf ktap

