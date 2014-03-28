#!/bin/sh

gcc -o sembench sembench.c -O2 -lpthread

COMMAND="./sembench -t 200 -w 20 -r 30 -o 2"

#------------------------------------------------------------#

echo -e "without tracing:"
#$COMMAND
#$COMMAND
#$COMMAND

#------------------------------------------------------------#

../../ktap -d -q -e 'trace syscalls:sys_enter_futex {}' &
echo -e "\nktap tracing in dry-run mode: trace syscalls:sys_enter_futex {}"
#$COMMAND
#$COMMAND
#$COMMAND
pid=`pidof ktap`
disown $pid; kill -9 $pid; sleep 1

#------------------------------------------------------------#

../../ktap -q -e 'trace syscalls:sys_enter_futex {}' &
echo -e "\nktap tracing: trace syscalls:sys_enter_futex {}"
#$COMMAND
#$COMMAND
#$COMMAND
pid=`pidof ktap`
disown $pid; kill -9 $pid; sleep 1

#------------------------------------------------------------#

../../ktap -q -e 'kdebug.tracepoint("sys_enter_futex", function () {})' &
echo -e '\nktap tracing: kdebug.tracepoint("sys_enter_futex", function () {})'
$COMMAND
$COMMAND
$COMMAND
pid=`pidof ktap`
disown $pid; kill -9 $pid; sleep 1

#------------------------------------------------------------#

../../ktap -q -e 'kdebug.tracepoint("sys_enter_futex", function () {
	var arg = 1
        if (arg == 0x100 || arg == 0x200 || arg == 0x300 || arg == 0x400 ||
            arg == 0x500 || arg == 0x600 || arg == 0x700 || arg == 0x800 ||
            arg == 0x900 || arg == 0x1000) {
                printf("%x %x\n", arg1, arg2)
        }}' &

echo -e '\nktap tracing: kdebug.tracepoint("sys_enter_futex", function (xxx) {})'
$COMMAND
$COMMAND
$COMMAND
pid=`pidof ktap`
disown $pid; kill -9 $pid; sleep 1


#------------------------------------------------------------#

perf record -e syscalls:sys_enter_futex -a &
echo -e "\nperf tracing: perf record -e syscalls:sys_enter_futex -a"
$COMMAND
$COMMAND
$COMMAND
pid=`pidof perf`
disown $pid; kill -9 $pid; sleep 1; rm -rf perf.data

#------------------------------------------------------------#

#lttng cannot compiled successful in my box.

#lttng create
#lttng enable-event -k syscalls:sys_enter_futex &
#echo -e "\nlttng tracing: lttng enable-event -k syscalls:sys_enter_futex"
#$COMMAND
#$COMMAND
#$COMMAND
#pid=`pidof perf`
#disown $pid; kill -9 $pid; sleep 1;
#lttng destroy



#------------------------------------------------------------#

../../ktap -q -e 'trace syscalls:sys_enter_futex {
	if (arg2 == 0x100 || arg2 == 0x200 || arg2 == 0x300 || arg2 == 0x400 ||
	    arg2 == 0x500 || arg2 == 0x600 || arg2 == 0x700 || arg2 == 0x800 ||
	    arg2 == 0x900 || arg2 == 0x1000) {
		printf("%x %x\n", arg1, arg2)
	}}' &

echo -e "\nktap tracing: trace syscalls:sys_enter_futex { if (arg2 == 0x100 || arg2 == 0x200 ... }"
$COMMAND
$COMMAND
$COMMAND
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
$COMMAND
$COMMAND
$COMMAND
pid=`pidof ktap`
disown $pid; kill -9 $pid; sleep 1

#------------------------------------------------------------#
#lttng don't support kerenl event filter now
#------------------------------------------------------------#

#------------------------------------------------------------#
#systemtap compiled failure in my box
#------------------------------------------------------------#

rm -rf ./sembench
