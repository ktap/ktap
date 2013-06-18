#!/bin/bash

ids=`find /sys/kernel/debug/tracing/events/syscalls/ -name id |xargs cat`
../ktap probe_by_id.kp "$ids"

