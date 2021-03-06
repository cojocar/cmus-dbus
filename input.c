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

#include "input.h"
#include "ip.h"
#include "pcm.h"
#include "http.h"
#include "xmalloc.h"
#include "file.h"
#include "utils.h"
#include "cmus.h"
#include "list.h"
#include "misc.h"
#include "debug.h"
#include "ui_curses.h"
#include "config/libdir.h"

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <dlfcn.h>

struct input_plugin {
	const struct input_plugin_ops *ops;
	struct input_plugin_data data;
	unsigned int open : 1;
	unsigned int eof : 1;
	int http_code;
	char *http_reason;

	/* cached duration, -1 = unset */
	int duration;

	/*
	 * pcm is converted to 16-bit signed little-endian stereo
	 * NOTE: no conversion is done if channels > 2 or bits > 16
	 */
	void (*pcm_convert)(char *, const char *, int);
	void (*pcm_convert_in_place)(char *, int);
	/*
	 * 4  if 8-bit mono
	 * 2  if 8-bit stereo or 16-bit mono
	 * 1  otherwise
	 */
	int pcm_convert_scale;
};

struct ip {
	struct list_head node;
	char *name;
	void *handle;

	const char * const *extensions;
	const char * const *mime_types;
	const struct input_plugin_ops *ops;
};

static const char * const plugin_dir = LIBDIR "/cmus/ip";
static LIST_HEAD(ip_head);

/* timeouts (ms) */
static int http_connection_timeout = 5e3;
static int http_read_timeout = 5e3;

static const char *pl_mime_types[] = {
	"audio/m3u",
	"audio/x-scpls",
	"audio/x-mpegurl"
};

static const char *get_extension(const char *filename)
{
	const char *ext;

	ext = filename + strlen(filename) - 1;
	while (ext >= filename && *ext != '/') {
		if (*ext == '.') {
			ext++;
			return ext;
		}
		ext--;
	}
	return NULL;
}

static const struct input_plugin_ops *get_ops_by_filename(const char *filename)
{
	struct ip *ip;
	const char *ext;

	ext = get_extension(filename);
	if (ext == NULL)
		return NULL;
	list_for_each_entry(ip, &ip_head, node) {
		const char * const *exts = ip->extensions;
		int i;

		for (i = 0; exts[i]; i++) {
			if (strcasecmp(ext, exts[i]) == 0)
				return ip->ops;
		}
	}
	return NULL;
}

static const struct input_plugin_ops *get_ops_by_mime_type(const char *mime_type)
{
	struct ip *ip;

	list_for_each_entry(ip, &ip_head, node) {
		const char * const *types = ip->mime_types;
		int i;

		for (i = 0; types[i]; i++) {
			if (strcasecmp(mime_type, types[i]) == 0)
				return ip->ops;
		}
	}
	return NULL;
}

static int do_http_get(struct http_get *hg, const char *uri, int redirections)
{
	GROWING_KEYVALS(h);
	int i, rc;
	const char *val;
	char *redirloc;

	d_print("%s\n", uri);

	hg->headers = NULL;
	hg->reason = NULL;
	hg->code = -1;
	hg->fd = -1;
	if (http_parse_uri(uri, &hg->uri))
		return -IP_ERROR_INVALID_URI;

	if (http_open(hg, http_connection_timeout))
		return -IP_ERROR_ERRNO;

	keyvals_add(&h, "Host", xstrdup(hg->uri.host));
	keyvals_add(&h, "User-Agent", xstrdup("cmus/" VERSION));
	keyvals_add(&h, "Icy-MetaData", xstrdup("1"));
	if (hg->uri.user && hg->uri.pass) {
		char buf[256];
		char *encoded;

		snprintf(buf, sizeof(buf), "%s:%s", hg->uri.user, hg->uri.pass);
		encoded = base64_encode(buf);
		if (encoded == NULL) {
			d_print("couldn't base64 encode '%s'\n", buf);
		} else {
			snprintf(buf, sizeof(buf), "Basic %s", encoded);
			free(encoded);
			keyvals_add(&h, "Authorization", xstrdup(buf));
		}
	}
	keyvals_terminate(&h);

	rc = http_get(hg, h.keyvals, http_read_timeout);
	keyvals_free(h.keyvals);
	switch (rc) {
	case -1:
		return -IP_ERROR_ERRNO;
	case -2:
		return -IP_ERROR_HTTP_RESPONSE;
	}

	/*
	 * FIXME: Use information from the headers, we read.
	 *
	 * especially interesting:
	 *  + icy-name
	 *  + icy-url
	 */
	d_print("HTTP response: %d %s\n", hg->code, hg->reason);
	for (i = 0; hg->headers[i].key != NULL; i++)
		d_print("  %s: %s\n", hg->headers[i].key, hg->headers[i].val);

	switch (hg->code) {
	case 200: /* OK */
		return 0;
	/*
	 * 3xx Codes (Redirections)
	 *     unhandled: 300 Multiple Choices
	 */
	case 301: /* Moved Permanently */
	case 302: /* Found */
	case 303: /* See Other */
	case 307: /* Temporary Redirect */
		val = keyvals_get_val(hg->headers, "location");
		if (!val)
			return -IP_ERROR_HTTP_RESPONSE;
		redirloc = xstrdup(val);
		http_get_free(hg);
		close(hg->fd);

		redirections++;
		if (redirections > 2)
			return -IP_ERROR_HTTP_REDIRECT_LIMIT;

		rc = do_http_get(hg, redirloc, redirections);
		free(redirloc);
		return rc;
	default:
		return -IP_ERROR_HTTP_STATUS;
	}
}

static int setup_remote(struct input_plugin *ip, const struct keyval *headers, int sock)
{
	const char *val;

	val = keyvals_get_val(headers, "Content-Type");
	if (val) {
		d_print("Content-Type: %s\n", val);
		ip->ops = get_ops_by_mime_type(val);
		if (ip->ops == NULL) {
			d_print("unsupported content type: %s\n", val);
			close(sock);
			return -IP_ERROR_FILE_FORMAT;
		}
	} else {
		const char *type = "audio/mpeg";

		d_print("assuming %s content type\n", type);
		ip->ops = get_ops_by_mime_type(type);
		if (ip->ops == NULL) {
			d_print("unsupported content type: %s\n", type);
			close(sock);
			return -IP_ERROR_FILE_FORMAT;
		}
	}

	ip->data.fd = sock;
	ip->data.metadata = xmalloc(16 * 255 + 1);

	val = keyvals_get_val(headers, "icy-metaint");
	if (val) {
		long int lint;

		if (str_to_int(val, &lint) == 0 && lint >= 0) {
			ip->data.metaint = lint;
			d_print("metaint: %d\n", ip->data.metaint);
		}
	}
	return 0;
}

struct read_playlist_data {
	struct input_plugin *ip;
	int rc;
	int count;
};

static int handle_line(void *data, const char *uri)
{
	struct read_playlist_data *rpd = data;
	struct http_get hg;

	rpd->count++;
	rpd->rc = do_http_get(&hg, uri, 0);
	if (rpd->rc) {
		rpd->ip->http_code = hg.code;
		rpd->ip->http_reason = hg.reason;
		if (hg.fd >= 0)
			close(hg.fd);

		hg.reason = NULL;
		http_get_free(&hg);
		return 0;
	}

	rpd->rc = setup_remote(rpd->ip, hg.headers, hg.fd);
	http_get_free(&hg);
	return 1;
}

static int read_playlist(struct input_plugin *ip, int sock)
{
	struct read_playlist_data rpd = { ip, 0, 0 };
	char *body;
	size_t size;

	body = http_read_body(sock, &size, http_read_timeout);
	close(sock);
	if (!body)
		return -IP_ERROR_ERRNO;

	cmus_playlist_for_each(body, size, 0, handle_line, &rpd);
	free(body);
	if (!rpd.count) {
		d_print("empty playlist\n");
		rpd.rc = -IP_ERROR_HTTP_RESPONSE;
	}
	return rpd.rc;
}

static int open_remote(struct input_plugin *ip)
{
	struct input_plugin_data *d = &ip->data;
	struct http_get hg;
	const char *val;
	int rc;

	rc = do_http_get(&hg, d->filename, 0);
	if (rc) {
		ip->http_code = hg.code;
		ip->http_reason = hg.reason;
		hg.reason = NULL;
		http_get_free(&hg);
		return rc;
	}

	val = keyvals_get_val(hg.headers, "Content-Type");
	if (val) {
		int i;

		for (i = 0; i < sizeof(pl_mime_types) / sizeof(pl_mime_types[0]); i++) {
			if (!strcasecmp(val, pl_mime_types[i])) {
				d_print("Content-Type: %s\n", val);
				http_get_free(&hg);
				return read_playlist(ip, hg.fd);
			}
		}
	}

	rc = setup_remote(ip, hg.headers, hg.fd);
	http_get_free(&hg);
	return rc;
}

static int open_file(struct input_plugin *ip)
{
	ip->ops = get_ops_by_filename(ip->data.filename);
	if (ip->ops == NULL)
		return -IP_ERROR_UNRECOGNIZED_FILE_TYPE;
	ip->data.fd = open(ip->data.filename, O_RDONLY);
	if (ip->data.fd == -1) {
		ip->ops = NULL;
		return -IP_ERROR_ERRNO;
	}
	return 0;
}

void ip_load_plugins(void)
{
	DIR *dir;
	struct dirent *d;

	dir = opendir(plugin_dir);
	if (dir == NULL) {
		error_msg("couldn't open directory `%s': %s", plugin_dir, strerror(errno));
		return;
	}
	while ((d = readdir(dir)) != NULL) {
		char filename[256];
		struct ip *ip;
		void *so;
		char *ext;

		if (d->d_name[0] == '.')
			continue;
		ext = strrchr(d->d_name, '.');
		if (ext == NULL)
			continue;
		if (strcmp(ext, ".so"))
			continue;

		snprintf(filename, sizeof(filename), "%s/%s", plugin_dir, d->d_name);

		so = dlopen(filename, RTLD_NOW);
		if (so == NULL) {
			error_msg("%s", dlerror());
			continue;
		}

		ip = xnew(struct ip, 1);

		ip->extensions = dlsym(so, "ip_extensions");
		ip->mime_types = dlsym(so, "ip_mime_types");
		ip->ops = dlsym(so, "ip_ops");
		if (!ip->extensions || !ip->mime_types || !ip->ops) {
			error_msg("%s: missing symbol", filename);
			free(ip);
			dlclose(so);
			continue;
		}

		ip->name = xstrndup(d->d_name, ext - d->d_name);
		ip->handle = so;

		list_add_tail(&ip->node, &ip_head);
	}
	closedir(dir);
}

static void ip_init(struct input_plugin *ip, char *filename)
{
	memset(ip, 0, sizeof(*ip));
	ip->http_code = -1;
	ip->pcm_convert_scale = -1;
	ip->duration = -1;
	ip->data.fd = -1;
	ip->data.filename = filename;
	ip->data.remote = is_url(filename);
}

struct input_plugin *ip_new(const char *filename)
{
	struct input_plugin *ip = xnew(struct input_plugin, 1);

	ip_init(ip, xstrdup(filename));
	return ip;
}

void ip_delete(struct input_plugin *ip)
{
	if (ip->open)
		ip_close(ip);
	free(ip->data.filename);
	free(ip);
}

int ip_open(struct input_plugin *ip)
{
	int rc;

	BUG_ON(ip->open);

	/* set fd and ops */
	if (ip->data.remote) {
		rc = open_remote(ip);
	} else {
		rc = open_file(ip);
	}

	if (rc) {
		d_print("opening `%s' failed: %d %s\n", ip->data.filename, rc,
				rc == -1 ? strerror(errno) : "");
		return rc;
	}

	rc = ip->ops->open(&ip->data);
	if (rc) {
		d_print("opening file `%s' failed: %d %s\n", ip->data.filename, rc,
				rc == -1 ? strerror(errno) : "");
		if (ip->data.fd != -1)
			close(ip->data.fd);
		free(ip->data.metadata);
		ip_init(ip, ip->data.filename);
		return rc;
	}
	ip->open = 1;
	return 0;
}

void ip_setup(struct input_plugin *ip)
{
	unsigned int bits, is_signed, channels;
	sample_format_t sf = ip->data.sf;

	bits = sf_get_bits(sf);
	is_signed = sf_get_signed(sf);
	channels = sf_get_channels(sf);

	ip->pcm_convert_scale = 1;
	ip->pcm_convert = NULL;
	ip->pcm_convert_in_place = NULL;

	if (bits <= 16 && channels <= 2) {
		unsigned int mask = ((bits >> 2) & 4) | (is_signed << 1);

		ip->pcm_convert = pcm_conv[mask | (channels - 1)];
		ip->pcm_convert_in_place = pcm_conv_in_place[mask | sf_get_bigendian(sf)];

		ip->pcm_convert_scale = (3 - channels) * (3 - bits / 8);
	}

	d_print("pcm convert: scale=%d convert=%d convert_in_place=%d\n",
			ip->pcm_convert_scale,
			ip->pcm_convert != NULL,
			ip->pcm_convert_in_place != NULL);
}

int ip_close(struct input_plugin *ip)
{
	int rc;

	rc = ip->ops->close(&ip->data);
	BUG_ON(ip->data.private);
	if (ip->data.fd != -1)
		close(ip->data.fd);
	free(ip->data.metadata);
	free(ip->http_reason);

	ip_init(ip, ip->data.filename);
	return rc;
}

int ip_read(struct input_plugin *ip, char *buffer, int count)
{
	struct timeval tv;
	fd_set readfds;
	/* 4608 seems to be optimal for mp3s, 4096 for oggs */
	char tmp[8 * 1024];
	char *buf;
	int sample_size;
	int rc;

	BUG_ON(count <= 0);

	FD_ZERO(&readfds);
	FD_SET(ip->data.fd, &readfds);
	/* zero timeout -> return immediately */
	tv.tv_sec = 0;
	tv.tv_usec = 50e3;
	rc = select(ip->data.fd + 1, &readfds, NULL, NULL, &tv);
	if (rc == -1) {
		if (errno == EINTR)
			errno = EAGAIN;
		return -1;
	}
	if (rc == 0) {
		errno = EAGAIN;
		return -1;
	}

	buf = buffer;
	if (ip->pcm_convert_scale > 1) {
		/* use tmp buffer for 16-bit mono and 8-bit */
		buf = tmp;
		count /= ip->pcm_convert_scale;
		if (count > sizeof(tmp))
			count = sizeof(tmp);
	}

	rc = ip->ops->read(&ip->data, buf, count);
	if (rc == -1 && (errno == EAGAIN || errno == EINTR)) {
		errno = EAGAIN;
		return -1;
	}
	if (rc <= 0) {
		ip->eof = 1;
		return rc;
	}

	BUG_ON((rc & ~((unsigned int)sf_get_frame_size(ip->data.sf) - 1U)) != rc);

	sample_size = sf_get_sample_size(ip->data.sf);
	if (ip->pcm_convert_in_place != NULL)
		ip->pcm_convert_in_place(buf, rc / sample_size);
	if (ip->pcm_convert != NULL)
		ip->pcm_convert(buffer, tmp, rc / sample_size);
	return rc * ip->pcm_convert_scale;
}

int ip_seek(struct input_plugin *ip, double offset)
{
	int rc;

	if (ip->data.remote)
		return -IP_ERROR_FUNCTION_NOT_SUPPORTED;
	rc = ip->ops->seek(&ip->data, offset);
	if (rc == 0)
		ip->eof = 0;
	return rc;
}

int ip_read_comments(struct input_plugin *ip, struct keyval **comments)
{
	return ip->ops->read_comments(&ip->data, comments);
}

int ip_duration(struct input_plugin *ip)
{
	if (ip->data.remote)
		return -1;
	if (ip->duration == -1)
		ip->duration = ip->ops->duration(&ip->data);
	if (ip->duration < 0)
		return -1;
	return ip->duration;
}

sample_format_t ip_get_sf(struct input_plugin *ip)
{
	BUG_ON(!ip->open);
	return ip->data.sf;
}

const char *ip_get_filename(struct input_plugin *ip)
{
	return ip->data.filename;
}

const char *ip_get_metadata(struct input_plugin *ip)
{
	BUG_ON(!ip->open);
	return ip->data.metadata;
}

int ip_is_remote(struct input_plugin *ip)
{
	return ip->data.remote;
}

int ip_metadata_changed(struct input_plugin *ip)
{
	int ret = ip->data.metadata_changed;

	BUG_ON(!ip->open);
	ip->data.metadata_changed = 0;
	return ret;
}

int ip_eof(struct input_plugin *ip)
{
	BUG_ON(!ip->open);
	return ip->eof;
}

char *ip_get_error_msg(struct input_plugin *ip, int rc, const char *arg)
{
	char buffer[1024];

	switch (-rc) {
	case IP_ERROR_ERRNO:
		snprintf(buffer, sizeof(buffer), "%s: %s", arg, strerror(errno));
		break;
	case IP_ERROR_UNRECOGNIZED_FILE_TYPE:
		snprintf(buffer, sizeof(buffer),
				"%s: unrecognized filename extension", arg);
		break;
	case IP_ERROR_FUNCTION_NOT_SUPPORTED:
		snprintf(buffer, sizeof(buffer),
				"%s: function not supported", arg);
		break;
	case IP_ERROR_FILE_FORMAT:
		snprintf(buffer, sizeof(buffer),
				"%s: file format not supported or corrupted file",
				arg);
		break;
	case IP_ERROR_INVALID_URI:
		snprintf(buffer, sizeof(buffer), "%s: invalid URI", arg);
		break;
	case IP_ERROR_SAMPLE_FORMAT:
		snprintf(buffer, sizeof(buffer),
				"%s: input plugin doesn't support the sample format",
				arg);
		break;
	case IP_ERROR_HTTP_RESPONSE:
		snprintf(buffer, sizeof(buffer), "%s: invalid HTTP response", arg);
		break;
	case IP_ERROR_HTTP_STATUS:
		snprintf(buffer, sizeof(buffer), "%s: %d %s", arg, ip->http_code, ip->http_reason);
		free(ip->http_reason);
		ip->http_reason = NULL;
		ip->http_code = -1;
		break;
	case IP_ERROR_HTTP_REDIRECT_LIMIT:
		snprintf(buffer, sizeof(buffer), "%s: too many HTTP redirections", arg);
		break;
	case IP_ERROR_INTERNAL:
		snprintf(buffer, sizeof(buffer), "%s: internal error", arg);
		break;
	case IP_ERROR_SUCCESS:
	default:
		snprintf(buffer, sizeof(buffer),
				"%s: this is not an error (%d), this is a bug",
				arg, rc);
		break;
	}
	return xstrdup(buffer);
}

char **ip_get_supported_extensions(void)
{
	struct ip *ip;
	char **exts;
	int i, size;
	int count = 0;

	size = 8;
	exts = xnew(char *, size);
	list_for_each_entry(ip, &ip_head, node) {
		const char * const *e = ip->extensions;

		for (i = 0; e[i]; i++) {
			if (count == size - 1) {
				size *= 2;
				exts = xrenew(char *, exts, size);
			}
			exts[count++] = xstrdup(e[i]);
		}
	}
	exts[count] = NULL;
	qsort(exts, count, sizeof(char *), strptrcmp);
	return exts;
}

void ip_dump_plugins(void)
{
	struct ip *ip;
	int i;

	printf("Input Plugins: %s\n", plugin_dir);
	list_for_each_entry(ip, &ip_head, node) {
		printf("  %s:\n    File Types:", ip->name);
		for (i = 0; ip->extensions[i]; i++)
			printf(" %s", ip->extensions[i]);
		printf("\n    MIME Types:");
		for (i = 0; ip->mime_types[i]; i++)
			printf(" %s", ip->mime_types[i]);
		printf("\n");
	}
}
