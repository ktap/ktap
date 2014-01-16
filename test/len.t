# vi: ft= et tw=4 sw=4

use lib 'test/lib';
use Test::ktap 'no_plan';

run_tests();

__DATA__

=== TEST 1: len
--- src
var a = "123456789"

print(len(a))

var b = {}
b[0] = 0
b[1] = 1
b["keys"] = "values"

print(len(b))

--- out
9
3
--- err

