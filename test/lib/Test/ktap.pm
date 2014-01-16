# Copyright (C) Yichun Zhang (agentzh)

package Test::ktap;

use Test::Base -Base;
use POSIX ();
use IPC::Run3 ();

our @EXPORT = qw( run_tests );

sub run_tests () {
    for my $block (Test::Base::blocks()) {
        run_test($block);
    }
}

sub bail_out (@) {
    Test::More::BAIL_OUT(@_);
}

sub run_test ($) {
    my $block = shift;
    my $name = $block->name;

    my $timeout = $block->timeout() || 10;
    my $option = $block->option;
    my $args = $block->args;

    my $cmd = "./ktap";

    if (defined $option) {
        $cmd .= " $option";
    }

    my $kpfile;
    if (defined $block->src) {
	$kpfile = POSIX::tmpnam() . ".kp";
	open my $out, ">$kpfile" or
	    bail_out("cannot open $kpfile for writing: $!");
	print $out ($block->src);
	close $out;
	$cmd .= " $kpfile"
    }

    if (defined $args) {
        $cmd .= " $args";
    }

    #warn "CMD: $cmd\n";

    my ($out, $err);
    IPC::Run3::run3($cmd, undef, \$out, \$err);
    my $ret = ($? >> 8);

    if (defined $kpfile) {
	unlink $kpfile;
    }

    if (defined $block->out) {
        is $out, $block->out, "$name - stdout eq okay";
    }

    my $regex = $block->out_like;
    if (defined $regex) {
        if (!ref $regex) {
            $regex = qr/$regex/ms;
        }
        like $out, $regex, "$name - stdout like okay";
    }

    if (defined $block->err) {
        is $err, $block->err, "$name - stderr eq okay";
    }

    $regex = $block->err_like;
    if (defined $regex) {
        if (!ref $regex) {
            $regex = qr/$regex/ms;
        }
        like $err, $regex, "$name - stderr like okay";
    }

    my $exp_ret = $block->ret;
    if (!defined $exp_ret) {
        $exp_ret = 0;
    }
    is $ret, $exp_ret, "$name - exit code okay";
}

1;
