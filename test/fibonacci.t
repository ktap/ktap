# vi: ft= et tw=4 sw=4

use lib 'test/lib';
use Test::ktap 'no_plan';

run_tests();

__DATA__

=== TEST 1: regular recursive fibonacci
--- src
function fib(n) {
	if (n < 2) {
		return n
	}
	return fib(n-1) + fib(n-2)
}

print(fib(20))
--- out
6765
--- err



=== TEST 2: tail recursive fibonacci
--- src
function fib(n) {
	function f(iter, res, next) {
		if (iter == 0) {
			return res;
		}
		return f(iter-1, next, res+next)
	}
	return f(n, 0, 1)
}

print(fib(20))
--- out
6765
--- err

