% The Ktap Tutorial

# Introduction

ktap is a new scripting dynamic tracing tool for linux

ktap is a new scripting dynamic tracing tool for Linux,
it uses a scripting language and lets users trace the Linux kernel dynamically.
ktap is designed to give operational insights with interoperability
that allows users to tune, troubleshoot and extend kernel and application.
It's similar with Linux Systemtap and Solaris Dtrace.

ktap have different design principles from Linux mainstream dynamic tracing
language in that it's based on bytecode, so it doesn't depend upon GCC,
doesn't require compiling kernel module for each script, safe to use in
production environment, fulfilling the embedded ecosystem's tracing needs.

Highlights features:

  * simple but powerful scripting language
  * register based interpreter (heavily optimized) in Linux kernel
  * small and lightweight (6KLOC of interpreter)
  * not depend on gcc for each script running
  * easy to use in embedded environment without debugging info
  * support for tracepoint, kprobe, uprobe, function trace, timer, and more
  * supported in x86, arm, ppc, mips
  * safety in sandbox


# Getting started

Requirements
- Linux 3.1 or later(Need some kernel patches for kernel earlier than 3.1)
- CONFIG_EVENT_TRACING enabled
- CONFIG_PERF_EVENTS enabled
- CONFIG_DEBUG_FS enabled
  (make sure debugfs mounted before insmod ktapvm
   mount debugfs: mount -t debugfs none /sys/kernel/debug/)

Note that those configuration is always enabled in Linux distribution,
like REHL, Fedora, Ubuntu, etc.

1. Clone ktap from github

        $ git clone http://github.com/ktap/ktap.git

2. Compiling ktap

        $ cd ktap
        $ make       #generate ktapvm kernel module and ktap binary

3. Load ktapvm kernel module(make sure debugfs mounted)

        $ make load  #need to be root or have sudo access

4. Running ktap

        $ ./ktap scripts/helloworld.kp

# Language basics

## Syntax basics

   Dynamic typed

## Control structures

## Date structures

   Associative array

## Built-in functions

## Librarys



# Linux tracing basics

   tracepoints, probe, timer
   filters
   above explaintion
   Ring buffer

# Tracing semantics in ktap

   architecture overview picture reference(pnp format)
   Built-in trace variables
   one-liners
   simple event tracing

# Advanced tracing pattern
   Aggregation/Histogram
   thread local
   flame graph

# Compare with Systemtap

# FAQ

# References

This file includes some references of Linux tracing(not only for ktap),
you will get to know helpful knowledge about tracing after read these links.

Linux Performance Analysis and Tools
By: Brendan Gregg
http://www.brendangregg.com/Slides/SCaLE_Linux_Performance2013.pdf

Good blog about system performance and tracing tool
By: Brendan Gregg
http://dtrace.org/blogs/

Ktap -- yet another kernel tracer
By: Jonathan Corbet
http://lwn.net/Articles/551314/

ktap: A New Scripting Dynamic Tracing Tool For Linux
By: zhangwei(Jovi)
http://events.linuxfoundation.org/sites/events/files/lcjpcojp13_zhangwei.pdf

Dtrace User Guide
By: Sun Microsystems
http://docs.huihoo.com/opensolaris/dtrace-user-guide/html/index.html

# History

# Sample scripts


