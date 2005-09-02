/* 
 * Copyright 2004 Timo Hirvonen
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

#include <ip.h>
#include <nomad.h>
#include <id3.h>
#include <xmalloc.h>
#include <read_wrapper.h>
#include <debug.h>

#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/* ------------------------------------------------------------------------- */

static ssize_t read_func(void *datasource, void *buffer, size_t count)
{
	struct input_plugin_data *ip_data = datasource;

	return read_wrapper(ip_data, buffer, count);
}

static off_t lseek_func(void *datasource, off_t offset, int whence)
{
	struct input_plugin_data *ip_data = datasource;

	return lseek(ip_data->fd, offset, whence);
}

static int close_func(void *datasource)
{
	struct input_plugin_data *ip_data = datasource;

	return close(ip_data->fd);
}

static struct nomad_callbacks callbacks = {
	.read = read_func,
	.lseek = lseek_func,
	.close = close_func
};

/* ------------------------------------------------------------------------- */

static int mad_open(struct input_plugin_data *ip_data)
{
	struct nomad *nomad;
	struct nomad_info info;
	int rc, fast;

	fast = 1;
	rc = nomad_open_callbacks(&nomad, ip_data, fast, &callbacks);
	switch (rc) {
	case -NOMAD_ERROR_ERRNO:
		return -1;
	case -NOMAD_ERROR_FILE_FORMAT:
		return -IP_ERROR_FILE_FORMAT;
	}
	ip_data->private = nomad;

	nomad_info(nomad, &info);

	/* always 16-bit signed little-endian */
	ip_data->sf = sf_rate(info.sample_rate) | sf_channels(info.channels) | sf_bits(16) | sf_signed(1);
	return 0;
}

static int mad_close(struct input_plugin_data *ip_data)
{
	struct nomad *nomad;

	nomad = ip_data->private;
	nomad_close(nomad);
	ip_data->fd = -1;
	ip_data->private = NULL;
	return 0;
}

static int mad_read(struct input_plugin_data *ip_data, char *buffer, int count)
{
	struct nomad *nomad;
	
	nomad = ip_data->private;
	return nomad_read(nomad, buffer, count);
}

static int mad_seek(struct input_plugin_data *ip_data, double offset)
{
	struct nomad *nomad;
	
	nomad = ip_data->private;
	return nomad_time_seek(nomad, offset);
}

static void get_comment(struct keyval *c, int *iptr, ID3 *id3, enum id3_key key, const char *key_name)
{
	int i = *iptr;

	c[i].val = id3_get_comment(id3, key);
	if (c[i].val == NULL)
		return;
	c[i].key = xstrdup(key_name);
	*iptr = i + 1;
}

static int mad_read_comments(struct input_plugin_data *ip_data,
		struct keyval **comments)
{
	ID3 *id3;
	struct keyval *c;
	int fd, rc, save, i;

	fd = open(ip_data->filename, O_RDONLY);
	if (fd == -1) {
		return -1;
	}

	id3 = id3_new();
	rc = id3_read_tags(id3, fd, ID3_V1 | ID3_V2);
	save = errno;
	close(fd);
	errno = save;
	if (rc) {
		if (rc == -1) {
			d_print("error: %s\n", strerror(errno));
			return -1;
		}
		d_print("corrupted tag?\n");
		*comments = xnew0(struct keyval, 1);
		return 0;
	}

	c = xnew0(struct keyval, NUM_ID3_KEYS + 1);
	i = 0;
	get_comment(c, &i, id3, ID3_ARTIST, "artist");
	get_comment(c, &i, id3, ID3_ALBUM, "album");
	get_comment(c, &i, id3, ID3_TITLE, "title");
	get_comment(c, &i, id3, ID3_DATE, "date");
	get_comment(c, &i, id3, ID3_GENRE, "genre");
	get_comment(c, &i, id3, ID3_DISC, "discnumber");
	get_comment(c, &i, id3, ID3_TRACK, "tracknumber");
	*comments = c;
	id3_free(id3);
	return 0;
}

static int mad_duration(struct input_plugin_data *ip_data)
{
	struct nomad *nomad;

	nomad = ip_data->private;
	return nomad_time_total(nomad);
}

const struct input_plugin_ops ip_ops = {
	.open = mad_open,
	.close = mad_close,
	.read = mad_read,
	.seek = mad_seek,
	.read_comments = mad_read_comments,
	.duration = mad_duration
};

const char * const ip_extensions[] = { "mp3", "mp2", NULL };
const char * const ip_mime_types[] = { "audio/mpeg", "audio/x-mp3", "audio/x-mpeg", "audio/x-mpegurl", NULL };
