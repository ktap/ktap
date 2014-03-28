# vi: ft= et tw=4 sw=4

use lib 'test/lib';
use Test::ktap 'no_plan';

run_tests();

__DATA__

=== TEST 1: function
--- src
### basic function call ###
function f1(a, b) {
	return a + b
}

print(f1(2, 3))

### return string ###
function f2() {
	return "function return"
}

print(f2())

### closure testing ### 
function f4() {
	var f5 = function(a, b) {
		return a * b
	}
	return f5
}

var f = f4()
print(f(9, 9))

### closure with lexcial variable ### 
var i = 1
function f6() {
	i = 5
	var f7 = function(a, b) {
		return a * b + i
	}
	return f7
}

f = f6()
print(f(9, 9))

i = 6
print(f(9, 9))

### tail call
### stack should not overflow in tail call mechanism
var a = 0
function f8(i) {
	if (i == 1000000) {
		a = 1000000
		return
	}
	# must add return here, otherwise stack overflow
	return f8(i+1)
}

f8(0)
print(a)

--- out
5
function return
81
86
87
1000000

--- err


