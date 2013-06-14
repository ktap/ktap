#/bin/sh

# get symbol offset by command: perf probe -x /lib/libc.so.6 malloc

echo 'p:probe_libc/malloc /lib/libc.so.6:0x000773c0' >> /sys/kernel/debug/tracing/uprobe_events
id1=`cat /sys/kernel/debug/tracing/events/probe_libc/malloc/id`
echo $id1

echo 'r:probe_libc/malloc_ret /lib/libc.so.6:0x000773c0' >> /sys/kernel/debug/tracing/uprobe_events
id2=`cat /sys/kernel/debug/tracing/events/probe_libc/malloc_ret/id`
echo $id2

../ktap probe_by_id.kp $id2 $id1

echo -:probe_libc/malloc >> /sys/kernel/debug/tracing/uprobe_events
echo -:probe_libc/malloc_ret >> /sys/kernel/debug/tracing/uprobe_events

