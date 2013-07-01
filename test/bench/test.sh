#!/bin/sh

gcc -o sembench sembench.c -O2 -lpthread

COMMAND="./sembench -t 200 -w 20 -r 30 -o 2"
echo "Pass 1 without tracing"
$COMMAND
echo "Pass 2 without tracing"
$COMMAND
echo "Pass 3 without tracing"
$COMMAND

../../ktap ./futex.kp &

echo "Pass 1 with tracing"
$COMMAND
echo "Pass 2 with tracing"
$COMMAND
echo "Pass 3 with tracing"
$COMMAND

pkill ktap

rm -rf ./sembench
