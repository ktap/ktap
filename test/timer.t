# vi: ft= et tw=4 sw=4

use lib 'test/lib';
use Test::ktap 'no_plan';

run_tests();

__DATA__

=== TEST 1: timer
--- opts: -q
--- src

var n1 = 0
var n2 = 0

tick-1s {
	n1 = n1 + 1
}

tick-1s {
	n2 = n2 + 1
}

tick-4s {
	if (n1 == 0 || n2 == 0) {
		print("failed")
	}
	exit()
}

--- out
--- err


