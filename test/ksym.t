# vi: ft= et tw=4 sw=4

use lib 'test/lib';
use Test::ktap 'no_plan';

run_tests();

__DATA__

=== TEST 1: ksym
--- src

var a = `generic_file_buffered_write`
var b = `generic_file_mmap`

printf("generic_file_buffered_write: 0x%x\n", a);
printf("generic_file_mmap: 0x%x\n", b);

# test read symbol in kernel module
printf("kp_call: 0x%x\n", `kp_call`)

--- out_like
kp_call: 0x(.*)
--- err

