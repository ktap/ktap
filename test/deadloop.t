# vi: ft= et ts=4

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



=== TEST 2: dead loop killed by signal
--- src
while (1) {}
--- out
--- err
--- expect_timeout
--- timeout: 1

