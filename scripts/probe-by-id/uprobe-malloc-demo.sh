#!/bin/bash

#make sure your kernel is 3.5 later(support uprobe)
#uretprobe is not demo in here, because uretprobe is only supported in 3.10 kernel

machine_type=`getconf LONG_BIT`

if [ $machine_type -eq 64 ]; then
    echo "perf probe -x /lib64/libc.so.6 malloc"
    perf probe -x /lib64/libc.so.6 malloc
    echo "perf probe -x /lib64/libc.so.6 free"
    perf probe -x /lib64/libc.so.6 free
elif [ $machine_type -eq 32 ]; then
    echo "perf probe -x /lib/libc.so.6 malloc"
    perf probe -x /lib/libc.so.6 malloc
    echo "perf probe -x /lib/libc.so.6 free"
    perf probe -x /lib/libc.so.6 free
fi

if [ $? -ne 0 ]; then
	exit
fi

id1=`cat /sys/kernel/debug/tracing/events/probe_libc/malloc/id`
id2=`cat /sys/kernel/debug/tracing/events/probe_libc/free/id`

../../ktap probe_by_id.kp "$id1 $id2"

perf probe --del probe_libc:malloc
perf probe --del probe_libc:free

