# vi: ft= et tw=4 sw=4

use lib 'test/lib';
use Test::ktap 'no_plan';

run_tests();

__DATA__

=== TEST 1: zero divide
--- src

var a = 1/0
#should not go here
print("failed")

--- out_like
(.*)divide 0(.*)
--- err


