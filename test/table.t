# vi: ft= et tw=4 sw=4

use lib 'test/lib';
use Test::ktap 'no_plan';

run_tests();

__DATA__

=== TEST 1: table
--- src

### table testing ###
var x = {}
x[1] = "1"
if (x[1] != "1") {
	print("failed")
}

x[1] = 22222222222222222222222222222222222222222
if (x[1] != 22222222222222222222222222222222222222222) {
	print("failed")
}

x[1] = "jovi"
if (x[1] != "jovi") {
	print("failed")
}

x[11111111111111111111111111111111] = "jovi"
if (x[11111111111111111111111111111111] != "jovi") {
	print("failed")
}

x["jovi"] = 1
if (x["jovi"] != 1) {
	print("failed")
}

x["long string....................................."] = 1
if (x["long string....................................."] != 1) {
	print("failed")
}

# issue: subx must declare firstly, otherwise kernel will oops
var subx = {}
subx["test"] = "this is test"
x["test"] = subx
if (x["test"]["test"] != "this is test") {
	print("failed")
}

var tbl = table.new(99999, 0)
var i = 1
while (i < 100000) {
	tbl[i] = i	
	i = i + 1
}

var i = 1
while (i < 100000) {
	if (tbl[i] != i) {
		print("failed")
	}
	i = i + 1
}

#### table initization
var days = {"Sunday", "Monday", "Tuesday", "Wednesday",
		"Thursday", "Friday", "Saturday"}

if (days[2] != "Monday") {
	print("failed")
}


--- out
--- err


=== TEST 2: ptable
--- src

var s = ptable()

for (i = 1, 100, 1) {
	s["k"] <<< i
}

if (count(s["k"]) != 100) {
	print("failed")
}
if (sum(s["k"]) != 5050) {
	print("failed")
}
if (max(s["k"]) != 100) {
	print("failed")
}
if (min(s["k"]) != 1) {
	print("failed")
}

var s2 = ptable(10000, 0)

for (i = 1, 10000, 1) {
	s2[i] <<< i
}

if (min(s2[1]) != 1) {
	print("failed")
}

if (sum(s2[10]) != 10) {
	print("failed")
}

if (max(s2[100]) != 100) {
	print("failed")
}

--- out
--- err

