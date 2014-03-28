# vi: ft= et ts=4

use lib 'test/lib';
use Test::ktap 'no_plan';

run_tests();

__DATA__

=== TEST 1: kprobe
--- opts: -q
--- src

var n = 0
trace probe:schedule {
	n = n + 1
}

# share same event id with previous one
trace probe:schedule {
}

# test event filter
trace probe:do_sys_open dfd=%di filename=%si flags=%dx mode=%cx /dfd==1/ { }

tick-1s {
	print(n==0)
	exit()
}
--- out
false
--- err



=== TEST 2: kretprobe
--- opts: -q
--- src
var n = 0
trace probe:__schedule%return {
	n = n + 1
}

tick-1s {
	print(n==0)
	exit()
}

--- out
false
--- err


=== TEST 3: only can be called in mainthread
--- opts: -q
--- src

trace probe:schedule {
	trace *:* {
	}
}

--- out
error: only mainthread can create function
--- err


=== TEST 4: can not be called in trace_end context
--- opts: -q
--- src

trace_end {
	trace *:* {
	}
}

--- out
error: kdebug.trace_by_id only can be called in RUNNING state
--- err



