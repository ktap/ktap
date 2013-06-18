#!/bin/bash

#make sure your kernel is 3.5 later(support uprobe)
#uretprobe is only supported in 3.10 kernel

machine_type=`getconf LONG_BIT`

if [ $machine_type -eq 64 ]; then
	LIBC=/lib64/libc.so.6
elif [ $machine_type -eq 32 ]; then
	LIBC=/lib/libc.so.6
fi

echo "perf probe -x $LIBC 'pmalloc=malloc'"
perf probe -x $LIBC -a 'pmalloc=malloc'
if [ $? -ne 0 ]; then
	exit
fi

echo "perf probe -x $LIBC 'rmalloc=malloc%return'"
perf probe -x $LIBC 'rmalloc=malloc%return'
if [ $? -ne 0 ]; then
	exit
fi

echo "perf probe -x $LIBC 'pfree=free'"
perf probe -x $LIBC 'pfree=free'
if [ $? -ne 0 ]; then
	exit
fi

echo "perf probe -x $LIBC 'rfree=free%return'"
perf probe -x $LIBC 'rfree=free%return'
if [ $? -ne 0 ]; then
	exit
fi

id1=`cat /sys/kernel/debug/tracing/events/probe_libc/pmalloc/id`
id2=`cat /sys/kernel/debug/tracing/events/probe_libc/rmalloc/id`
id3=`cat /sys/kernel/debug/tracing/events/probe_libc/pfree/id`
id4=`cat /sys/kernel/debug/tracing/events/probe_libc/rfree/id`

../../ktap probe_by_id.kp "$id1 $id2 $id3 $id4"

perf probe --del probe_libc:pmalloc
perf probe --del probe_libc:rmalloc
perf probe --del probe_libc:pfree
perf probe --del probe_libc:rfree

