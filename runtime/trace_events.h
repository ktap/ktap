#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 2, 0)
#include <linux/ftrace_event.h>
typedef struct ftrace_event_call trace_event_call;
#else
#include <linux/trace_events.h>
typedef struct trace_event_call trace_event_call;
#endif
