# vi: ft= et tw=4 sw=4

use lib 'test/lib';
use Test::ktap 'no_plan';

run_tests();

__DATA__

=== TEST 1: test if
--- src

if (false) {
	print("failed")
}

if (nil) {
	print("failed")
}

# ktap only think false and nil is "real false", number 0 is true
# it's same as lua
# Might change it in future, to make similar with C
if (0) {
	print("number 0 is true")
}

--- out
number 0 is true
--- err


