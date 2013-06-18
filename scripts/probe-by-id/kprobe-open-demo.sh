#!/bin/bash

echo "perf probe -a 'p=do_sys_open'"
perf probe -a 'p=do_sys_open'
if [ $? -ne 0 ]; then
	exit
fi

echo "perf probe -a 'r=do_sys_open%return'"
perf probe -a 'r=do_sys_open%return'
if [ $? -ne 0 ]; then
	exit
fi

id1=`cat /sys/kernel/debug/tracing/events/probe/p/id`
id2=`cat /sys/kernel/debug/tracing/events/probe/r/id`

../../ktap probe_by_id.kp "$id1 $id2"

perf probe --del probe:p
perf probe --del probe:r

