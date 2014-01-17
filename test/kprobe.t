# vi: ft= et tw=4 sw=4

use lib 'test/lib';
use Test::ktap 'no_plan';

run_tests();

__DATA__

=== TEST 1: kprobe
--- option: -q
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
--- option: -q
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
