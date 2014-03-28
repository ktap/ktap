# vi: ft= et tw=4 sw=4

use lib 'test/lib';
use Test::ktap 'no_plan';

run_tests();

__DATA__

=== TEST 1: bad assignment (unexpected eof)
--- src
a =

--- out
--- err_like
unexpected symbol near '<eof>'

--- ret: 255

