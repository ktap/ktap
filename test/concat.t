# vi: ft= et tw=4 sw=4

use lib 'test/lib';
use Test::ktap 'no_plan';

run_tests();

__DATA__

=== TEST 1: string concat
--- src
var a = "123"
var b = "456"

print(a..b)

--- out
123456
--- err


