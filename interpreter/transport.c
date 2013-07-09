/*
 * transport.c - ktap transport functionality
 *
 * Copyright 2013 The ktap Project Developers.
 * See the COPYRIGHT file at the top-level directory of this distribution.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/debugfs.h>
#include <linux/ftrace_event.h>
#include "../include/ktap.h"


struct ktap_trace_entry {
	struct ftrace_event_call *call;
	struct trace_entry ent;
};

struct ktap_trace_iterator {
	struct ring_buffer	*buffer;
	struct ktap_trace_entry	*ent;
	void			*private;

	struct trace_iterator	iter;
};

enum ktap_trace_type {
	__TRACE_FIRST_TYPE = 0,

	TRACE_PRINT,
	TRACE_STACK,
	TRACE_USER_STACK,

	__TRACE_LAST_TYPE,
};

#define KTAP_TRACE_ITER(iter)	\
	container_of(iter, struct ktap_trace_iterator, iter)

ssize_t trace_seq_to_user(struct trace_seq *s, char __user *ubuf, size_t cnt)
{
	int len;
	int ret;

	if (!cnt)
		return 0;

	if (s->len <= s->readpos)
		return -EBUSY;

	len = s->len - s->readpos;
	if (cnt > len)
		cnt = len;
	ret = copy_to_user(ubuf, s->buffer + s->readpos, cnt);
	if (ret == cnt)
		return -EFAULT;

	cnt -= ret;

	s->readpos += cnt;
	return cnt;
}

static int trace_empty(struct trace_iterator *iter)
{
	struct ktap_trace_iterator *ktap_iter = KTAP_TRACE_ITER(iter);
	int cpu;

	for_each_online_cpu(cpu) {
		if (!ring_buffer_empty_cpu(ktap_iter->buffer, cpu))
			return 0;
	}

	return 1;
}

static void trace_consume(struct trace_iterator *iter)
{
	struct ktap_trace_iterator *ktap_iter = KTAP_TRACE_ITER(iter);

	ring_buffer_consume(ktap_iter->buffer, iter->cpu, &iter->ts,
			    &iter->lost_events);
}

/* todo: export kernel function ftrace_find_event in future */
static enum print_line_t print_trace_fmt(struct trace_iterator *iter)
{
	struct ktap_trace_iterator *ktap_iter = KTAP_TRACE_ITER(iter);
	struct ktap_trace_entry *entry = ktap_iter->ent;
	struct trace_event *ev;

	iter->ent = &entry->ent;

	ev = &(entry->call->event);

	if (ev) {
		int ret = ev->funcs->trace(iter, 0, ev);

		/* overwrite '\n' at the ending */
		iter->seq.buffer[iter->seq.len - 1] = '\0';
		return ret;
	}

	return TRACE_TYPE_PARTIAL_LINE;
}

static enum print_line_t print_trace_line(struct trace_iterator *iter)
{
	struct ktap_trace_iterator *ktap_iter = KTAP_TRACE_ITER(iter);
	struct ktap_trace_entry *entry = ktap_iter->ent;
	char *str = (char *)(entry + 1);

	if (entry->ent.type == TRACE_PRINT) {
		if (!trace_seq_printf(&iter->seq, "%s", str))
			return TRACE_TYPE_PARTIAL_LINE;

		return TRACE_TYPE_HANDLED;
	}

	return print_trace_fmt(iter);
}

static struct ktap_trace_entry *
peek_next_entry(struct trace_iterator *iter, int cpu, u64 *ts,
		unsigned long *lost_events)
{
	struct ktap_trace_iterator *ktap_iter = KTAP_TRACE_ITER(iter);
	struct ring_buffer_event *event;

	event = ring_buffer_peek(ktap_iter->buffer, cpu, ts, lost_events);
	if (event) {
		iter->ent_size = ring_buffer_event_length(event);
		return ring_buffer_event_data(event);
	}

	return NULL;
}

static struct ktap_trace_entry *
__find_next_entry(struct trace_iterator *iter, int *ent_cpu,
		  unsigned long *missing_events, u64 *ent_ts)
{
	struct ktap_trace_iterator *ktap_iter = KTAP_TRACE_ITER(iter);
	struct ring_buffer *buffer = ktap_iter->buffer;
	struct ktap_trace_entry *ent, *next = NULL;
	unsigned long lost_events = 0, next_lost = 0;
	u64 next_ts = 0, ts;
	int next_cpu = -1;
	int next_size = 0;
	int cpu;

	for_each_online_cpu(cpu) {
		if (ring_buffer_empty_cpu(buffer, cpu))
			continue;

		ent = peek_next_entry(iter, cpu, &ts, &lost_events);
		/*
		 * Pick the entry with the smallest timestamp:
		 */
		if (ent && (!next || ts < next_ts)) {
			next = ent;
			next_cpu = cpu;
			next_ts = ts;
			next_lost = lost_events;
			next_size = iter->ent_size;
		}
	}

	iter->ent_size = next_size;

	if (ent_cpu)
		*ent_cpu = next_cpu;

	if (ent_ts)
		*ent_ts = next_ts;

	if (missing_events)
		*missing_events = next_lost;

	return next;
}

/* Find the next real entry, and increment the iterator to the next entry */
static void *trace_find_next_entry_inc(struct trace_iterator *iter)
{
	struct ktap_trace_iterator *ktap_iter = KTAP_TRACE_ITER(iter);

	ktap_iter->ent = __find_next_entry(iter, &iter->cpu,
				      &iter->lost_events, &iter->ts);
	if (ktap_iter->ent)
		iter->idx++;

	return ktap_iter->ent ? ktap_iter : NULL;
}

static void poll_wait_pipe(void)
{
	set_current_state(TASK_INTERRUPTIBLE);
	/* sleep for 100 msecs, and try again. */
	schedule_timeout(HZ / 10);
}


static int tracing_wait_pipe(struct file *filp)
{
	struct trace_iterator *iter = filp->private_data;
	struct ktap_trace_iterator *ktap_iter = KTAP_TRACE_ITER(iter);
	ktap_state *ks = ktap_iter->private;

	while (trace_empty(iter)) {

		if ((filp->f_flags & O_NONBLOCK)) {
			return -EAGAIN;
		}

		mutex_unlock(&iter->mutex);

		poll_wait_pipe();

		mutex_lock(&iter->mutex);

		if (G(ks)->exit && trace_empty(iter)) {
			flush_signals(current);
			return -EINTR;
		}
	}

	return 1;
}

static ssize_t
tracing_read_pipe(struct file *filp, char __user *ubuf, size_t cnt, loff_t *ppos)
{
	struct trace_iterator *iter = filp->private_data;
	struct ktap_trace_iterator *ktap_iter = KTAP_TRACE_ITER(iter);
	ssize_t sret;

	/* return any leftover data */
	sret = trace_seq_to_user(&iter->seq, ubuf, cnt);
	if (sret != -EBUSY)
		return sret;
	/*
	 * Avoid more than one consumer on a single file descriptor
	 * This is just a matter of traces coherency, the ring buffer itself
	 * is protected.
	 */
	mutex_lock(&iter->mutex);

waitagain:
	sret = tracing_wait_pipe(filp);
	if (sret <= 0)
		goto out;

	/* stop when tracing is finished */
	if (trace_empty(iter)) {
		sret = 0;
		goto out;
	}

	if (cnt >= PAGE_SIZE)
		cnt = PAGE_SIZE - 1;

	/* reset all but tr, trace, and overruns */
	memset(&iter->seq, 0,
	       sizeof(struct trace_iterator) -
	       offsetof(struct trace_iterator, seq));
	iter->pos = -1;

	while (trace_find_next_entry_inc(iter) != NULL) {
		enum print_line_t ret;
		int len = iter->seq.len;

		ret = print_trace_line(iter);
		if (ret == TRACE_TYPE_PARTIAL_LINE) {
			/* don't print partial lines */
			iter->seq.len = len;
			break;
		}
		if (ret != TRACE_TYPE_NO_CONSUME)
			trace_consume(iter);

		if (iter->seq.len >= cnt)
			break;

		/*
		 * Setting the full flag means we reached the trace_seq buffer
		 * size and we should leave by partial output condition above.
		 * One of the trace_seq_* functions is not used properly.
		 */
		WARN_ONCE(iter->seq.full, "full flag set for trace type %d",
			  ktap_iter->ent->ent.type);
	}

	/* Now copy what we have to the user */
	sret = trace_seq_to_user(&iter->seq, ubuf, cnt);
	if (iter->seq.readpos >= iter->seq.len)
		trace_seq_init(&iter->seq);

	/*
	 * If there was nothing to send to user, in spite of consuming trace
	 * entries, go back to wait for more entries.
	 */
	if (sret == -EBUSY)
		goto waitagain;

out:
	mutex_unlock(&iter->mutex);

	return sret;
}

static int tracing_open_pipe(struct inode *inode, struct file *filp)
{
	struct ktap_trace_iterator *ktap_iter;
	ktap_state *ks = inode->i_private;

	/* create a buffer to store the information to pass to userspace */
	ktap_iter = kzalloc(sizeof(*ktap_iter), GFP_KERNEL);
	if (!ktap_iter)
		return -ENOMEM;

	ktap_iter->private = ks;
	ktap_iter->buffer = G(ks)->buffer;
	mutex_init(&ktap_iter->iter.mutex);
	filp->private_data = &ktap_iter->iter;

	nonseekable_open(inode, filp);

	return 0;
}

static int tracing_release_pipe(struct inode *inode, struct file *file)
{
	struct trace_iterator *iter = file->private_data;
	struct ktap_trace_iterator *ktap_iter = KTAP_TRACE_ITER(iter);

	mutex_destroy(&iter->mutex);
	kfree(ktap_iter);
	return 0;
}

static const struct file_operations tracing_pipe_fops = {
	.open		= tracing_open_pipe,
	.read		= tracing_read_pipe,
	.splice_read	= NULL,
	.release	= tracing_release_pipe,
	.llseek		= no_llseek,
};

void kp_transport_event_write(ktap_state *ks, struct ktap_event *e)
{
	struct ring_buffer *buffer = G(ks)->buffer;
	struct ring_buffer_event *event;
	struct ktap_trace_entry *entry;

	event = ring_buffer_lock_reserve(buffer, sizeof(struct ftrace_event_call *) +
						 e->entry_size);
	if (!event) {
		return;
	} else {
		entry = ring_buffer_event_data(event);

		entry->call = e->call;
		memcpy(&entry->ent, e->entry, e->entry_size);

		ring_buffer_unlock_commit(buffer, event);
	}
}
void kp_transport_write(ktap_state *ks, const void *data, size_t length)
{
	struct ring_buffer *buffer = G(ks)->buffer;
	struct ring_buffer_event *event;
	struct ktap_trace_entry *entry;
	int size;

	size = sizeof(struct ktap_trace_entry) + length;

	event = ring_buffer_lock_reserve(buffer, size);
	if (!event) {
		return;
	} else {
		entry = ring_buffer_event_data(event);

		tracing_generic_entry_update(&entry->ent, 0, 0);
		entry->ent.type = TRACE_PRINT;
		entry->call = NULL;
		memcpy(entry + 1, data, length);

		ring_buffer_unlock_commit(buffer, event);
	}
}

void kp_transport_exit(ktap_state *ks)
{
	ring_buffer_free(G(ks)->buffer);
	debugfs_remove(G(ks)->trace_pipe_dentry);
}

extern struct dentry *ktap_dir;

int kp_transport_init(ktap_state *ks)
{
	struct ring_buffer *buffer;
	struct dentry *dentry;
	char filename[32] = {0};

	buffer = ring_buffer_alloc(1000000, RB_FL_OVERWRITE);
	if (!buffer)
		return -ENOMEM;

	sprintf(filename, "trace_pipe_%d", (int)task_tgid_vnr(current));

	dentry = debugfs_create_file(filename, 0444, ktap_dir,
				     ks, &tracing_pipe_fops);
	if (!dentry) {
		pr_err("ktapvm: cannot create trace_pipe file in debugfs\n");
		ring_buffer_free(buffer);
		return -1;
	}

	G(ks)->buffer = buffer;
	G(ks)->trace_pipe_dentry = dentry;

	return 0;
}
