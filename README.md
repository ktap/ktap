ktap
====

A New Scripting Dynamic Tracing Tool For Linux

KTAP is a new dynamic tracing tool for Linux,
it is designed to give operational insights that allow users to
tune and troubleshoot kernel and application.
It's similar with Linux Systemtap and Solaris Dtrace.

KTAP uses a scripting language and lets users trace the Linux kernel dynamically.
KTAP have different design principles from SystemTap in that it's based on bytecode,
so it doesn't depend upon GCC, doesn't require compiling a kernel module,
have great portability, safe to use in production environment,
fulfilling the embedd ecosystem's tracing needs.

KTAP is built from scratch, with GPL licensed.
https://github.com/ktap/ktap.git
