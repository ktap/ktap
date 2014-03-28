#!/bin/sh

# This script compare stack profiling performance between ktap and stap.
#
# 1. ktap -e 'profile-1000us { s[stack(-1, 12)] += 1 }'
# 2. stap -e 'probe timer.profile { s[backtrace()] += 1 }'
# 3. stap -e 'probe timer.profile { s[backtrace()] <<< 1 }'

#Result:
#Currently the stack profiling overhead is nearly same between ktap and stap.
#
#ktap reslove kernel stack to string in runtime, which is very time consuming,
#optimize it in future.


gcc -o sembench sembench.c -O2 -lpthread

COMMAND="./sembench -t 200 -w 20 -r 30 -o 2"

#------------------------------------------------------------#

echo -e "without tracing:"
$COMMAND; $COMMAND; $COMMAND

#------------------------------------------------------------#

../../ktap -q -e 'var s = table.new(0, 20000) profile-1000us { s[stack(-1, 12)] += 1 }' &

echo -e "\nktap tracing: profile-1000us { s[stack(-1, 12)] += 1 }"
$COMMAND; $COMMAND; $COMMAND
pid=`pidof ktap`
disown $pid; kill -9 $pid; sleep 1

#------------------------------------------------------------#

stap -o /dev/null -e 'global s[20000]; probe timer.profile { s[backtrace()] += 1 }' &

echo -e "\nstap tracing: probe timer.profile { s[backtrace()] += 1 }"
$COMMAND; $COMMAND; $COMMAND
pkill stap

#------------------------------------------------------------#

stap -o /dev/null -e 'global s[20000]; probe timer.profile { s[backtrace()] <<< 1 }' &

echo -e "\nstap tracing: probe timer.profile { s[backtrace()] <<< 1 }"
$COMMAND; $COMMAND; $COMMAND
pkill stap

#------------------------------------------------------------#


rm -rf ./sembench

