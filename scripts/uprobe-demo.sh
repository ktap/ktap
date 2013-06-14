#/bin/sh

# get symbol offset by command: perf probe -x /lib/libc.so.6 malloc

echo 'p:probe_libc/malloc /lib/libc.so.6:0x000773c0' > /sys/kernel/debug/tracing/uprobe_events
id=`cat /sys/kernel/debug/tracing/events/probe_libc/malloc/id`
echo $id

../ktap probe_by_id.kp $id

echo -:probe_libc/malloc >> /sys/kernel/debug/tracing/uprobe_events

