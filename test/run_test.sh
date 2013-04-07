#!/bin/sh

rmmod ktapvm > /dev/null 2>&1
insmod ../ktapvm.ko
if test $? -ne 0; then
	echo "Cannot insmod ../ktapvm.ko"
	exit -1
fi

KTAP=../ktap

for kp in `ls *.kp`
do
	echo "-----------------------------"
	echo "executing $kp"

	if test $kp == "divide_0.kp"; then
		$KTAP $kp > /dev/null
	else
		$KTAP $kp
	fi
done

rmmod ktapvm
if test $? -ne 0; then
	echo "Error in rmmod ../ktapvm.ko, leak module refcount?"
	exit -1
fi

