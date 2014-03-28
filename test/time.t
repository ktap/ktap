# vi: ft= ts=4 sw=4 et

use lib 'test/lib';
use Test::ktap 'no_plan';

our $SecPattern = time();
$SecPattern =~ s{(\d)\d$}{ my $a = $1; my $b = $a + 1; "[$a$b]\\d" }e;

#warn $SecPattern;

run_tests();

__DATA__

=== TEST 1: gettimeofday_s
--- src
var begin = gettimeofday_s()
printf("sec: %d\n", begin)
printf("elapsed: %d\n", begin - gettimeofday_s())

--- out_like eval
qr/^sec: $::SecPattern
elapsed: 0$/

--- err



=== TEST 2: gettimeofday_ms
--- src
printf("%d\n", gettimeofday_ms())

--- out_like eval
qr/^$::SecPattern\d{3}$/

--- err



=== TEST 3: gettimeofday_us
--- src
printf("%d", gettimeofday_us())

--- out_like eval
qr/^$::SecPattern\d{6}$/

--- err



=== TEST 4: gettimeofday_ns
--- src
printf("%d", gettimeofday_ns())

--- out_like eval
qr/^$::SecPattern\d{9}$/

--- err

