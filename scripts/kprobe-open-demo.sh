#!/bin/bash

#get kernel debuginfo firstly. In Fedora: debuginfo-install kernel

echo "perf probe -f do_sys_open"
perf probe -f do_sys_open

id=`cat /sys/kernel/debug/tracing/events/probe/do_sys_open/id`

../ktap probe_by_id.kp $id

perf probe --del probe:do_sys_open

