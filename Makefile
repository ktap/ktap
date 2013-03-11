
LIBDIR = library

obj-m		+= ktapvm.o
ktapvm-y	:= ktap.o loader.o object.o $(LIBDIR)/baselib.o $(LIBDIR)/oslib.o tstring.o table.o vm.o \
			$(LIBDIR)/syscalls.o $(LIBDIR)/trace.o opcode.o strfmt.o transport.o
all:
	make -C ../../.. M=`pwd` modules

UDIR = userspace
ktap: 
	gcc -g -O2 -o ktap $(UDIR)/lex.c $(UDIR)/parser.c $(UDIR)/code.c  \
		$(UDIR)/dump.c $(UDIR)/main.c $(UDIR)/util.c $(UDIR)/ktapio.c \
		opcode.c table.c tstring.c object.c -lpthread

clean:
	rm -rf ktapvm ktap *.o library/*.o library/.*.o.cmd *.out *.ko *.mod.c *.order *.o.cmd Module.symvers

