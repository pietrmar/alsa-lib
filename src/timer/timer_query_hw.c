/*
 *  Timer Interface - main file
 *  Copyright (c) 1998-2001 by Jaroslav Kysela <perex@suse.cz>
 *
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Library General Public License as
 *   published by the Free Software Foundation; either version 2 of
 *   the License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Library General Public License for more details.
 *
 *   You should have received a copy of the GNU Library General Public
 *   License along with this library; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "timer_local.h"

#define SNDRV_FILE_TIMER		"/dev/snd/timer"
#define SNDRV_TIMER_VERSION_MAX	SNDRV_PROTOCOL_VERSION(2, 0, 0)

static int snd_timer_query_hw_close(snd_timer_query_t *handle)
{
	int res;

	if (!handle)
		return -EINVAL;
	res = close(handle->poll_fd) < 0 ? -errno : 0;
	return res;
}

static int snd_timer_query_hw_next_device(snd_timer_query_t *handle, snd_timer_id_t * tid)
{
	if (!handle || !tid)
		return -EINVAL;
	if (ioctl(handle->poll_fd, SNDRV_TIMER_IOCTL_NEXT_DEVICE, tid) < 0)
		return -errno;
	return 0;
}

static snd_timer_query_ops_t snd_timer_query_hw_ops = {
	close: snd_timer_query_hw_close,
	next_device: snd_timer_query_hw_next_device,
};

int snd_timer_query_hw_open(snd_timer_query_t **handle, const char *name, int mode)
{
	int fd, ver, tmode;
	snd_timer_query_t *tmr;

	*handle = NULL;

	tmode = O_RDONLY;
	if (mode & SND_TIMER_OPEN_NONBLOCK)
		tmode |= O_NONBLOCK;	
	if ((fd = open(SNDRV_FILE_TIMER, tmode)) < 0)
		return -errno;
	if (ioctl(fd, SNDRV_TIMER_IOCTL_PVERSION, &ver) < 0) {
		close(fd);
		return -errno;
	}
	if (SNDRV_PROTOCOL_INCOMPATIBLE(ver, SNDRV_TIMER_VERSION_MAX)) {
		close(fd);
		return -SND_ERROR_INCOMPATIBLE_VERSION;
	}
	tmr = (snd_timer_query_t *) calloc(1, sizeof(snd_timer_t));
	if (tmr == NULL) {
		close(fd);
		return -ENOMEM;
	}
	tmr->type = SND_TIMER_TYPE_HW;
	tmr->mode = tmode;
	tmr->name = strdup(name);
	tmr->poll_fd = fd;
	tmr->ops = &snd_timer_query_hw_ops;
	*handle = tmr;
	return 0;
}

SND_DLSYM_BUILD_VERSION(_snd_timer_query_hw_open, SND_TIMER_QUERY_DLSYM_VERSION);
int _snd_timer_query_hw_open(snd_timer_query_t **timer, char *name,
			     snd_config_t *root ATTRIBUTE_UNUSED,
			     snd_config_t *conf, int mode)
{
	snd_config_iterator_t i, next;
	snd_config_for_each(i, next, conf) {
		snd_config_t *n = snd_config_iterator_entry(i);
		const char *id = snd_config_get_id(n);
		if (strcmp(id, "comment") == 0)
			continue;
		if (strcmp(id, "type") == 0)
			continue;
		SNDERR("Unexpected field %s", id);
		return -EINVAL;
	}
	return snd_timer_query_hw_open(timer, name, mode);
}
