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

static struct dentry *ktap_trace_dentry;

static struct ring_buffer *buffer;

/*
 * Trace iterator - used by printout routines who present trace
 * results to users and which routines might sleep, etc:
 * Copied from struct trace_iterator in include/linux/ftrace_event.h
 */
struct ktap_trace_iterator {
	struct ring_buffer	*buffer;
	struct mutex		mutex;
	void			*private;

	/* trace_seq for __print_flags() and __print_symbolic() etc. */
	struct trace_seq	tmp_seq;

	/* The below is zeroed out in pipe_read */
	struct trace_seq	seq;
	struct trace_entry	*ent;
	unsigned long		lost_events;
	int			leftover;
	int			ent_size;
	int			cpu;
	u64			ts;

	loff_t			pos;
	long			idx;
};

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

static int trace_empty(struct ktap_trace_iterator *iter)
{
	int cpu;

	for_each_online_cpu(cpu) {
		if (!ring_buffer_empty_cpu(iter->buffer, cpu))
			return 0;
	}

	return 1;
}

static void trace_consume(struct ktap_trace_iterator *iter)
{
	ring_buffer_consume(iter->buffer, iter->cpu, &iter->ts,
			    &iter->lost_events);
}

static enum print_line_t print_trace_line(struct ktap_trace_iterator *iter)
{
	struct trace_entry *entry = iter->ent;
	char *str = (char *)(entry + 1);

	if (!trace_seq_printf(&iter->seq, "%s", str))
		return TRACE_TYPE_PARTIAL_LINE;

	return TRACE_TYPE_HANDLED;
}

static struct trace_entry *
peek_next_entry(struct ktap_trace_iterator *iter, int cpu, u64 *ts,
		unsigned long *lost_events)
{
	struct ring_buffer_event *event;

	event = ring_buffer_peek(buffer, cpu, ts, lost_events);
	if (event) {
		iter->ent_size = ring_buffer_event_length(event);
		return ring_buffer_event_data(event);
	}

	return NULL;
}

static struct trace_entry *
__find_next_entry(struct ktap_trace_iterator *iter, int *ent_cpu,
		  unsigned long *missing_events, u64 *ent_ts)
{
	struct ring_buffer *buffer = iter->buffer;
	struct trace_entry *ent, *next = NULL;
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
static void *trace_find_next_entry_inc(struct ktap_trace_iterator *iter)
{
	iter->ent = __find_next_entry(iter, &iter->cpu,
				      &iter->lost_events, &iter->ts);
	if (iter->ent)
		iter->idx++;

	return iter->ent ? iter : NULL;
}

static void poll_wait_pipe(void)
{
	set_current_state(TASK_INTERRUPTIBLE);
	/* sleep for 100 msecs, and try again. */
	schedule_timeout(HZ / 10);
}


static int tracing_wait_pipe(struct file *filp)
{
	struct ktap_trace_iterator *iter = filp->private_data;
	ktap_state *ks = iter->private;

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
	struct ktap_trace_iterator *iter = filp->private_data;
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
	       sizeof(struct ktap_trace_iterator) -
	       offsetof(struct ktap_trace_iterator, seq));
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
			  iter->ent->type);
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
	struct ktap_trace_iterator *iter;
	ktap_state *ks = inode->i_private;

	/* create a buffer to store the information to pass to userspace */
	iter = kzalloc(sizeof(*iter), GFP_KERNEL);
	if (!iter)
		return -ENOMEM;

	iter->private = ks;
	iter->buffer = buffer;
	mutex_init(&iter->mutex);
	filp->private_data = iter;

	nonseekable_open(inode, filp);

	return 0;
}

static int tracing_release_pipe(struct inode *inode, struct file *file)
{
	struct ktap_trace_iterator *iter = file->private_data;

	mutex_destroy(&iter->mutex);
	kfree(iter);
	return 0;
}

static const struct file_operations tracing_pipe_fops = {
	.open		= tracing_open_pipe,
	.read		= tracing_read_pipe,
	.splice_read	= NULL,
	.release	= tracing_release_pipe,
	.llseek		= no_llseek,
};

void kp_transport_write(ktap_state *ks, const void *data, size_t length)
{
	struct ring_buffer_event *event;
	struct trace_entry *entry;

	event = ring_buffer_lock_reserve(buffer, sizeof(*entry) + length);
	if (!event) {
		return;
	} else {
		entry = ring_buffer_event_data(event);

		//tracing_generic_entry_update(entry, flags, pc);
		tracing_generic_entry_update(entry, 0, 0);
		memcpy(entry + 1, data, length);

		ring_buffer_unlock_commit(buffer, event);
	}
}

void kp_transport_exit(ktap_state *ks)
{
	ring_buffer_free(buffer);
	debugfs_remove(ktap_trace_dentry);
}

extern struct dentry *ktap_dir;

int kp_transport_init(ktap_state *ks)
{
	buffer = ring_buffer_alloc(1000000, RB_FL_OVERWRITE);
	if (!buffer)
		return -ENOMEM;

	ktap_trace_dentry = debugfs_create_file("trace_pipe", 0444, ktap_dir,
						ks, &tracing_pipe_fops);
	if (!ktap_trace_dentry) {
		pr_err("ktapvm: cannot create trace file in debugfs\n");
		ring_buffer_free(buffer);
		return -1;
	}

	return 0;
}

