% The ktap Tutorial

# Introduction

ktap is a new script-based dynamic tracing tool for Linux
http://www.ktap.org

ktap is a new script-based dynamic tracing tool for Linux.
It uses a scripting language and lets the user trace the Linux kernel dynamically.
ktap is designed to give operational insights with interoperability
that allows users to tune, troubleshoot and extend kernel and application.
It's similar to Linux SystemTap and Solaris DTrace.

ktap has different design principles from Linux mainstream dynamic tracing
language in that it's based on bytecode, so it doesn't depend upon GCC,
doesn't require compiling a kernel module for each script, safe to use in
production environment, fulfilling the embedded ecosystem's tracing needs.

Highlights features:

* a simple but powerful scripting language
* register-based interpreter (heavily optimized) in Linux kernel
* small and lightweight
* not depend on the GCC toolchain for each script run
* easy to use in embedded environments without debugging info
* support for tracepoint, kprobe, uprobe, function trace, timer, and more
* supported in x86, ARM, PowerPC, MIPS
* safety in sandbox

# Getting started

Requirements

* Linux 3.1 or later (patches are required for earlier versions)
* `CONFIG_EVENT_TRACING` enabled
* `CONFIG_PERF_EVENTS` enabled
* `CONFIG_DEBUG_FS` enabled

     make sure debugfs mounted before `insmod ktapvm`

     mount debugfs: `mount -t debugfs none /sys/kernel/debug/`
* libelf (optional)
     Install elfutils-libelf-devel on RHEL-based distros, or libelf-dev on
     Debian-based distros.
     Use `make NO_LIBELF=1` to build without libelf support.
     libelf is required for resolving symbols to addresses in DSO, and for SDT.

Note that those configurations should always be enabled in Linux distribution,
like RHEL, Fedora, Ubuntu, etc.

1. Clone ktap from GitHub

        $ git clone http://github.com/ktap/ktap.git
2. Compile ktap

        $ cd ktap
        $ make       #generate ktapvm kernel module and ktap binary
3. Load ktapvm kernel module(make sure debugfs mounted)

        $ make load  #need to be root or have sudo access
4. Run ktap

        $ ./ktap samples/helloworld.kp


# Language basics

## Syntax basics

ktap's syntax is designed with the C language syntax in mind. This is for lowering the entry barrier for C programmers who are working on the kernel or other systems software.

* Variable declarations

    The biggest syntax differences with C is that ktap is a dynamically-typed
language, so you won't need add any variable type declaration, just
use the variable.
* Functions

    All functions in ktap should use keyword "function" declaration
* Comments

    Comments in ktap start with `#`. Long comments are not supported right now.
* Others

    Semicolons (`;`) are not required at the end of statements in ktap. ktap uses a free-syntax style, so you are free to use ';' or not.

ktap uses `nil` as `NULL`. The result of an arithmetic operation on `nil` is also `nil`.

ktap does not have array structures, and it does not have any pointer operations.

## Control structures

ktap's `if`/`else` statement is the same as the C language's.

There are three kinds of for-loop in ktap:

1. a kinda Lua-ish style:

    for (i = init, limit, step) { body }
2. the same form as in C:

    for (i = init; i < limit; i += step) { body }
3. Lua's table iterating style:

    for (k, v in pairs(t)) { body } # looping all elements of table

Note that ktap does not have the `continue` keyword, but C does.

## Data structures

Associative arrays are heavily used in ktap; they are also called "tables".

Table declarations:

    t = {}

How to use tables:

    t[1] = 1
    t[1] = "xxx"
    t["key"] = 10
    t["key"] = "value"

    for (k, v in pairs(t)) { body }   # looping all elements of table

# Built-in functions and libraries

## Built-in functions

**print (...)**

Receives any number of arguments, and prints their values.
print is not intended for formatted output, but only as a
quick way to show values, typically for debugging.

For formatted output, use `printf` instead.

**printf (fmt, ...)**

Similar to C's `printf`, for formatted string output.

**pairs (t)**

Returns three values: the next function, the table t, and nil,
so that the construction

    for (k, v in pairs(t)) { body }

will iterate through all the key-value pairs in the table `t`.

**len (t) /len (s)**

If the argument is a string, returns the length of the string.

If the argument is a table, returns the number of table pairs.

**in_interrupt ()**

Checks if it is in the context of interrupts.

**exit ()**

quits ktap programs, similar to the `exit` syscall.

**arch ()**

returns machine architecture, like `x86`, `arm`, and etc.

**kernel_v ()**

returns Linux kernel version string, like `3.9` and etc.

**user_string (addr)**

accepts a userspace address, reads the string data from userspace, and returns the ktap string value.

**print_hist (t)**

accepts a table and outputs the table histogram to the user.


## Libraries

### Kdebug Library

**kdebug.trace_by_id (eventdef_info, eventfun)**

This function is the underlying interface for the higher level tracing primitives.

Note that the `eventdef_info` argument is just a C pointer value pointing to a userspace memory block holding the real
`eventdef_info` structure. The structure definition is as follows:

    struct ktap_eventdesc {
	int nr; /* the number to id */
	int *id_arr; /* id array */
	char *filter;
    };

Those `id`s are read from `/sys/kernel/debug/tracing/events/$SYS/$EVENT/id`.

The second argument in above example is a ktap function object:

    function eventfun () { action }

**kdebug.trace_end (endfunc)**

This function is used for invoking a function when tracing ends, it will wait
until the user presses `CTRL-C` to stop tracing, then ktap will call the argument, the `endfunc` function. The
user could output tracing results in that function, or do other things.

User usually do not need to use the `kdebug` library directly and just use the `trace`/`trace_end` keywords provided by the language.

### Timer Library

### Table Library

**table.new (narr, nrec)**

pre-allocates a table with `narr` array entries and `nrec` records.

# Linux tracing basics

tracepoints, probe, timer, filters, ring buffer

# Tracing semantics in ktap

## Tracing block

**trace EVENTDEF /FILTER/ { ACTION }**

This is the basic tracing block in ktap. You need to use a specific `EVENTDEF` string, and your own event function.

There are four types of `EVENTDEF`: tracepoints, kprobes, uprobes, SDT probes.

- tracepoint:

	EventDef               Description
	--------------------   -------------------------------
	syscalls:*             trace all syscalls events
	syscalls:sys_enter_*   trace all syscalls entry events
	kmem:*                 trace all kmem related events
	sched:*                trace all sched related events
	sched:sched_switch     trace sched_switch tracepoint
	\*:\*                  trace all tracepoints in system

	All tracepoint events are based on
	
	    /sys/kernel/debug/tracing/events/$SYS/$EVENT

- ftrace (kernel 3.3+, and must be compiled with `CONFIG_FUNCTION_TRACER`)

	EventDef               Description
	--------------------   -------------------------------
	ftrace:function        trace kernel functions based on ftrace

	User need to use filter (/ip==*/) to trace specific functions.
	Function must be listed in /sys/kernel/debug/tracing/available_filter_functions

> ***Note*** of function event
> 
> perf support ftrace:function tracepoint since Linux 3.3 (see below commit),
> ktap is based on perf callback, so it means kernel must be newer than 3.3
> then can use this feature.
> 
>     commit ced39002f5ea736b716ae233fb68b26d59783912
>     Author: Jiri Olsa <jolsa@redhat.com>
>     Date:   Wed Feb 15 15:51:52 2012 +0100
>
>     ftrace, perf: Add support to use function tracepoint in perf 
>

- kprobe:

	EventDef               Description
	--------------------   -----------------------------------
	probe:schedule         trace schedule function
	probe:schedule%return  trace schedule function return
	probe:SyS_write        trace SyS_write function
	probe:vfs*             trace wildcards vfs related function

	kprobe functions must be listed in /proc/kallsyms
- uprobe:

	EventDef                               Description
	------------------------------------   ---------------------------
	probe:/lib64/libc.so.6:malloc          trace malloc function
	probe:/lib64/libc.so.6:malloc%return   trace malloc function return
	probe:/lib64/libc.so.6:free            trace free function
	probe:/lib64/libc.so.6:0x82000         trace function with file offset 0x82000
	probe:/lib64/libc.so.6:*               trace all libc function

	symbol resolving need libelf support

- sdt:

	EventDef                               Description
	------------------------------------   --------------------------
	sdt:/libc64/libc.so.6:lll_futex_wake   trace stapsdt lll_futex_wake
	sdt:/libc64/libc.so.6:*                trace all static markers in libc

	sdt resolving need libelf support


**trace_end { ACTION }**

## Tracing Built-in variables

**arg0..9**

Evaluates to argument 0 to 9 of the event object. If fewer than ten arguments are passed to the current probe, the remaining variables return nil.

> ***Note*** of arg offset
>
> The arg offset(0..9) is determined by event format shown in debugfs.
>
>     #cat /sys/kernel/debug/tracing/events/sched/sched_switch/format
>     name: sched_switch
>     ID: 268
>     format:
>         field:char prev_comm[32];         <- arg0
>         field:pid_t prev_pid;             <- arg1
>         field:int prev_prio;              <- arg2
>         field:long prev_state;            <- arg3
>         field:char next_comm[32];         <- arg4
>         field:pid_t next_pid;             <- arg5
>         field:int next_prio;              <- arg6
>
> As shown above, the tracepoint event `sched:sched_switch` takes 7 arguments, from `arg0` to `arg6`.
>
> For syscall event, `arg0` is the syscall number, not the first argument of the syscall function. Use `arg1` as the first argument of the syscall function.
> For example:
>
>     SYSCALL_DEFINE3(read, unsigned int, fd, char __user *, buf, size_t, count)
>                                         <arg1>             <arg2>       <arg3>
>
> This is similar to kprobe and uprobe, the `arg0` of kprobe/uprobe events
>  is always `_probe_ip`, not the first argument given by the user, for example:
>
>     # ktap -e 'trace probe:/lib64/libc.so.6:malloc size=%di'
>
>     # cat /sys/kernel/debug/tracing/events/ktap_uprobes_3796/malloc/format
>         field:unsigned long __probe_ip;   <- arg0
>         field:u64 size;                   <- arg1


**cpu**

returns the current CPU id.

**pid**

returns current process pid.

**tid**

returns the current thread id.

**uid**

returns the current process's uid.

**execname**

returns the current process executable's name in a string.

**argstr**

Event string representation. You can print it by `print(argstr)`, turning the
event into a human readable string. The result is mostly the same as each
entry in `/sys/kernel/debug/tracing/trace`

**probename**

Event name. Each event has a name associated with it.
(Dtrace also have 'probename' keyword)

## Timer syntax

**tick-Ns        { ACTION }**

**tick-Nsec      { ACTION }**

**tick-Nms       { ACTION }**

**tick-Nmsec     { ACTION }**

**tick-Nus       { ACTION }**

**tick-Nusec     { ACTION }**

**profile-Ns     { ACTION }**

**profile-Nsec   { ACTION }**

**profile-Nms    { ACTION }**

**profile-Nmsec  { ACTION }**

**profile-Nus    { ACTION }**

**profile-Nusec  { ACTION }**

architecture overview picture reference(pnp format)

one-liners

simple event tracing

# Advanced tracing pattern

* Aggregations/histograms
* Thread locals
* Flame graphs

# Overhead/Performance

* ktap has a much shorter startup time than SystemTap (try the helloword script).
* ktap has a smaller memory footprint than SystemTap
* Some scripts show that ktap has a little lower overhead than SystemTap
(we chose two scripts to compare, function profile, stack profile.
this is not means all scripts in SystemTap have big overhead than ktap)

# FAQ

**Q: Why use a bytecode design?**

A: Using bytecode is a clean and lightweight solution,
   you do not need the GCC toolchain to compile every script; all you
   need is a ktapvm kernel module and the userspace tool called "ktap".
   Since its language uses a virtual machine design, it has a great portability.
   Suppose you are working on a multi-arch cluster; if you want to run
   a tracing script on each board, you will not need cross-compile your tracing
   scripts for all the boards. You can just use the `ktap` tool
   to run scripts right away.

   The bytecode-based design also makes execution safer than the native code
   generation approach.

   It is already observed that SystemTap is not widely used in embedded Linux systems.
   This is mainly caused by the problem of SystemTap's design decisions in its architecture design. It is a natural
   design for Red Hat and IBM, because Red Hat/IBM is focusing on the server area,
   not embedded area.

**Q: What's the differences with SystemTap and DTrace?**

A: For SystemTap, the answer is already mentioned in the above question,
   SystemTap chooses the translator design, sacrificing usability for runtime performance.
   The dependency on the GCC chain when running scripts is the problem that ktap wants to solve.

   DTrace shares the same design decision of using bytecode, so basically
   DTrace and ktap are more alike. There have been some projects aimed at porting
   DTrace from Solaris to Linux, but these efforts are still under way and are relatively slow in progress. DTrace
   has its root in Solaris, and there are many huge differences between Solaris's
   tracing infrastructure and Linux's.

   DTrace is based on D language, a language subset of C. It's a restricted
   language, like without for-looping, for safe use in production systems.
   It seems that DTrace for Linux only supports x86 architecture, doesn't work on
   PowerPC and ARM/MIPS. Obviously it's not suited for embedded Linux currently.

   DTrace uses ctf as input for debuginfo handing, compared to vmlinux for
   SystemTap.

   On the license part, DTrace is released as CDDL, which is incompatible with
   GPL. (This is why it's impossible to upstream DTrace into mainline.)

**Q: Why use a dynamically-typed language instead of a statically-typed language?**

A: It's hard to say which one is better than the other. Dynamically-typed
   languages bring efficiency and fast prototype production, but lose type
   checking at the compile phase, and it's easy to make mistake in runtime. It also
   needs many runtime checks. In contrast, statically-typed languages win on
   programming safety and performance. Statically-typed languages would suit for
   interoperation with the kernel, as the kernel is written mainly in C. Note that
   SystemTap and DTrace both use statically-typed languages.

   ktap chooses a dynamically-typed language for its initial implementation.

**Q: Why do we need ktap for event tracing? There is already a built-in ftrace**

A: This is also a common question for all dynamic tracing tools, not only ktap.
   ktap provides more flexibility than the built-in tracing infrastructure. Suppose
   you need to print a global variable at a tracepoint hit, or you want to print
   a backtrace. Furthermore, you want to store some info into an associative array, and
   display it as a histogram when tracing ends. `ftrace` cannot handle all these requirements.
   Overall, ktap provides you with great flexibility to script your own trace
   needs.

**Q: How about the performance? Is ktap slow?**

A: ktap is not slow. The bytecode is very high-level, based on Lua. The language's
   virtual machine is register-based (compared to the stack-based JVM and CLR), with a small number of
   instructions. The table data structure is heavily optimized in ktapvm.
   ktap uses per-cpu allocation in many places, without the global locking scheme.
   It is very fast when executing tracepoint callbacks.
   Performance benchmarks show that the overhead of ktap runtime is nearly
   10% (storing event name into associative array), compared to the full speed
   running time without any tracepoints enabled.

   ktap will keep optimizing unfailingly. Hopefully the overhead will
   decrease to little more than 5%, or even less.

**Q: Why not port a higher-level language, like Python or Java, directly into the kernel?**

A: I am serious on the size of VM and the memory footprint. The Python VM is too large
   for embedding into the kernel, and Python has many advanced functionalities
   which we do not really need.

   The number of bytecode opcodes of other higher level languages is also big. ktap only has 32
   bytecode opcodes, whereas Python/Java/Erlang all have nearly two hundred opcodes.
   There are also some problems when porting those languages into the kernel.
   Kernel programming is very different from userspace programming,
   like lack of floating-point numbers, handling sleeping code, deadloop is
   not allowed in the kernel, multi-thread management, etc. So it is impossible
   to port large language implementations over to the kernel environment with trivial efforts.

**Q: What is the status of ktap now?**

A: Basically it works on x86-32, x86-64, PowerPC, ARM. It also could work for
   other hardware architectures, but is not tested yet. (I don't have enough hardware to test.)
   If you find any bugs, fix it with your own programming skills, or just report to me.

**Q: How can I hack on ktap? I want to write some extensions for ktap.**

A: Patches welcome! Volunteers welcome!
   You can write your own libraries to fulfill your specific needs,
   or write scripts for fun.

**Q: What's the plan for ktap? Is there a roadmap?**

A: The current plan is to deliver stable ktapvm kernel modules, more ktap scripts,
   and more bugfixes.

# References

* [Linux Performance Analysis and Tools][REF1]
* [Dtrace Blog][REF2]
* [Dtrace User Guide][REF3]
* [LWN: ktap -- yet another kernel tracer][REF4]
* [LWN: Ktap almost gets into 3.13][REF5]
* [staging: ktap: add to the kernel tree][REF6]
* [ktap introduction in LinuxCon Japan 2013][REFR7(content is out of date)
* [ktap Examples by Brendan Gregg][REFR8
* [What Linux can learn from Solaris performance, and vice-versa][REF9]
* [Ktap or BPF?][REF10]

[REF1]: http://www.brendangregg.com/Slides/SCaLE_Linux_Performance2013.pdf
[REF2]: http://dtrace.org/blogs/
[REF3]: http://docs.huihoo.com/opensolaris/dtrace-user-guide/html/index.html
[REF4]: http://lwn.net/Articles/551314/
[REF5]: http://lwn.net/Articles/572788/
[REF6]: https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=c63a164271f81220ff4966d41218a9101f3d0ec4
[REF7]: http://events.linuxfoundation.org/sites/events/files/lcjpcojp13_zhangwei.pdf
[REF8]: http://www.brendangregg.com/ktap.html
[REF9]: http://www.slideshare.net/brendangregg/what-linux-can-learn-from-solaris-performance-and-viceversa
[REF10]: http://lwn.net/Articles/595565/

# History

* ktap was invented at 2012
* First RFC sent to LKML at 2012.12.31
* The code was released in GitHub at 2013.01.18
* ktap released v0.1 at 2013.05.21
* ktap released v0.2 at 2013.07.31
* ktap released v0.3 at 2013.10.29

For more release info, please look at RELEASES.txt in project root directory.

# Examples

1. simplest one-liner command to enable all tracepoints

        ktap -e "trace *:* { print(argstr) }"
2. syscall tracing on target process

        ktap -e "trace syscalls:* { print(argstr) }" -- ls
3. ftrace(kernel newer than 3.3, and must compiled with CONFIG_FUNCTION_TRACER)

        ktap -e "trace ftrace:function { print(argstr) }"

        ktap -e "trace ftrace:function /ip==mutex*/ { print(argstr) }"
4. simple syscall tracing

        trace syscalls:* {
                print(cpu, pid, execname, argstr)
        }
5. syscall tracing in histogram style

        var s = {}

        trace syscalls:sys_enter_* {
                s[probename] += 1
        }

        trace_end {
                print_hist(s)
        }
6. kprobe tracing

        trace probe:do_sys_open dfd=%di fname=%dx flags=%cx mode=+4($stack) {
                print("entry:", execname, argstr)
        }

        trace probe:do_sys_open%return fd=$retval {
                print("exit:", execname, argstr)
        }
7. uprobe tracing

        trace probe:/lib/libc.so.6:malloc {
                print("entry:", execname, argstr)
        }

        trace probe:/lib/libc.so.6:malloc%return {
                print("exit:", execname, argstr)
        }
8. stapsdt tracing (userspace static marker)

        trace sdt:/lib64/libc.so.6:lll_futex_wake {
                print("lll_futex_wake", execname, argstr)
        }

        or:

        #trace all static mark in libc
        trace sdt:/lib64/libc.so.6:* {
                print(execname, argstr)
        }
9. timer

        tick-1ms {
                printf("time fired on one cpu\n");
        }

        profile-2s {
                printf("time fired on every cpu\n");
        }
10. FFI (Call kernel function from ktap script, need to compile with FFI=1)

        ffi.cdef[[
                int printk(char *fmt, ...);
        ]]

        ffi.C.printk("This message is called from ktap ffi\n")

More examples can be found at [samples][samples_dir] directory.

[samples_dir]: https://github.com/ktap/ktap/tree/master/samples

# Appendix

Here is the complete syntax of ktap in extended BNF.
(based on Lua syntax: http://www.lua.org/manual/5.1/manual.html#5.1)

        chunk ::= {stat [';']} [laststat [';']

        block ::= chunk

        stat ::=  varlist '=' explist | 
                 functioncall | 
                 { block } | 
                 while exp { block } | 
                 repeat block until exp | 
                 if exp { block {elseif exp { block }} [else block] } | 
                 for Name '=' exp ',' exp [',' exp] { block } | 
                 for namelist in explist { block } | 
                 function funcname funcbody | 
                 function Name funcbody | 
                 var namelist ['=' explist] 

        laststat ::= return [explist] | break

        funcname ::= Name {'.' Name} [':' Name]

        varlist ::= var {',' var}

        var ::=  Name | prefixexp '[' exp ']'| prefixexp '.' Name 

        namelist ::= Name {',' Name}

        explist ::= {exp ',' exp

        exp ::=  nil | false | true | Number | String | '...' | function | 
                 prefixexp | tableconstructor | exp binop exp | unop exp 

        prefixexp ::= var | functioncall | '(' exp ')'

        functioncall ::=  prefixexp args | prefixexp ':' Name args 

        args ::=  '(' [explist] ')' | tableconstructor | String 

        function ::= function funcbody

        funcbody ::= '(' [parlist] ')' { block }

        parlist ::= namelist [',' '...'] | '...'

        tableconstructor ::= '{' [fieldlist] '}'

        fieldlist ::= field {fieldsep field} [fieldsep]

        field ::= '[' exp ']' '=' exp | Name '=' exp | exp

        fieldsep ::= ',' | ';'

        binop ::= '+' | '-' | '*' | '/' | '^' | '%' | '..' | 
                  '<' | '<=' | '>' | '>=' | '==' | '!=' | 
                  and | or

        unop ::= '-'

