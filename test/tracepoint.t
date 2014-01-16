# vi: ft= et tw=4 sw=4

use lib 'test/lib';
use Test::ktap 'no_plan';

run_tests();

__DATA__

=== TEST 1: tracepoint
--- option: -q
--- src

var n = 0

trace sched:* {
	n = n + 1
}

tick-1s {
	if (n == 0) {
		print("failed")
	}
	exit()
}

--- out
--- err


