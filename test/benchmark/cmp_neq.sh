#!/bin/sh

# This script compare number equality performance between ktap and stap.
# It also compare different ktap tracing interfaces.
#
# 1. ktap -e 'trace syscalls:sys_enter_futex {}'
# 2. ktap -e 'kdebug.tracepoint("sys_enter_futex", function () {})'
# 3. ktap -e 'trace probe:SyS_futex uaddr=%di {}'
# 4. ktap -e 'kdebug.kprobe("SyS_futex", function () {})'
# 5. stap -e 'probe syscall.futex {}'
# 6. ktap -d -e 'trace syscalls:sys_enter_futex {}'
# 7. ktap -d -e 'kdebug.tracepoint("sys_enter_futex", function () {})'
# 8. ktap -e 'trace syscalls:sys_enter_futex /kernel_buildin_filter/ {}'

#Result:
#ktap number computation and comparsion overhead is bigger than stap,
#nearly 10+% (4 vs. 5 in above)), ktap is not very slow.
#
#Perf backend tracing overhead is big, because it need copy temp buffer, and
#code path is very long than direct callback(1 vs. 4 in above).

gcc -o sembench sembench.c -O2 -lpthread

COMMAND="./sembench -t 200 -w 20 -r 30 -o 2"

#------------------------------------------------------------#

echo -e "without tracing:"
#$COMMAND; $COMMAND; $COMMAND

#------------------------------------------------------------#

../../ktap -q -e 'trace syscalls:sys_enter_futex {
	var uaddr = arg2
        if (uaddr == 0x100 || uaddr == 0x200 || uaddr == 0x300 ||
	    uaddr == 0x400 || uaddr == 0x500 || uaddr == 0x600 ||
	    uaddr == 0x700 || uaddr == 0x800 || uaddr == 0x900 ||
	    uaddr == 0x1000) {
                printf("%x %x\n", arg1, arg2)
        }}' &

echo -e "\nktap tracing: trace syscalls:sys_enter_futex { if (arg2 == 0x100 || arg2 == 0x200 ... }"
$COMMAND; $COMMAND; $COMMAND
pid=`pidof ktap`
disown $pid; kill -9 $pid; sleep 1

#------------------------------------------------------------#

../../ktap -q -e 'kdebug.tracepoint("sys_enter_futex", function () {
	var arg = arg2
        if (arg == 0x100 || arg == 0x200 || arg == 0x300 || arg == 0x400 ||
            arg == 0x500 || arg == 0x600 || arg == 0x700 || arg == 0x800 ||
            arg == 0x900 || arg == 0x1000) {
                printf("%x %x\n", arg1, arg2)
        }})' &

echo -e '\nktap tracing: kdebug.tracepoint("sys_enter_futex", function (xxx) {})'
$COMMAND; $COMMAND; $COMMAND
pid=`pidof ktap`
disown $pid; kill -9 $pid; sleep 1

#------------------------------------------------------------#

../../ktap -q -e 'trace probe:SyS_futex uaddr=%di {
	var arg = arg1
        if (arg == 0x100 || arg == 0x200 || arg == 0x300 || arg == 0x400 ||
            arg == 0x500 || arg == 0x600 || arg == 0x700 || arg == 0x800 ||
            arg == 0x900 || arg == 0x1000) {
                printf("%x\n", arg1)
        }}' &
echo -e '\nktap tracing: trace probe:SyS_futex uaddr=%di {...}'
$COMMAND; $COMMAND; $COMMAND
pid=`pidof ktap`
disown $pid; kill -9 $pid; sleep 1


#------------------------------------------------------------#
../../ktap -q -e 'kdebug.kprobe("SyS_futex", function () {
	var uaddr = 1
        if (uaddr == 0x100 || uaddr == 0x200 || uaddr == 0x300 ||
	    uaddr == 0x400 || uaddr == 0x500 || uaddr == 0x600 ||
	    uaddr == 0x700 || uaddr == 0x800 || uaddr == 0x900 ||
	    uaddr == 0x1000) {
                printf("%x\n", uaddr)
	}})' &
echo -e '\nktap tracing: kdebug.kprobe("SyS_futex", function () {})'
$COMMAND; $COMMAND; $COMMAND
pid=`pidof ktap`
disown $pid; kill -9 $pid; sleep 1

#------------------------------------------------------------#

stap -e 'probe syscall.futex {
	uaddr = $uaddr
        if (uaddr == 0x100 || uaddr == 0x200 || uaddr == 0x300 ||
	    uaddr == 0x400 || uaddr == 0x500 || uaddr == 0x600 ||
	    uaddr == 0x700 || uaddr == 0x800 || uaddr == 0x900 ||
	    uaddr == 0x1000) {
                printf("%x\n", uaddr)
        }}' &

echo -e "\nstap tracing: probe syscall.futex { if (uaddr == 0x100 || addr == 0x200 ... }"
$COMMAND; $COMMAND; $COMMAND
pid=`pidof stap`
disown $pid; kill -9 $pid; sleep 1

#------------------------------------------------------------#


../../ktap -d -q -e 'trace syscalls:sys_enter_futex {
	var uaddr = arg2
        if (uaddr == 0x100 || uaddr == 0x200 || uaddr == 0x300 ||
	    uaddr == 0x400 || uaddr == 0x500 || uaddr == 0x600 ||
	    uaddr == 0x700 || uaddr == 0x800 || uaddr == 0x900 ||
	    uaddr == 0x1000) {
                printf("%x %x\n", arg1, arg2)
        }}' &

echo -e "\nktap tracing dry-run: trace syscalls:sys_enter_futex { if (arg2 == 0x100 || arg2 == 0x200 ... }"
$COMMAND; $COMMAND; $COMMAND
pid=`pidof ktap`
disown $pid; kill -9 $pid; sleep 1


#------------------------------------------------------------#

../../ktap -d -q -e 'kdebug.tracepoint("sys_enter_futex", function () {
	var arg = arg2
        if (arg == 0x100 || arg == 0x200 || arg == 0x300 || arg == 0x400 ||
            arg == 0x500 || arg == 0x600 || arg == 0x700 || arg == 0x800 ||
            arg == 0x900 || arg == 0x1000) {
                printf("%x %x\n", arg1, arg2)
        }})' &

echo -e '\nktap tracing dry-run: kdebug.tracepoint("sys_enter_futex", function (xxx) {})'
$COMMAND; $COMMAND; $COMMAND
pid=`pidof ktap`
disown $pid; kill -9 $pid; sleep 1


#------------------------------------------------------------#

../../ktap -q -e 'trace syscalls:sys_enter_futex /
	uaddr == 0x100 || uaddr == 0x200 || uaddr == 0x300 || uaddr == 0x400 ||
	uaddr == 0x500 || uaddr == 0x600 || uaddr == 0x700 || uaddr == 0x800 ||
	uaddr == 0x900 || uaddr == 0x1000/ {
		printf("%x %x\n", arg1, arg2)
	}' &

echo -e "\nktap tracing: trace syscalls:sys_enter_futex /uaddr == 0x100 || uaddr == 0x200 .../ {}"
$COMMAND; $COMMAND; $COMMAND
pid=`pidof ktap`
disown $pid; kill -9 $pid; sleep 1

#------------------------------------------------------------#

rm -rf ./sembench

