/* 
 * Copyright 2004-2005 Timo Hirvonen
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include "mixer.h"
#include "op.h"
#include "utils.h"
#include "xmalloc.h"
#include "debug.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#if defined(__OpenBSD__)
#include <soundcard.h>
#else
#include <sys/soundcard.h>
#endif

enum {
	OSS_MIXER_CHANNEL_VOLUME,
	OSS_MIXER_CHANNEL_BASS,
	OSS_MIXER_CHANNEL_TREBLE,
	OSS_MIXER_CHANNEL_SYNTH,
	OSS_MIXER_CHANNEL_PCM,
	OSS_MIXER_CHANNEL_SPEAKER,
	OSS_MIXER_CHANNEL_LINE,
	OSS_MIXER_CHANNEL_MIC,
	OSS_MIXER_CHANNEL_CD,
	OSS_MIXER_CHANNEL_IMIX,
	OSS_MIXER_CHANNEL_ALTPCM,
	OSS_MIXER_CHANNEL_RECLEV,
	OSS_MIXER_CHANNEL_IGAIN,
	OSS_MIXER_CHANNEL_OGAIN,
	OSS_MIXER_CHANNEL_LINE1,
	OSS_MIXER_CHANNEL_LINE2,
	OSS_MIXER_CHANNEL_LINE3,
	OSS_MIXER_CHANNEL_DIGITAL1,
	OSS_MIXER_CHANNEL_DIGITAL2,
	OSS_MIXER_CHANNEL_DIGITAL3,
	OSS_MIXER_CHANNEL_PHONEIN,
	OSS_MIXER_CHANNEL_PHONEOUT,
	OSS_MIXER_CHANNEL_VIDEO,
	OSS_MIXER_CHANNEL_RADIO,
	OSS_MIXER_CHANNEL_MONITOR,
	OSS_MIXER_CHANNEL_MAX
};

static int mixer_fd = -1;
static int mixer_devmask;
/* static int mixer_recmask; */
/* static int mixer_recsrc; */
/* static int mixer_stereodevs; */
static char mixer_channels[OSS_MIXER_CHANNEL_MAX];

/* configuration */
static char *oss_mixer_device = NULL;
static int oss_volume_controls_pcm = 1;

static int mixer_open(const char *device)
{
	int i;

	mixer_fd = open(device, O_RDWR);
	if (mixer_fd == -1)
		return -1;
	ioctl(mixer_fd, SOUND_MIXER_READ_DEVMASK, &mixer_devmask);
/* 	ioctl(mixer_fd, SOUND_MIXER_READ_RECMASK, &mixer_recmask); */
/* 	ioctl(mixer_fd, SOUND_MIXER_READ_RECSRC, &mixer_recsrc); */
/* 	ioctl(mixer_fd, SOUND_MIXER_READ_STEREODEVS, &mixer_stereodevs); */
	i = 0;
	while (i < min(SOUND_MIXER_NRDEVICES, OSS_MIXER_CHANNEL_MAX)) {
		mixer_channels[i] = (mixer_devmask >> i) & 1;
		i++;
	}
	while (i < OSS_MIXER_CHANNEL_MAX)
		mixer_channels[i++] = 0;
	return 0;
}

static int mixer_set_level(int channel, int l, int r)
{
	int tmp;

	tmp = (l & 0x7f) + ((r & 0x7f) << 8);
	if (ioctl(mixer_fd, MIXER_WRITE(channel), &tmp) == -1)
		return -1;
	return 0;
}

static int mixer_get_level(int channel, int *l, int *r)
{
	int tmp;

	if (ioctl(mixer_fd, MIXER_READ(channel), &tmp) == -1)
		return -1;
	*l = tmp & 0x7f;
	*r = (tmp >> 8) & 0x7f;
	return 0;
}

static int oss_device_exists(const char *device)
{
	struct stat s;

	if (stat(device, &s) == 0) {
		d_print("device %s exists\n", device);
		return 1;
	}
	d_print("device %s does not exist\n", device);
	return 0;
}

static int oss_mixer_init(void)
{
	const char *new_mixer_dev = "/dev/sound/mixer";
	const char *mixer_dev = "/dev/mixer";

	if (oss_mixer_device) {
		if (oss_device_exists(oss_mixer_device))
			return 0;
		free(oss_mixer_device);
		oss_mixer_device = NULL;
		return -1;
	}
	if (oss_device_exists(new_mixer_dev)) {
		oss_mixer_device = xstrdup(new_mixer_dev);
		return 0;
	}
	if (oss_device_exists(mixer_dev)) {
		oss_mixer_device = xstrdup(mixer_dev);
		return 0;
	}
	return -1;
}

static int oss_mixer_exit(void)
{
	if (oss_mixer_device) {
		free(oss_mixer_device);
		oss_mixer_device = NULL;
	}
	return 0;
}

static int oss_mixer_open(int *volume_max)
{
	*volume_max = 100;
	if (mixer_open(oss_mixer_device) == 0)
		return 0;
	return -1;
}

static int oss_mixer_close(void)
{
	close(mixer_fd);
	mixer_fd = -1;
	return 0;
}

static int oss_mixer_set_volume(int l, int r)
{
	if (oss_volume_controls_pcm) {
		return mixer_set_level(OSS_MIXER_CHANNEL_PCM, l, r);
	} else {
		return mixer_set_level(OSS_MIXER_CHANNEL_VOLUME, l, r);
	}
}

static int oss_mixer_get_volume(int *l, int *r)
{
	if (oss_volume_controls_pcm) {
		return mixer_get_level(OSS_MIXER_CHANNEL_PCM, l, r);
	} else {
		return mixer_get_level(OSS_MIXER_CHANNEL_VOLUME, l, r);
	}
}

static int oss_mixer_set_option(int key, const char *val)
{
	switch (key) {
	case 0:
		if (strcasecmp(val, "pcm") == 0) {
			oss_volume_controls_pcm = 1;
		} else if (strcasecmp(val, "master") == 0) {
			oss_volume_controls_pcm = 0;
		} else {
			errno = EINVAL;
			return -OP_ERROR_ERRNO;
		}
		break;
	case 1:
		free(oss_mixer_device);
		oss_mixer_device = xstrdup(val);
		break;
	default:
		return -OP_ERROR_NOT_OPTION;
	}
	return 0;
}

static int oss_mixer_get_option(int key, char **val)
{
	switch (key) {
	case 0:
		if (oss_volume_controls_pcm) {
			*val = xstrdup("PCM");
		} else {
			*val = xstrdup("Master");
		}
		break;
	case 1:
		if (oss_mixer_device)
			*val = xstrdup(oss_mixer_device);
		break;
	default:
		return -OP_ERROR_NOT_OPTION;
	}
	return 0;
}

const struct mixer_plugin_ops op_mixer_ops = {
	.init = oss_mixer_init,
	.exit = oss_mixer_exit,
	.open = oss_mixer_open,
	.close = oss_mixer_close,
	.set_volume = oss_mixer_set_volume,
	.get_volume = oss_mixer_get_volume,
	.set_option = oss_mixer_set_option,
	.get_option = oss_mixer_get_option
};

const char * const op_mixer_options[] = {
	"channel",
	"device",
	NULL
};
