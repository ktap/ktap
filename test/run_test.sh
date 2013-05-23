#!/bin/sh

rmmod ktapvm > /dev/null 2>&1
insmod ../ktapvm.ko
if test $? -ne 0; then
	echo "Cannot insmod ../ktapvm.ko"
	exit -1
fi

KTAP=../ktap
function ktaprun {
	echo "$KTAP $@"
	$KTAP $@
}



#####################################################
ktaprun arg.kp 1 testing
ktaprun arith.kp
ktaprun concat.kp
ktaprun count.kp
ktaprun fibonacci.kp
ktaprun function.kp
ktaprun if.kp
ktaprun kprobe.kp
ktaprun kretprobe.kp
ktaprun len.kp
ktaprun looping.kp
ktaprun pairs.kp
ktaprun table.kp
ktaprun timer.kp
ktaprun tracepoint.kp
ktaprun zerodivide.kp
#####################################################
rmmod ktapvm
if test $? -ne 0; then
	echo "Error in rmmod ../ktapvm.ko, leak module refcount?"
	exit -1
fi

