# vi: ft= et tw=4 sw=4

use lib 'test/lib';
use Test::ktap 'no_plan';

run_tests();

__DATA__

=== TEST 1: sanity
--- args: 1 testing "2 3 4"
--- src
printf("arg 0: %s\n", arg[0])
printf("arg 1: %d\n", arg[1])
printf("arg 2: %s\n", arg[2])
printf("arg 3: %s\n", arg[2])

--- out_like chop
^arg 0: /tmp/\S+\.kp
arg 1: 1
arg 2: testing
arg 3: testing$

--- err

