#!/bin/sh

# This script compare table performance between ktap and stap.
#
# 1. ktap -e 'trace syscalls:sys_enter_futex { s[execname] += 1 }'
# 2. ktap -e 'kdebug.tracepoint("sys_enter_futex", function () { s[execname] += 1 })'
# 3. ktap -e 'kdebug.kprobe("SyS_futex", function () { s[execname] += 1 })'
# 4. stap -e 'probe syscall.futex { s[execname()] += 1 }'
# 5. ktap -e 'kdebug.kprobe("SyS_futex", function () { s[probename] += 1 })'
# 6. stap -e 'probe syscall.futex { s[name] += 1 }'
# 7. ktap -e 'kdebug.kprobe("SyS_futex", function () { s["constant_string_key"] += 1 })'
# 8. stap -e 'probe syscall.futex { s["constant_string_key"] += 1 }'

#Result:
#Currently ktap table operation overhead is smaller than stap.


gcc -o sembench sembench.c -O2 -lpthread

COMMAND="./sembench -t 200 -w 20 -r 30 -o 2"

#------------------------------------------------------------#

echo -e "without tracing:"
$COMMAND; $COMMAND; $COMMAND

#------------------------------------------------------------#

../../ktap -q -e 'var s = {} trace syscalls:sys_enter_futex { s[execname] += 1 }' &

echo -e "\nktap tracing: trace syscalls:sys_enter_futex { s[execname] += 1 }"
$COMMAND; $COMMAND; $COMMAND
pid=`pidof ktap`
disown $pid; kill -9 $pid; sleep 1

#------------------------------------------------------------#

../../ktap -q -e 'var s = {} kdebug.tracepoint("sys_enter_futex", function () {
	s[execname] += 1 })' &

echo -e '\nktap tracing: kdebug.tracepoint("sys_enter_futex", function () { s[execname] += 1})'
$COMMAND; $COMMAND; $COMMAND
pid=`pidof ktap`
disown $pid; kill -9 $pid; sleep 1

#------------------------------------------------------------#

../../ktap -q -e 'var s = {} kdebug.kprobe("SyS_futex", function () {
	s[execname] += 1 })' &

echo -e '\nktap tracing: kdebug.kprobe("SyS_futex", function () { s[execname] += 1 })'
$COMMAND; $COMMAND; $COMMAND
pid=`pidof ktap`
disown $pid; kill -9 $pid; sleep 1

#------------------------------------------------------------#

stap -e 'global s; probe syscall.futex { s[execname()] += 1 }' &

echo -e "\nstap tracing: probe syscall.futex { s[execname()] += 1 }"
$COMMAND; $COMMAND; $COMMAND
pkill stap

#------------------------------------------------------------#

../../ktap -q -e 'var s = {} kdebug.kprobe("SyS_futex", function () {
	s[probename] += 1 })' &

echo -e '\nktap tracing: kdebug.kprobe("SyS_futex", function () { s[probename] += 1 })'
$COMMAND; $COMMAND; $COMMAND
pid=`pidof ktap`
disown $pid; kill -9 $pid; sleep 1

#------------------------------------------------------------#

stap -e 'global s; probe syscall.futex { s[name] += 1 }' &

echo -e "\nstap tracing: probe syscall.futex { s[name] += 1 }"
$COMMAND; $COMMAND; $COMMAND
pkill stap

#------------------------------------------------------------#

../../ktap -q -e 'var s = {} s["const_string_key"] = 0 kdebug.kprobe("SyS_futex", function () {
	s["const_string_key"] += 1 })' &

echo -e '\nktap tracing: kdebug.kprobe("SyS_futex", function () { s["const_string_key"] += 1 })'
$COMMAND; $COMMAND; $COMMAND
pid=`pidof ktap`
disown $pid; kill -9 $pid; sleep 1

#------------------------------------------------------------#

stap -e 'global s; probe syscall.futex { s["const_string_key"] += 1 }' &

echo -e "\nstap tracing: probe syscall.futex { s["const_string_key"] += 1 }"
$COMMAND; $COMMAND; $COMMAND
pkill stap

#------------------------------------------------------------#

stap -o /dev/null -e 'global s; probe syscall.futex { s["const_string_key"] <<< 1 }' &

echo -e "\nstap tracing: probe syscall.futex { s["const_string_key"] <<< 1 }"
$COMMAND; $COMMAND; $COMMAND
pkill stap

#------------------------------------------------------------#


rm -rf ./sembench

