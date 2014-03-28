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


=== TEST 2: cannot call timer.tick in trace_end context
--- opts: -q
--- src

trace_end {
	tick-1s {
		print("error")
	}
}

--- out
error: timer.tick only can be called in RUNNING state
--- err


=== TEST 3: cannot call timer.profile in trace_end context
--- opts: -q
--- src

trace_end {
	profile-1s {
		print("error")
	}
}

--- out
error: timer.profile only can be called in RUNNING state

--- err

