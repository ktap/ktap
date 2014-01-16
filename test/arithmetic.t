# vi: ft= et tw=4 sw=4

use lib 'test/lib';
use Test::ktap 'no_plan';

run_tests();

__DATA__

=== TEST 1: arithmetic
--- src
if (1 > 2) {
	print("failed")
}
if (200 < 100) {
	print("failed")
}

var a = 4
var b = 5

if ((a + b) != 9) {
	print("failed")
}

if ((a - b) != -1) {
	print("failed")
}

if ((a * b) != 20) {
	print("failed")
}

if ((a % b) != 4) {
	print("failed")
}

if ((a / b) != 0) {
	print("failed")
}



#below checking only valid for 64-bit system

var c = 0x1234567812345678
var d = 0x2

if (c + d != 0x123456781234567a) {
	print("failed")
}

if (-1 != 0xffffffffffffffff) {
	print("failed")
}

--- out
--- err


