# vi: ft= et tw=4 sw=4

use lib 'test/lib';
use Test::ktap 'no_plan';

run_tests();

__DATA__

=== TEST 1: exit dead loop
--- src
tick-1s {
	exit()
}

tick-3s {
	print("dead loop not exited")
}

while (1) {}
--- out
--- err

