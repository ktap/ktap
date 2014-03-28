# vi: ft= et ts=4

use lib 'test/lib';
use Test::ktap 'no_plan';

run_tests();

__DATA__

=== TEST 1: tracepoint
--- opts: -q
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


=== TEST 2: enable all tracepoints in dry-run mode
--- opts: -q -d
--- src

trace *:* {}

--- out
--- err
--- expect_timeout
--- timeout: 10


=== TEST 3: test kdebug.tracepoint
--- opts: -q
--- src

kdebug.tracepoint("sys_enter_open", function () {})
tick-1s {
	exit()
}

--- out
--- err
