# vi: ft= et tw=4 sw=4

use lib 'test/lib';
use Test::ktap 'no_plan';

run_tests();

__DATA__

=== TEST 1: looping
--- src

### basic while-loop testing
var a = 1
while (a < 1000) {
	a = a + 1
}

print(a)

### break testing
### Note that ktap don't have continue keyword
var a = 1
while (a < 1000) {
	if (a == 10) {
		break
	}
	a = a + 1
}

print(a)

### for-loop testing
var b = 0
for (c = 0, 1000, 1) {
	b = b + 1
}

print(b)

--- out
1000
10
1001
--- err

