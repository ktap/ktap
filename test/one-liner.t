# vi: ft= et tw=4 sw=4

use lib 'test/lib';
use Test::ktap 'no_plan';

run_tests();

__DATA__

=== TEST 1: print
--- args: -e 'print("one-liner testing")'
--- out
one-liner testing
--- err



=== TEST 2: exit
--- args: -e 'exit() print("failed")'
--- out
--- err



=== TEST 3: syscalls in "ls"
--- args: -e 'trace syscalls:* { print(argstr) }' -- ls
--- out_like
sys_mprotect -> 0x0
.*?
sys_close\(fd: \d+\)
--- err



=== TEST 4: trace ktap syscalls
--- args: -e 'trace syscalls:* { print(argstr) }' -- ./ktap -e 'print("trace ktap by self")'
--- out_like
sys_mprotect -> 0x0
.*?
sys_close\(fd: \d+\)
--- err

=== TEST 5: trace ktap function calls
--- args: -q -e 'trace probe:kp_* {print(argstr)}' -- ./ktap samples/helloworld.kp
--- out_like
kp_vm_new_state: (.*)
.*?

