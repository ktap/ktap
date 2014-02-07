package Test::ktap::features;

use strict;
use warnings;

use Exporter 'import';
our @EXPORT_OK = qw( &has_ffi );

sub has_ffi {
    return `./ktap -V 2>&1 | grep -c FFI` == 1;
}

1;
