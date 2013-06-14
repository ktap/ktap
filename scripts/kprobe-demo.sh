#/bin/sh

# You can run this shell by command(only tested in x86_32):
# ./kprobe.sh 'do_sys_open dfd=%ax filename=%dx flags=%cx mode=+4($stack)'

echo "p:myprobe" $@ > /sys/kernel/debug/tracing/kprobe_events
id=`cat /sys/kernel/debug/tracing/events/kprobes/myprobe/id`
echo $id

../ktap probe_by_id.kp $id

echo -:myprobe >> /sys/kernel/debug/tracing/kprobe_events

