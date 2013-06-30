/*
 * eventdef.c - ktap eventdef parser
 *
 * Copyright 2013 The ktap Project Developers.
 * See the COPYRIGHT file at the top-level directory of this distribution.
 *
 * tracepoint parse code is based on perf(linux/tools/perf/util/parse-events.c)
 *
 * Note that lots of functions in this file could reuse perf's code
 * in future, we want ktap's eventdef compatiable with perf as much
 * as possible.
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

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>

#include "../include/ktap_types.h"
#include "../include/ktap_opcodes.h"
#include "ktapc.h"

static char tracing_events_path[] = "/sys/kernel/debug/tracing/events";

#define IDS_ARRAY_SIZE 4096
static u8 *ids_array;

static int get_digit_len(int id)
{
	int len = -1;

	if (id < 10)
		len = 1;
	else if (id < 100)
		len = 2;
	else if (id < 1000)
		len = 3;
	else if (id < 10000)
		len = 4;
	else if (id < 100000)
		len = 5;

	return len;
}

static char *get_idstr()
{
	char *idstr, *ptr;
	int total_len = 0;
	int i;

	for (i = 0; i < IDS_ARRAY_SIZE*8; i++) {
		if (ids_array[i/8] & (1 << (i%8)))
			total_len += get_digit_len(i) + 1;
	}

	idstr = malloc(total_len);
	if (!idstr)
		return NULL;

	memset(idstr, 0, total_len);
	ptr = idstr;
	for (i = 0; i < IDS_ARRAY_SIZE*8; i++) {
		if (ids_array[i/8] & (1 << (i%8))) {
			char digits[32] = {0};
			int len;

			sprintf(digits, "%d ", i);
			len = strlen(digits);
			strncpy(ptr, digits, len);
			ptr += len;
		}
	}
	*(ptr - 1) = '\0';

	return idstr;
}

static int add_event(char *evtid_path)
{
	char id_buf[24];
	int id, fd;

	fd = open(evtid_path, O_RDONLY);
	if (fd < 0) {
		/*
		 * some tracepoint doesn't have id file, like ftrace,
		 * return success in here, and don't print error.
		 */
		return 0;
	}

	if (read(fd, id_buf, sizeof(id_buf)) < 0) {
		fprintf(stderr, "read file error %s\n", evtid_path);
		close(fd);
		return -1;
	}

	id = atoll(id_buf);

	if (id >= IDS_ARRAY_SIZE * 8) {
		fprintf(stderr, "tracepoint id(%d) is bigger than %d\n", id,
				IDS_ARRAY_SIZE * 8);
		return -1;
	}

	/* ftrace:function id is 1, but we cannot enable this tracepoint */
	if (id == 1)
		return 0;

	ids_array[id/8] = ids_array[id/8] | (1 << (id%8));

	return 0;
}

static int add_tracepoint(char *sys_name, char *evt_name)
{
	char evtid_path[PATH_MAX] = {0};


	snprintf(evtid_path, PATH_MAX, "%s/%s/%s/id", tracing_events_path,
					sys_name, evt_name);
	return add_event(evtid_path);
}

static int add_tracepoint_multi_event(char *sys_name, char *evt_name)
{
	char evt_path[PATH_MAX];
	struct dirent *evt_ent;
	DIR *evt_dir;
	int ret = 0;

	snprintf(evt_path, PATH_MAX, "%s/%s", tracing_events_path, sys_name);
	evt_dir = opendir(evt_path);
	if (!evt_dir) {
		perror("Can't open event dir");
		return -1;
	}

	while (!ret && (evt_ent = readdir(evt_dir))) {
		if (!strcmp(evt_ent->d_name, ".")
		    || !strcmp(evt_ent->d_name, "..")
		    || !strcmp(evt_ent->d_name, "enable")
		    || !strcmp(evt_ent->d_name, "filter"))
			continue;

		if (!strglobmatch(evt_ent->d_name, evt_name))
			continue;

		ret = add_tracepoint(sys_name, evt_ent->d_name);
	}

	closedir(evt_dir);
	return ret;
}

static int add_tracepoint_event(char *sys_name, char *evt_name)
{
	return strpbrk(evt_name, "*?") ?
	       add_tracepoint_multi_event(sys_name, evt_name) :
	       add_tracepoint(sys_name, evt_name);
}

static int add_tracepoint_multi_sys(char *sys_name, char *evt_name)
{
	struct dirent *events_ent;
	DIR *events_dir;
	int ret = 0;

	events_dir = opendir(tracing_events_path);
	if (!events_dir) {
		perror("Can't open event dir");
		return -1;
	}

	while (!ret && (events_ent = readdir(events_dir))) {
		if (!strcmp(events_ent->d_name, ".")
		    || !strcmp(events_ent->d_name, "..")
		    || !strcmp(events_ent->d_name, "enable")
		    || !strcmp(events_ent->d_name, "header_event")
		    || !strcmp(events_ent->d_name, "header_page"))
			continue;

		if (!strglobmatch(events_ent->d_name, sys_name))
			continue;

		ret = add_tracepoint_event(events_ent->d_name,
					   evt_name);
	}

	closedir(events_dir);
	return ret;
}

static int parse_events_add_tracepoint(char *sys, char *event)
{
	if (strpbrk(sys, "*?"))
		return add_tracepoint_multi_sys(sys, event);
	else
		return add_tracepoint_event(sys, event);
}

#define KPROBE_EVENTS_PATH "/sys/kernel/debug/tracing/kprobe_events"

static int parse_events_add_kprobe(char *old_event)
{
	static int event_seq = 0;
	char probe_event[128] = {0};
	char event_id_path[128] = {0};
	char *event;
	char *r;
	int fd;
	int ret;

	fd = open(KPROBE_EVENTS_PATH, O_WRONLY);
	if (fd < 0) {
		fprintf(stderr, "Cannot open %s\n", KPROBE_EVENTS_PATH);
		return -1;
	}

	event = strdup(old_event);

	r = strstr(event, "%return");
	if (r) {
		memset(r, ' ', 7);
		sprintf(probe_event, "r:kprobes/kp%d %s", event_seq, event);
	} else
		sprintf(probe_event, "p:kprobes/kp%d %s", event_seq, event);

	ret = write(fd, probe_event, strlen(probe_event));
	if (ret <= 0) {
		fprintf(stderr, "Cannot write %s to %s\n", probe_event, KPROBE_EVENTS_PATH);
		close(fd);
		return -1;
	}

	close(fd);

	sprintf(event_id_path, "/sys/kernel/debug/tracing/events/kprobes/kp%d/id",
			event_seq);
	ret = add_event(event_id_path);
	if (ret < 0)
		return -1;

	event_seq++;
	return 0;
}

#define UPROBE_EVENTS_PATH "/sys/kernel/debug/tracing/uprobe_events"

static int parse_events_add_uprobe(char *old_event)
{
	static int event_seq = 0;
	char probe_event[128] = {0};
	char event_id_path[128] = {0};
	char *event;
	char *r;
	int fd;
	int ret;

	fd = open(UPROBE_EVENTS_PATH, O_WRONLY);
	if (fd < 0) {
		fprintf(stderr, "Cannot open %s\n", UPROBE_EVENTS_PATH);
		return -1;
	}

	event = strdup(old_event);

	r = strstr(event, "%return");
	if (r) {
		memset(r, ' ', 7);
		sprintf(probe_event, "r:uprobes/kp%d %s", event_seq, event);
	} else
		sprintf(probe_event, "p:uprobes/kp%d %s", event_seq, event);

	printf("[probe event] %s\n", probe_event);
	ret = write(fd, probe_event, strlen(probe_event));
	if (ret <= 0) {
		fprintf(stderr, "Cannot write %s to %s\n", probe_event, UPROBE_EVENTS_PATH);
		close(fd);
		return -1;
	}

	close(fd);

	sprintf(event_id_path, "/sys/kernel/debug/tracing/events/uprobes/kp%d/id",
			event_seq);
	ret = add_event(event_id_path);
	if (ret < 0)
		return -1;

	event_seq++;
	return 0;
}

static int parse_events_add_probe(char *old_event)
{
	char *separator;

	separator = strchr(old_event, ':');
	if (!separator || (separator == old_event))
		return parse_events_add_kprobe(old_event);
	else
		return parse_events_add_uprobe(old_event);
}

static int parse_events_add_stapsdt(char *old_event)
{
	printf("Currently ktap don't support stapsdt, please waiting\n");

	return -1;
}

ktap_string *ktapc_parse_eventdef(ktap_string *eventdef)
{
	const char *def_str = getstr(eventdef);
	char sys[128] = {0}, event[128] = {0}, *separator, *idstr;
	int ret;

	if (!ids_array) {
		ids_array = malloc(IDS_ARRAY_SIZE);
		if (!ids_array)
			return NULL;
	}
	memset(ids_array, 0, IDS_ARRAY_SIZE);

	separator = strchr(def_str, ':');
	if (!separator || (separator == def_str)) {
		return NULL;
	}

	strncpy(sys, def_str, separator - def_str);
	strcpy(event, separator+1);

	if (!strcmp(sys, "probe"))
		ret = parse_events_add_probe(event);
	else if (!strcmp(sys, "stapsdt"))
		ret = parse_events_add_stapsdt(event);
	else
		ret = parse_events_add_tracepoint(sys, event);

	if (ret)
		return NULL;

	idstr = get_idstr();
	return ktapc_ts_new(idstr);
}

