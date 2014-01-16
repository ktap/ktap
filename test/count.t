# vi: ft= et tw=4 sw=4

use lib 'test/lib';
use Test::ktap 'no_plan';

run_tests();

__DATA__

=== TEST 1: count
--- src
var t = {}

t["key"] += 1
print(t["key"])

t["key"] += 1
print(t["key"])

--- out
1
2
--- err


