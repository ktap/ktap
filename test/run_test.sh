#!/bin/sh

rmmod ktapvm > /dev/null 2>&1
insmod ../ktapvm.ko
if test $? -ne 0; then
	echo "Cannot insmod ../ktapvm.ko"
	exit -1
fi

KTAP=../ktap
ktaprun() {
	echo "$KTAP $@"
	$KTAP $@
}



#######################################################
# Use $ktap directly if the arguments contains strings
$KTAP arg.kp 1 testing "2 3 4"
$KTAP -e 'print("one-liner testing")'
$KTAP -e 'exit()'
$KTAP -o /dev/null -e 'trace syscalls:* { print(argevent) }' \
		-- ls > /dev/null

$KTAP -o /dev/null -e 'trace syscalls:* { print(argevent) }' \
		-- $KTAP -e 'print("trace ktap by self")'

ktaprun arithmetic.kp
ktaprun -o /dev/null stack_overflow.kp
ktaprun concat.kp
ktaprun count.kp
ktaprun fibonacci.kp
ktaprun function.kp
ktaprun if.kp
ktaprun -q kprobe.kp
ktaprun -q kretprobe.kp
ktaprun len.kp
ktaprun looping.kp
ktaprun pairs.kp
ktaprun table.kp
ktaprun ptable.kp
ktaprun -q timer.kp
ktaprun -q tracepoint.kp
ktaprun -o /dev/null zerodivide.kp
ktaprun -o /dev/null ksym.kp

echo "testing kill deadloop ktap script"
$KTAP -e 'while (1) {}' &
sleep 1
pkill ktap
sleep 1

cd ffi && make --quiet --no-print-directory test && cd -

#####################################################
rmmod ktapvm
if test $? -ne 0; then
	echo "Error in rmmod ../ktapvm.ko, leak module refcount?"
	exit -1
fi

