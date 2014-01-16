# vi: ft= et tw=4 sw=4

use lib 'test/lib';
use Test::ktap 'no_plan';

run_tests();

__DATA__

=== TEST 1: stack overflow
--- src
function f(a) {
	        return 1 + f(a+1)
}

print(f(0))

--- out_like
(.*)stack overflow(.*)
--- err


