ktap
====

A New Scripting Dynamic Tracing Tool For Linux

KTAP is a new scripting dynamic tracing tool for Linux,
it uses a scripting language and lets users trace the Linux kernel dynamically.
KTAP is designed to give operational insights with interoperability
that allow users to tune, troubleshoot and extend kernel and application.
It's similar with Linux Systemtap and Solaris Dtrace.

KTAP have different design principles from Linux mainstream dynamic tracing
language in that it's based on bytecode, so it doesn't depend upon GCC,
doesn't require compiling a kernel module, safe to use in production
environment, fulfilling the embedd ecosystem's tracing needs.

KTAP also is designed for enabling great interoperability with Linux kernel,
it gives user the power to modify and extend the system, and let users
explore the system in an easy way.

More information can be found at doc/ directory.


Building & Running
------------------

1) Clone ktap from github  

	[root@jovi]# git clone http://github.com/ktap/ktap.git

2) Compiling ktap  

	[root@jovi]# cd ktap
	[root@jovi]# make       #generate ktapvm kernel module and ktap binary

3) Insert ktapvm kernel module  

	[root@jovi]# insmod ./ktapvm.ko

4) Running ktap  

	[root@jovi]# ./ktap scripts/syscalls.kp


Example of syscall tracing
--------------------------

syscalls.kp:  

	function eventfun (e) {
		printf("%d %d\t%s\t%s", cpu(), pid(), execname(), e.tostring())
	}

	kdebug.probe("tp:syscalls", eventfun)

	kdebug.probe_end(function () {
		printf("probe end\n")
	})


Mailing list
------------
ktap@freelists.org  
You can subscribe KTAP mailing list at link(subscribe before posting):
http://www.freelists.org/list/ktap


License
-------
ktap is GPL licensed.  
See more info in doc/ktap-license.txt.


Contribution
------------
KTAP is still under active development, so contribution is welcome.
You are encouraged to report bugs, provide feedback, send feature request,
or hack on it.


ktap links
----------
LWN review on ktap by Jonathan Corbet: http://lwn.net/Articles/551314/

