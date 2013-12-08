% The ktap Tutorial

# Introduction

ktap is a new script-based dynamic tracing tool for linux
http://www.ktap.org

ktap is a new script-based dynamic tracing tool for Linux,
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
* small and lightweight
* not depend on gcc for each script running
* easy to use in embedded environment without debugging info
* support for tracepoint, kprobe, uprobe, function trace, timer, and more
* supported in x86, arm, ppc, mips
* safety in sandbox


# Getting started

Requirements

* Linux 3.1 or later(Need some kernel patches for kernel earlier than 3.1)
* CONFIG_EVENT_TRACING enabled
* CONFIG_PERF_EVENTS enabled
* CONFIG_DEBUG_FS enabled  
     make sure debugfs mounted before insmod ktapvm  
     mount debugfs: mount -t debugfs none /sys/kernel/debug/
* libelf (optional)
     Install elfutils-libelf-devel on RHEL-based distros, or libelf-dev on
     Debian-based distros.
     Use `make NO_LIBELF=1` to build without libelf support.
     libelf is required for resolving symbols to addresses in DSO, and for sdt.

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

ktap's syntax is design on the mind of C language syntax friendly,
to make it easy scripting by kernel developer.

1. Variable declaration
The biggest syntax differences with C is that ktap is a dynamic typed
language, so you won't need add any variable type declaration, just
use the variable.

2. function
All functions in ktap should use keyword "function" declaration

3. comments
The comments of ktap is starting from '#', long comments doesn't support now.

4. others
Don't need place any ';' at the ending of statement in ktap.
ktap use free syntax style, so you can choose to use the ';' or not.

ktap use nil as NULL, the result of any number operate on nil is nil.

ktap don't have array structure, also don't have any pointer operation.

## Control structures

ktap if/else is same as C language.

There have two method of for-loop in ktap:

    for (i = init, limit, step) { body }

this is same as below in C:

    for (i = init; i < limit; i += step) { body }

The next for-loop method is:

    for (k, v in pairs(t)) { body }   # looping all elements of table

Note that ktap don't have "continue" keyword, but C does.

## Date structures

Associative array is heavily used in ktap, it's also called by table.

table declaration:

    t = {}

how to use table:  

    t[1] = 1
    t[1] = "xxx"
    t["key"] = 10
    t["key"] = "value"

    for (k, v in pairs(t)) { body }   # looping all elements of table


# Built in functions and librarys

## Built in functions

**print (...)**  
Receives any number of arguments, and prints their values,
print is not intended for formatted output, but only as a
quick way to show a value, typically for debugging.
For formatted output, use printf.

**printf (fmt, ...)**  
Similar with C printf, use for format string output.

**pairs (t)**  
Returns three values: the next function, the table t, and nil,
so that the construction
for (k,v in pairs(t)) { body }
will iterate over all key-value pairs of table t.

**len (t) /len (s)**  
If the argument is string, return length of string,
if the argument is table, return counts of table pairs.

**in_interrupt ()**  
checking is context is interrupt context
  
**exit ()**  
quit ktap executing, similar with exit syscall

**pid ()**  
return current process pid

**tid ()**  
return current thread id

**uid ()**  
return current process uid

**execname ()**  
return current process exec name string

**cpu ()**  
return current cpu id

**arch ()**  
return machine architecture, like x86, arm, etc.

**kernel_v ()**  
return Linux kernel version string, like 3.9, etc.

**user_string (addr)**  
Receive userspace address, read string from userspace, return string.

**histogram (t)**  
Receive table, output table histogram to user.

**curr_task_info (offset, fetch_bytes)**  
fetch value in field offset of task_struct structure, argument fetch_bytes
could be 4 or 8, if fetch_bytes is not given, default is 4.

user may need to get field offset by gdb, for example:
gdb vmlinux
(gdb)p &(((struct task_struct *)0).prio)

**print_backtrace ()**  
print current task stack info


## Librarys

### Kdebug Library

**kdebug.probe_by_id (event_ids, eventfun)**

This function is underly representation of high level tracing primitive.
event_ids is the id of all events, it's read from  
/sys/kernel/debug/tracing/events/$SYS/$EVENT/id

for multi-events tracing, the event_ids is concatenation of all id, for example:
 "2 3 4", seperated by blank space.

The second argument in above examples is a function:  
function eventfun () { action }


**kdebug.probe_end (endfunc)**  

This function is used for invoking a function when tracing end, it will wait
until user press CTRL+C to stop tracing, then ktap will call endfunc function, 
user could show tracing results in that function, or do other things.

User don't have to use kdebug library directly, use trace/trace_end keyword.

### Timer Library



# Linux tracing basics

tracepoints, probe, timer  
filters  
above explaintion  
Ring buffer

# Tracing semantics in ktap

## Tracing block

**trace EVENTDEF /FILTER/ { ACTION }**

This is the basic tracing block for ktap, you need to use a specific EVENTDEF
string, and own event function.

There have four type of EVENTDEF, tracepoint, kprobe, uprobe, sdt. 

- tracepoint:  

	EventDef               Description 
	--------------------   -------------------------------
	syscalls:*             trace all syscalls events
	syscalls:sys_enter_*   trace all syscalls entry events
	kmem:*                 trace all kmem related events
	sched:*                trace all sched related events
	sched:sched_switch     trace sched_switch tracepoint
	\*:\*                  trace all tracepoints in system

	All tracepoint events are based on:  
	          /sys/kernel/debug/tracing/events/$SYS/$EVENT

- ftrace(kernel must compiled with CONFIG_FUNCTION_TRACER)

	EventDef               Description 
	--------------------   -------------------------------
	ftrace:function        trace kernel functions based on ftrace

	User need to use filter (/ip==*/) to trace specfic functions.
	Function must be listed in /sys/kernel/debug/tracing/available_filter_functions

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

## Tracing built-in variables

**argevent**  
event object, you can print it by: print(argevent), it will print events
into human readable string, the result is mostly same as each entry of
/sys/kernel/debug/tracing/trace

**argname**  
event name, each event have a name associated with it.

**arg1..9**  
get argument 1..9 of event object.

> ***Note*** of arg offset
>
> The arg offset(1..9) is determined by event format shown in debugfs.
>
>     #cat /sys/kernel/debug/tracing/events/sched/sched_switch/format
>     name: sched_switch
>     ID: 268
>     format:
>         field:char prev_comm[32];         <- arg1
>         field:pid_t prev_pid;             <- arg2
>         field:int prev_prio;              <- arg3
>         field:long prev_state;            <- arg4
>         field:char next_comm[32];         <- arg5
>         field:pid_t next_pid;             <- arg6
>         field:int next_prio;              <- arg7
>
> As shown, tracepoint event sched:sched_switch have 7 arguments, from arg1 to
> arg7.
>
> Need to note that arg1 of syscall event is syscall number, not first argument
> of syscall function. Use arg2 as first argument of syscall function.
> For example:
>
>     SYSCALL_DEFINE3(read, unsigned int, fd, char __user *, buf, size_t, count)
>                                         <arg2>             <arg3>       <arg4>
>
> This is similar with kprobe and uprobe, the arg1 of kprobe/uprobe event
> always is _probe_ip, not the first argument given by user, for example:
> 
>     # ktap -e 'trace probe:/lib64/libc.so.6:malloc size=%di'
>
>     # cat /sys/kernel/debug/tracing/events/ktap_uprobes_3796/malloc/format
>         field:unsigned long __probe_ip;   <- arg1
>         field:u64 size;                   <- arg2


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

Aggregation/Histogram  
thread local  
flame graph

# Overhead/Performance

ktap have more fast boot time thant Systemtap(try the helloword script)  
ktap have little memory usage than Systemtap  
and some scripts show that ktap have a little overhead than Systemtap
(we choosed two scripts to compare, function profile, stack profile.
this is not means all scripts in Systemtap have big overhead than ktap)


# FAQ

**Q: Why use bytecode design?**  
A: Using bytecode would be a clean and lightweight solution,
   you don't need gcc toolchain to compile every scripts, all you
   need is a ktapvm kernel modules and userspace tool called ktap.
   Since its language virtual machine design, it have great portability,
   suppose you are working at a multi-arch cluster, if you want to run
   a tracing script on each board, you won't need cross-compile tracing
   script onto all board, what you really need to do is use ktap tool
   to run script just in time.

   Bytecode based design also will make executing more safer, than native code
   generation.

   Reality already showing that SystemTap is not widely used in embedded Linux,
   caused by problem of SystemTap's architecture design choice, it's a natural
   design for Redhat and IBM, because Redhat/IBM is focusing on server area,
   not embedded area.

**Q: What's the differences with SystemTap and Dtrace?**  
A: For SystemTap, the answer is already mentioned at above question,
   SystemTap use translator design, for trade-off on performance with usability,
   based on GCC, that's what ktap want to solve.

   For Dtrace, one common design with Dtrace is also use bytecode, so basically
   Dtrace and ktap is on the same road. There have some projects aim to porting
   Dtrace from Solaris to Linux, but the process is still on the road, Dtrace
   is rooted in Solaris, and there have many huge differences between Solaris
   tracing infrastructure with Linux's.

   Dtrace is based on D language, a language subset of C, it's a restricted
   language, like without for-looping, for safty use in production system.
   It seems that Dtrace for Linux only support x86 architecture, not work on
   powerpc and arm/mips, obviously it's not suit for embedded Linux currently.

   Dtrace use ctf as input for debuginfo handing, compare with vmlinux for
   SystemTap.

   On the license part, Dtrace is released as CDDL, which is incompatible with
   GPL(this is why it's impossible to upstream Dtrace into mainline).

**Q: Why use dynamically typed language? but not statically typed language?**  
A: It's hard to say which one is more better than other, dynamically typed
   language bring efficiency and fast prototype production, but loosing type
   check at compiling phase, and easy to make mistake in runtime, also it's
   need many runtime checking, In contrast, statically typed language win on
   programing safety, and performance. Statically language would suit for
   interoperate with kernel, as kernel is wrote mainly in C, Need to note that
   SystemTap and Dtrace both is statically language.

   ktap choose dynamically typed language as initial implementation.

**Q: Why we need ktap for event tracing? There already have a built-in ftrace**  
A: This also is a common question for all dynamic tracing tool, not only ktap.
   ktap provide more flexibility than built-in tracing infrastructure. Suppose
   you need print a global variable when tracepoint hit, or you want print
   backtrace, even more, you want to store some info into associative array, and
   display it in histogram style when tracing end, in these case, some of them
   ftrace can take it, some of them ftrace can not.
   Overall, ktap provide you with great flexibility to scripting your own trace
   need.

**Q: How about the performance? Is ktap slow?**  
A: ktap is not slow, the bytecode is very high-level, based on lua, the language
   virtual machine is register-based(compare with stack-based), with little
   instruction, the table data structure is heavily optimized in ktapvm.
   ktap use per-cpu allocation in many place, without global locking scheme,
   it's very fast when executing tracepoint callback.
   Performance benchmark showing that the overhead of ktap running is nearly
   10%(store event name into associative array), compare with full speed
   running without any tracepoint enabled.

   ktap will optimize overhead all the time, hopefully the overhead will
   decrease to little than 5%, even more.

**Q: Why not porting a high level language implementation into kernel directly?
   Like python/JVM?**  
A: I take serious on the size of vm and memory footprint. Python vm is large,
   it's not suit to embed into kernel, and python have some functionality
   which we don't need.

   The bytecode of other high level language is also big, ktap only have 32
   bytecodes, python/java/erlang have nearly two hundred bytecodes.
   There also have some problems when porting those language into kernel,
   userspace programming have many differences with kernel programming,
   like float numbers, handle sleeping code carefully in kernel, deadloop is
   not allowed in kernel, multi-thread management, etc.., so it's impossible
   to porting language implementation into kernel with little adaption work.

**Q: What's the status of ktap now?**  
A: Basically it works on x86-32, x86-64, powerpc, arm, it also could work for
   other hardware architecture, but not proven yet(I don't have enough hardware
   to test)
   If you found some bug, fix it on you own programming skill, or report to me.

**Q: How to hack ktap? I want to write some extensions onto ktap.**  
A: welcome hacking.  
   You can write your own library to fulfill your specific need,
   you can write any script as you want.

**Q: What's the plan of ktap? any roadmap?**  
A: the current plan is deliver stable ktapvm kernel modules, more ktap script,
   and bugfix.


# References

* [Linux Performance Analysis and Tools][LPAT]
* [Dtrace Blog][dtraceblog]
* [Dtrace User Guide][dug]
* [LWN: ktap -- yet another kernel tracer][lwn1]
* [LWN: Ktap almost gets into 3.13][lwn2]
* [staging: ktap: add to the kernel tree][ktap_commit]
* [ktap introduction in LinuxCon Japan 2013][lcj](content is out of date)
* [ktap Examples by Brendan Gregg][KEBG]

[LPAT]: http://www.brendangregg.com/Slides/SCaLE_Linux_Performance2013.pdf
[dtraceblog]: http://dtrace.org/blogs/
[dug]: http://docs.huihoo.com/opensolaris/dtrace-user-guide/html/index.html
[lwn1]: http://lwn.net/Articles/551314/
[lwn2]: http://lwn.net/Articles/572788/
[ktap_commit]: https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=c63a164271f81220ff4966d41218a9101f3d0ec4
[lcj]: http://events.linuxfoundation.org/sites/events/files/lcjpcojp13_zhangwei.pdf
[KEBG]: http://www.brendangregg.com/ktap.html

# History

* ktap was invented at 2002
* First RFC sent to LKML at 2012.12.31
* The code was released in github at 2013.01.18
* ktap released v0.1 at 2013.05.21
* ktap released v0.2 at 2013.07.31
* ktap released v0.3 at 2013.10.29

For more release info, please look at RELEASES.txt in project root directory.

# Examples

1. simplest one-liner command to enable all tracepoints

        ktap -e "trace *:* { print(argevent) }"

2. syscall tracing on target process

        ktap -e "trace syscalls:* { print(argevent) }" -- ls

3. function tracing

        ktap -e "trace ftrace:function { print(argevent) }"

        ktap -e "trace ftrace:function /ip==mutex*/ { print(argevent) }"

4. simple syscall tracing

        trace syscalls:* {
                print(cpu(), pid(), execname(), argevent)
        }

5. syscall tracing in histogram style

        s = {}

        trace syscalls:sys_enter_* {
                s[argname] += 1
        }

        trace_end {
                histogram(s)
        }

6. kprobe tracing

        trace probe:do_sys_open dfd=%di fname=%dx flags=%cx mode=+4($stack) {
                print("entry:", execname(), argevent)
        }

        trace probe:do_sys_open%return fd=$retval {
                print("exit:", execname(), argevent)
        }

7. uprobe tracing

        trace probe:/lib/libc.so.6:malloc {
                print("entry:", execname(), argevent)
        }

        trace probe:/lib/libc.so.6:malloc%return {
                print("exit:", execname(), argevent)
        }

8. stapsdt tracing (userspace static marker)

        trace sdt:/lib64/libc.so.6:lll_futex_wake {
                print("lll_futex_wake", execname(), argevent)
        }

        or:

        #trace all static mark in libc
        trace sdt:/lib64/libc.so.6:* {
                print(execname(), argevent)
        }

9. timer

        tick-1ms {
                printf("time fired on one cpu\n");
        }

        profile-2s {
                printf("time fired on every cpu\n");
        }

10. FFI (Call kernel function from ktap script, need compile with FFI=1)

        cdef[[
                int printk(char *fmt, ...);
        ]]

        C.printk("This message is called from ktap ffi\n")

More examples can be found at [samples][samples_dir] directory.

[samples_dir]: https://github.com/ktap/ktap/tree/master/samples

# Appendix

Here is the complete syntax of ktap in extended BNF.
(based on lua syntax: http://www.lua.org/manual/5.1/manual.html#5.1)

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
                 local function Name funcbody | 
                 local namelist ['=' explist] 

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

