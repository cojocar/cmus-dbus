/* 
 * Copyright 2005 Timo Hirvonen
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

#include <ip_modplug.h>
#include <file.h>
#include <xmalloc.h>

#include <modplug.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

struct mod_private {
	ModPlugFile *file;
};

static int mod_open(struct input_plugin_data *ip_data)
{
	struct mod_private *priv;
	char *contents;
	int size, rc;
	ModPlugFile *file;
	ModPlug_Settings settings;

	size = lseek(ip_data->fd, 0, SEEK_END);
	if (size == -1)
		return -IP_ERROR_ERRNO;
	if (lseek(ip_data->fd, 0, SEEK_SET) == -1)
		return -IP_ERROR_ERRNO;

	contents = xnew(char, size);
	rc = read_all(ip_data->fd, contents, size);
	if (rc == -1) {
		int save = errno;

		free(contents);
		errno = save;
		return -IP_ERROR_ERRNO;
	}
	if (rc != size) {
		free(contents);
		return -IP_ERROR_FILE_FORMAT;
	}
	errno = 0;
	file = ModPlug_Load(contents, size);
	if (file == NULL) {
		int save = errno;

		free(contents);
		errno = save;
		if (errno == 0) {
			/* libmodplug never sets errno? */
			return -IP_ERROR_FILE_FORMAT;
		}
		return -IP_ERROR_ERRNO;
	}
	free(contents);

	priv = xnew(struct mod_private, 1);
	priv->file = file;

	ModPlug_GetSettings(&settings);
	settings.mFlags = MODPLUG_ENABLE_OVERSAMPLING | MODPLUG_ENABLE_NOISE_REDUCTION;
/* 	settings.mFlags |= MODPLUG_ENABLE_REVERB; */
/* 	settings.mFlags |= MODPLUG_ENABLE_MEGABASS; */
/* 	settings.mFlags |= MODPLUG_ENABLE_SURROUND; */
	settings.mChannels = 2;
	settings.mBits = 16;
	settings.mFrequency = 44100;
	settings.mResamplingMode = MODPLUG_RESAMPLE_FIR;
	ModPlug_SetSettings(&settings);

	ip_data->private = priv;
	ip_data->sf = sf_bits(16) | sf_rate(44100) | sf_channels(2) | sf_signed(1);
	return 0;
}

static int mod_close(struct input_plugin_data *ip_data)
{
	struct mod_private *priv = ip_data->private;

	ModPlug_Unload(priv->file);
	free(priv);
	ip_data->private = NULL;
	return 0;
}

static int mod_read(struct input_plugin_data *ip_data, char *buffer, int count)
{
	struct mod_private *priv = ip_data->private;
	int rc;
	
	errno = 0;
	rc = ModPlug_Read(priv->file, buffer, count);
	if (rc < 0) {
		if (errno == 0)
			return -IP_ERROR_INTERNAL;
		return -IP_ERROR_ERRNO;
	}
	return rc;
}

static int mod_seek(struct input_plugin_data *ip_data, double offset)
{
	struct mod_private *priv = ip_data->private;
	int ms = (int)(offset * 1000.0 + 0.5);

	/* void */
	ModPlug_Seek(priv->file, ms);
	return 0;
}

static int mod_read_comments(struct input_plugin_data *ip_data, struct comment **comments)
{
	struct mod_private *priv = ip_data->private;
	struct comment *c;
	const char *name;

	c = xnew0(struct comment, 2);
	name = ModPlug_GetName(priv->file);
	if (name != NULL && *name != 0) {
		c[0].key = xstrdup("title");
		c[0].val = xstrdup(name);
	}
	*comments = c;
	return 0;
}

static int mod_duration(struct input_plugin_data *ip_data)
{
	struct mod_private *priv = ip_data->private;

	return (ModPlug_GetLength(priv->file) + 500) / 1000;
}

const struct input_plugin_ops modplug_ip_ops = {
	.open = mod_open,
	.close = mod_close,
	.read = mod_read,
	.seek = mod_seek,
	.read_comments = mod_read_comments,
	.duration = mod_duration
};

const char * const modplug_extensions[] = { "mod", "s3m", "xm", "it", "669", "amf", "ams", "dbm", "dmf", "dsm", "far", "mdl", "med", "mtm", "okt", "ptm", "stm", "ult", "umx", "mt2", "psm", "mdz", "s3z", "xmz", "itz", "mdr", "s3r", "xmr", "itr", "mdgz", "s3gz", "xmgz", "itgz", NULL };
const char * const modplug_mime_types[] = { NULL };
