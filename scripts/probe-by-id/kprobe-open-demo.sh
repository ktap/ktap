#!/bin/bash

#get kernel debuginfo firstly. In Fedora: debuginfo-install kernel

echo "perf probe -f do_sys_open"
perf probe -f do_sys_open

echo "perf probe -f do_sys_open%return"
perf probe -f do_sys_open%return

id1=`cat /sys/kernel/debug/tracing/events/probe/do_sys_open/id`
id2=`cat /sys/kernel/debug/tracing/events/probe/do_sys_open_1/id`

../../ktap probe_by_id.kp "$id1 $id2"

perf probe --del probe:do_sys_open
perf probe --del probe:do_sys_open_1

