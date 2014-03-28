# vi: ft= et tw=4 sw=4

use lib 'test/lib';
use Test::ktap 'no_plan';

run_tests();

__DATA__

=== TEST 1: looping
--- src

var t = {}
t[1] = 101
t[2] = 102
t[3] = 103
t["key_1"] = "value_1"
t["key_2"] = "value_2"
t["key_3"] = "value_3"

var n = 0

for (k, v in pairs(t)) {
	n = n + 1

	if (k == 1 && v != 101) {
		print("failed")
	}
	if (k == 2 && v != 102) {
		print("failed")
	}
	if (k == 3 && v != 103) {
		print("failed")
	}
	if (k == "key_1" && v != "value_1") {
		print("failed")
	}
	if (k == "key_2" && v != "value_2") {
		print("failed")
	}
	if (k == "key_3" && v != "value_3") {
		print("failed")
	}
}

if (n != len(t)) {
	print("failed")
}

--- out
--- err

