/* 
 * Copyright 2007 dnk <dnk@bjum.net>
 * Based on oss.c
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

#include "op.h"
#include "sf.h"
#include "utils.h"
#include "xmalloc.h"
#include "debug.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

static HWAVEOUT wave_out;
static sample_format_t waveout_sf;
static int buffer_size = 4096;
static int buffer_count = 12;
static WAVEHDR *buffers;
static int buffer_idx;
static int buffers_free;

#define FRAME_SIZE_ALIGN(x) \
	(((x) / sf_get_frame_size(waveout_sf)) * sf_get_frame_size(waveout_sf))

static void waveout_error(const char *name, int rc)
{
	const char *errstr = "UNKNOWN";

	switch (rc) {
	case MMSYSERR_ALLOCATED:	errstr = "MMSYSERR_ALLOCATED"; break;
	case MMSYSERR_INVALHANDLE:	errstr = "MMSYSERR_INVALHANDLE"; break;
	case MMSYSERR_NODRIVER:		errstr = "MMSYSERR_NODRIVER"; break;
	case MMSYSERR_BADDEVICEID:	errstr = "MMSYSERR_BADDEVICEID"; break;
	case MMSYSERR_NOMEM:		errstr = "MMSYSERR_NOMEM"; break;
	case MMSYSERR_NOTSUPPORTED:	errstr = "MMSYSERR_NOTSUPPORTED"; break;
	case WAVERR_STILLPLAYING:	errstr = "WAVERR_STILLPLAYING"; break;
	case WAVERR_UNPREPARED:		errstr = "WAVERR_UNPREPARED"; break;
	case WAVERR_BADFORMAT:		errstr = "WAVERR_BADFORMAT"; break;
	case WAVERR_SYNC:		errstr = "WAVERR_SYNC"; break;
	}

	d_print("%s returned error %s (%d)\n", name, errstr, rc);
}

static void clean_buffers(void)
{
	int i;

	/* mark used buffers clean */
	for (i = 0; i < buffer_count; i++) {
		WAVEHDR *hdr = &buffers[(buffer_idx + i) % buffer_count];

		if (!(hdr->dwFlags & WHDR_DONE))
			break;
		buffers_free++;
		waveOutUnprepareHeader(wave_out, hdr, sizeof(WAVEHDR));
		hdr->dwFlags = 0;
	}
}

static int waveout_open(sample_format_t sf)
{
	WAVEFORMATEX  format;
	int rc, i;

	/* WAVEFORMATEX does not support channels > 2, waveOutWrite() wants little endian signed PCM */
	if (sf_get_bigendian(sf) || !sf_get_signed(sf) || sf_get_channels(sf) > 2) {
		return -OP_ERROR_SAMPLE_FORMAT;
	}

	memset(&format, 0, sizeof(format));
	format.cbSize = sizeof(format);
	format.wFormatTag = WAVE_FORMAT_PCM;
	format.nChannels = sf_get_channels(sf);
	format.nSamplesPerSec = sf_get_rate(sf);
	format.wBitsPerSample = sf_get_bits(sf);
	format.nAvgBytesPerSec = sf_get_second_size(sf);
	format.nBlockAlign = sf_get_frame_size(sf);

	rc = waveOutOpen(&wave_out, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL);
	if (rc != MMSYSERR_NOERROR) {
		switch (rc) {
		case MMSYSERR_ALLOCATED:
			errno = EBUSY;
			return -OP_ERROR_ERRNO;
		case MMSYSERR_BADDEVICEID:
		case MMSYSERR_NODRIVER:
			errno = ENODEV;
			return -OP_ERROR_ERRNO;
		case MMSYSERR_NOMEM:
			errno = ENOMEM;
			return -OP_ERROR_ERRNO;
		case WAVERR_BADFORMAT:
			return -OP_ERROR_SAMPLE_FORMAT;
		}
		return -OP_ERROR_INTERNAL;
	}

	/* reset buffers */
	for (i = 0; i < buffer_count; i++) {
		buffers[i].dwFlags = 0;
	}
	buffer_idx = 0;
	buffers_free = buffer_count;

	waveout_sf = sf;

	return 0;
}

static int waveout_close(void)
{
	int rc;

	waveOutReset(wave_out);

	clean_buffers();

	rc = waveOutClose(wave_out);
	if (rc != MMSYSERR_NOERROR) {
		waveout_error("waveOutClose", rc);
		return -1;
	}
	wave_out = NULL;

	return 0;
}

static int waveout_init(void)
{
	WAVEHDR *hdr;
	int i;

	/* create buffers */
	buffers = xnew(WAVEHDR, buffer_count);
	for (i = 0; i < buffer_count; i++) {
		hdr = &buffers[i];

		memset(hdr, 0, sizeof(WAVEHDR));
		hdr->lpData = xmalloc(buffer_size);
	}
	return 0;
}

static int waveout_exit(void)
{
	int i;

	for (i = 0; i < buffer_count; i++) {
		free(buffers[i].lpData);
	}
	free(buffers);
	buffers = NULL;

	return 0;
}

static int waveout_write(const char *buffer, int count)
{
	int written = 0;
	int len, rc;

	count = FRAME_SIZE_ALIGN(count);

	clean_buffers();

	while (count > 0) {
		WAVEHDR *hdr = &buffers[buffer_idx];

		if (hdr->dwFlags != 0) {
			/* no free buffers */
			break;
		}

		len = FRAME_SIZE_ALIGN(min(count, buffer_size));
		hdr->dwBufferLength = len;
		memcpy(hdr->lpData, buffer + written, len);

		rc = waveOutPrepareHeader(wave_out, hdr, sizeof(WAVEHDR));
		if (rc != MMSYSERR_NOERROR) {
			waveout_error("waveOutPrepareHeader", rc);
			break;
		}

		rc = waveOutWrite(wave_out, hdr, sizeof(WAVEHDR));
		if (rc != MMSYSERR_NOERROR) {
			waveOutUnprepareHeader(wave_out, hdr, sizeof(WAVEHDR));
			hdr->dwFlags = 0;
			waveout_error("waveOutWrite", rc);
			break;
		}
 
		written += len;
		count -= len;
		buffer_idx = (buffer_idx + 1) % buffer_count;
		buffers_free--;
	}

	return written;
}

static int waveout_pause(void)
{
	if (waveOutPause(wave_out) != MMSYSERR_NOERROR)
		return -1;
	return 0;
}

static int waveout_unpause(void)
{
	if (waveOutRestart(wave_out) != MMSYSERR_NOERROR)
		return -1;
	return 0;
}

static int waveout_buffer_space(void)
{
	clean_buffers();
	if (buffers_free == 0)
		return -1;
	return buffers_free * FRAME_SIZE_ALIGN(buffer_size);
}

static int waveout_set_option(int key, const char *val)
{
	long int ival;
	int reinit = 0;

	switch (key) {
	case 0:
		if (str_to_int(val, &ival) || ival < 4096 || ival > 65536) {
			errno = EINVAL;
			return -OP_ERROR_ERRNO;
		}
		if (buffers) {
			waveout_exit();
			reinit = 1;
		}
		buffer_size = ival;
		break;
	case 1:
		if (str_to_int(val, &ival) || ival < 2 || ival > 64) {
			errno = EINVAL;
			return -OP_ERROR_ERRNO;
		}
		if (buffers) {
			waveout_exit();
			reinit = 1;
		}
		buffer_count = ival;
		break;
	default:
		return -OP_ERROR_NOT_OPTION;
	}

	if (reinit) {
		waveout_init();
	}

	return 0;
}

static int waveout_get_option(int key, char **val)
{
	switch (key) {
	case 0:
		*val = xnew(char, 22);
		snprintf(*val, 22, "%d", buffer_size);
		break;
	case 1:
		*val = xnew(char, 22);
		snprintf(*val, 22, "%d", buffer_count);
		break;
	default:
		return -OP_ERROR_NOT_OPTION;
	}
	return 0;
}

const struct output_plugin_ops op_pcm_ops = {
	.init = waveout_init,
	.exit = waveout_exit,
	.open = waveout_open,
	.close = waveout_close,
	.write = waveout_write,
	.pause = waveout_pause,
	.unpause = waveout_unpause,
	.buffer_space = waveout_buffer_space,
	.set_option = waveout_set_option,
	.get_option = waveout_get_option
};

const char * const op_pcm_options[] = {
	"buffer_size",
	"buffer_count",
	NULL
};

const int op_priority = 0;
