#!/bin/bash

machine_type=`getconf LONG_BIT`

if [ $machine_type -eq 64 ]; then
    echo "perf probe -x /lib64/libc.so.6 malloc"
    perf probe -x /lib64/libc.so.6 malloc
elif [ $machine_type -eq 32 ]; then
    echo "perf probe -x /lib/libc.so.6 malloc"
    perf probe -x /lib/libc.so.6 malloc
fi

if [ $? -ne 0 ]; then
	exit
fi

id=`cat /sys/kernel/debug/tracing/events/probe_libc/malloc/id`

../ktap probe_by_id.kp $id

perf probe --del probe_libc:malloc

