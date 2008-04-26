/*
 * Copyright 2005 Timo Hirvonen
 */

#include "id3.h"
#include "comment.h"
#include "xmalloc.h"
#include "utf8_encode.h"
#include "uchar.h"
#include "options.h"
#include "debug.h"

#include <unistd.h>
#include <inttypes.h>
#include <errno.h>
#include <stdio.h>

/*
 * position:
 *
 *    0 "ID3"
 *  -10 "3DI"
 * -128 "TAG"
 * -138 "3DI"
 *
 * if v2 is at beginning _and_ at end then there must be a seek tag at beginning
 */

struct v2_header {
	unsigned char ver_major;
	unsigned char ver_minor;
	unsigned char flags;
	uint32_t size;
};

struct v2_extended_header {
	uint32_t size;
};

struct v2_frame_header {
	char id[4];
	uint32_t size;
	uint16_t flags;
};

#define V2_HEADER_UNSYNC	(1 << 7)
#define V2_HEADER_EXTENDED	(1 << 6)
#define V2_HEADER_EXPERIMENTAL	(1 << 5)
#define V2_HEADER_FOOTER	(1 << 4)

#define V2_FRAME_COMPRESSED	(1 << 3) /* great idea!!1 */
#define V2_FRAME_ENCRYPTHED	(1 << 2) /* wow, this is very neat! */
#define V2_FRAME_UNSYNC		(1 << 1)
#define V2_FRAME_LEN_INDICATOR	(1 << 0)

#define NR_GENRES 148
/* genres {{{ */
static const char *genres[NR_GENRES] = {
	"Blues",
	"Classic Rock",
	"Country",
	"Dance",
	"Disco",
	"Funk",
	"Grunge",
	"Hip-Hop",
	"Jazz",
	"Metal",
	"New Age",
	"Oldies",
	"Other",
	"Pop",
	"R&B",
	"Rap",
	"Reggae",
	"Rock",
	"Techno",
	"Industrial",
	"Alternative",
	"Ska",
	"Death Metal",
	"Pranks",
	"Soundtrack",
	"Euro-Techno",
	"Ambient",
	"Trip-Hop",
	"Vocal",
	"Jazz+Funk",
	"Fusion",
	"Trance",
	"Classical",
	"Instrumental",
	"Acid",
	"House",
	"Game",
	"Sound Clip",
	"Gospel",
	"Noise",
	"Alt",
	"Bass",
	"Soul",
	"Punk",
	"Space",
	"Meditative",
	"Instrumental Pop",
	"Instrumental Rock",
	"Ethnic",
	"Gothic",
	"Darkwave",
	"Techno-Industrial",
	"Electronic",
	"Pop-Folk",
	"Eurodance",
	"Dream",
	"Southern Rock",
	"Comedy",
	"Cult",
	"Gangsta Rap",
	"Top 40",
	"Christian Rap",
	"Pop/Funk",
	"Jungle",
	"Native American",
	"Cabaret",
	"New Wave",
	"Psychedelic",
	"Rave",
	"Showtunes",
	"Trailer",
	"Lo-Fi",
	"Tribal",
	"Acid Punk",
	"Acid Jazz",
	"Polka",
	"Retro",
	"Musical",
	"Rock & Roll",
	"Hard Rock",
	"Folk",
	"Folk/Rock",
	"National Folk",
	"Swing",
	"Fast-Fusion",
	"Bebob",
	"Latin",
	"Revival",
	"Celtic",
	"Bluegrass",
	"Avantgarde",
	"Gothic Rock",
	"Progressive Rock",
	"Psychedelic Rock",
	"Symphonic Rock",
	"Slow Rock",
	"Big Band",
	"Chorus",
	"Easy Listening",
	"Acoustic",
	"Humour",
	"Speech",
	"Chanson",
	"Opera",
	"Chamber Music",
	"Sonata",
	"Symphony",
	"Booty Bass",
	"Primus",
	"Porn Groove",
	"Satire",
	"Slow Jam",
	"Club",
	"Tango",
	"Samba",
	"Folklore",
	"Ballad",
	"Power Ballad",
	"Rhythmic Soul",
	"Freestyle",
	"Duet",
	"Punk Rock",
	"Drum Solo",
	"A Cappella",
	"Euro-House",
	"Dance Hall",
	"Goa",
	"Drum & Bass",
	"Club-House",
	"Hardcore",
	"Terror",
	"Indie",
	"BritPop",
	"Negerpunk",
	"Polsk Punk",
	"Beat",
	"Christian Gangsta Rap",
	"Heavy Metal",
	"Black Metal",
	"Crossover",
	"Contemporary Christian",
	"Christian Rock",
	"Merengue",
	"Salsa",
	"Thrash Metal",
	"Anime",
	"JPop",
	"Synthpop"
};
/* }}} */

#if 1
#define id3_debug(...) d_print(__VA_ARGS__)
#else
#define id3_debug(...) do { } while (0)
#endif

const char * const id3_key_names[NUM_ID3_KEYS] = {
	"artist",
	"album",
	"title",
	"date",
	"genre",
	"discnumber",
	"tracknumber",
	"albumartist",
	"artistsort",
	"albumartistsort",
	"compilation",
	"replaygain_track_gain",
	"replaygain_track_peak",
	"replaygain_album_gain",
	"replaygain_album_peak"
};

static int utf16_is_special(const uchar uch)
{
	if (UTF16_IS_HSURROGATE(uch) || UTF16_IS_LSURROGATE(uch) || UTF16_IS_BOM(uch))
		return -1;
	return 0;
}

static char *utf16_to_utf8(const unsigned char *buf, int buf_size)
{
	char *out;
	int i, idx;

	out = xnew(char, (buf_size / 2) * 4 + 1);
	i = idx = 0;
	while (buf_size - i >= 2) {
		uchar u;

		u = buf[i] + (buf[i + 1] << 8);
		if (u_is_unicode(u)) {
			if (utf16_is_special(u) == 0)
				u_set_char(out, &idx, u);
		} else {
			free(out);
			return NULL;
		}
		if (u == 0)
			return out;
		i += 2;
	}
	u_set_char(out, &idx, 0);
	return out;
}

static char *utf16be_to_utf8(const unsigned char *buf, int buf_size)
{
	char *out;
	int i, idx;

	out = xnew(char, (buf_size / 2) * 4 + 1);
	i = 0;
	idx = 0;
	while (buf_size - i >= 2) {
		uchar u;

		u = buf[i + 1] + (buf[i] << 8);
		if (u_is_unicode(u)) {
		       if (utf16_is_special(u) == 0)
				u_set_char(out, &idx, u);
		} else {
			free(out);
			return NULL;
		}
		if (u == 0)
			return out;
		i += 2;
	}
	u_set_char(out, &idx, 0);
	return out;
}

static int is_v1(const char *buf)
{
	return buf[0] == 'T' && buf[1] == 'A' && buf[2] == 'G';
}

static int u32_unsync(const unsigned char *buf, uint32_t *up)
{
	uint32_t b, u = 0;
	int i;

	for (i = 0; i < 4; i++) {
		b = buf[i];
		if (b >= 0x80)
			return 0;
		u <<= 7;
		u |= b;
	}
	*up = u;
	return 1;
}

static void get_u32(const unsigned char *buf, uint32_t *up)
{
	uint32_t b, u = 0;
	int i;

	for (i = 0; i < 4; i++) {
		b = buf[i];
		u <<= 8;
		u |= b;
	}
	*up = u;
}

static void get_u24(const unsigned char *buf, uint32_t *up)
{
	uint32_t b, u = 0;
	int i;

	for (i = 0; i < 3; i++) {
		b = buf[i];
		u <<= 8;
		u |= b;
	}
	*up = u;
}

static int v2_header_footer_parse(struct v2_header *header, const char *buf)
{
	const unsigned char *b = (const unsigned char *)buf;

	header->ver_major = b[3];
	header->ver_minor = b[4];
	header->flags = b[5];
	if (header->ver_major == 0xff || header->ver_minor == 0xff)
		return 0;
	return u32_unsync(b + 6, &header->size);
}

static int v2_header_parse(struct v2_header *header, const char *buf)
{
	if (buf[0] != 'I' || buf[1] != 'D' || buf[2] != '3')
		return 0;
	return v2_header_footer_parse(header, buf);
}

static int v2_footer_parse(struct v2_header *header, const char *buf)
{
	if (buf[0] != '3' || buf[1] != 'D' || buf[2] != 'I')
		return 0;
	return v2_header_footer_parse(header, buf);
}

static int v2_extended_header_parse(struct v2_extended_header *header, const char *buf)
{
	return u32_unsync((const unsigned char *)buf, &header->size);
}

static int is_frame_id_char(char ch)
{
	return (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
}

/* XXXYYY
 *
 * X = [A-Z0-9]
 * Y = byte
 *
 * XXX is frame
 * YYY is frame size excluding this 6 byte header
 */
static int v2_2_0_frame_header_parse(struct v2_frame_header *header, const char *buf)
{
	int i;

	for (i = 0; i < 3; i++) {
		if (!is_frame_id_char(buf[i]))
			return 0;
		header->id[i] = buf[i];
	}
	header->id[3] = 0;
	get_u24((const unsigned char *)(buf + 3), &header->size);
	header->flags = 0;
	if (header->size == 0)
		return 0;
	id3_debug("%c%c%c %d\n", header->id[0], header->id[1], header->id[2], header->size);
	return 1;
}

/* XXXXYYYYZZ
 *
 * X = [A-Z0-9]
 * Y = byte
 * Z = byte
 *
 * XXXX is frame
 * YYYY is frame size excluding this 10 byte header
 * ZZ   is flags
 */
static int v2_3_0_frame_header_parse(struct v2_frame_header *header, const char *buf)
{
	int i;

	for (i = 0; i < 4; i++) {
		if (!is_frame_id_char(buf[i]))
			return 0;
		header->id[i] = buf[i];
	}
	get_u32((const unsigned char *)(buf + 4), &header->size);
	header->flags = (buf[8] << 8) | buf[9];
	if (header->size == 0)
		return 0;
	id3_debug("%c%c%c%c %d\n", header->id[0], header->id[1], header->id[2],
			header->id[3], header->size);
	return 1;
}

/* same as 2.3 but header size is sync safe */
static int v2_4_0_frame_header_parse(struct v2_frame_header *header, const char *buf)
{
	int i;

	for (i = 0; i < 4; i++) {
		if (!is_frame_id_char(buf[i]))
			return 0;
		header->id[i] = buf[i];
	}
	if (!u32_unsync((const unsigned char *)(buf + 4), &header->size))
		return 0;
	header->flags = (buf[8] << 8) | buf[9];
	if (header->size == 0)
		return 0;
	id3_debug("%c%c%c%c %d\n", header->id[0], header->id[1], header->id[2],
			header->id[3], header->size);
	return 1;
}

static int read_all(int fd, char *buf, size_t size)
{
	size_t pos = 0;

	while (pos < size) {
		int rc = read(fd, buf + pos, size - pos);

		if (rc == -1) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			return -1;
		}
		pos += rc;
	}
	return 0;
}

static char *parse_genre(const char *str)
{
	int parenthesis = 0;
	long int idx;
	char *end;

	if (strncasecmp(str, "(RX", 3) == 0)
		return xstrdup("Remix");

	if (strncasecmp(str, "(CR", 3) == 0)
		return xstrdup("Cover");

	if (*str == '(') {
		parenthesis = 1;
		str++;
	}

	idx = strtol(str, &end, 10);
	if (str != end) {
		/* Number parsed but there may be some crap after the number.
		 * I don't care, ID3v2 by definition contains crap.
		 */
		if (idx >= 0 && idx < NR_GENRES)
			return xstrdup(genres[idx]);
	}

	if (parenthesis) {
		const char *ptr = strchr(str, ')');

		if (ptr && ptr[1]) {
			/* genre name after random crap in parenthesis,
			 * return the genre name */
			return xstrdup(ptr + 1);
		}
		str--;
	}

	/* random crap, just return it and wait for a bug report */
	return xstrdup(str);
}

/* http://www.id3.org/id3v2.4.0-structure.txt */
static struct {
	const char name[8];
	enum id3_key key;
} frame_tab[] = {
	/* 2.4.0 */
	{ "TDRC", ID3_DATE }, // recording date
	{ "TDRL", ID3_DATE }, // release date
	{ "TDOR", ID3_DATE }, // original release date
	{ "TSOP", ID3_ARTISTSORT },

	/* >= 2.3.0 */
	{ "TPE1", ID3_ARTIST },
	{ "TALB", ID3_ALBUM },
	{ "TIT2", ID3_TITLE },
	{ "TYER", ID3_DATE },
	{ "TCON", ID3_GENRE },
	{ "TPOS", ID3_DISC },
	{ "TRCK", ID3_TRACK },
	{ "TPE2", ID3_ALBUMARTIST },
	{ "XSOP", ID3_ARTISTSORT }, // obsolete
	{ "TCMP", ID3_COMPILATION },

	/* obsolete frames (2.2.0) */
	{ "TP1",  ID3_ARTIST },
	{ "TAL",  ID3_ALBUM },
	{ "TT2",  ID3_TITLE },
	{ "TYE",  ID3_DATE },
	{ "TCO",  ID3_GENRE },
	{ "TPA",  ID3_DISC },
	{ "TRK",  ID3_TRACK },

	{ "", -1 }
};

static int frame_tab_index(const char *id)
{
	int i;

	for (i = 0; frame_tab[i].key != -1; i++) {
		if (!strncmp(id, frame_tab[i].name, 4))
			return i;
	}
	return -1;
}

static void fix_date(char *buf)
{
	const char *ptr = buf;
	int ch, len = 0;

	do {
		ch = *ptr++;
		if (ch >= '0' && ch <= '9') {
			len++;
			continue;
		}
		if (len == 4) {
			// number which length is 4, must be year
			memmove(buf, ptr - 5, 4);
			buf[4] = 0;
			return;
		}
		len = 0;
	} while (ch);
	*buf = 0;
}

static char *decode_str(const char *buf, int len, int encoding)
{
	char *in, *out = NULL;
	int rc = 0;

	switch (encoding) {
	case 0x00: /* ISO-8859-1 */
		in = xstrndup(buf, len);
		rc = utf8_encode(in, id3_default_charset, &out);
		free(in);
		break;
	case 0x03: /* UTF-8 */
		in = xstrndup(buf, len);
		if (u_is_valid(in)) {
			out = in;
		} else {
			rc = utf8_encode(in, id3_default_charset, &out);
			free(in);
		}
		break;
	case 0x01: /* UTF-16 */
		out = utf16_to_utf8((const unsigned char *)buf, len);
		break;
	case 0x02: /* UTF-16BE */
		out = utf16be_to_utf8((const unsigned char *)buf, len);
		break;
	}
	return out;
}

static void add_v2(struct id3tag *id3, enum id3_key key, char *value)
{
	free(id3->v2[key]);
	id3->v2[key] = value;
	id3->has_v2 = 1;
}

static void decode_normal(struct id3tag *id3, const char *buf, int len, int encoding, enum id3_key key)
{
	char *out = decode_str(buf, len, encoding);

	if (!out)
		return;

	if (key == ID3_GENRE) {
		char *tmp;

		id3_debug("genre before: '%s'\n", out);
		tmp = parse_genre(out);
		free(out);
		out = tmp;
	}
	if (key == ID3_DATE) {
		id3_debug("date before: '%s'\n", out);
		fix_date(out);
		if (!*out) {
			id3_debug("date parsing failed\n");
			free(out);
			return;
		}
	}
	add_v2(id3, key, out);
}

static void decode_txxx(struct id3tag *id3, const char *buf, int len, int encoding)
{
	enum id3_key key = NUM_ID3_KEYS;
	int size;
	char *out;

	out = decode_str(buf, len, encoding);
	if (!out)
		return;

	id3_debug("TXXX, key = '%s'\n", out);
	if (!strcasecmp(out, "replaygain_track_gain"))
		key = ID3_RG_TRACK_GAIN;
	if (!strcasecmp(out, "replaygain_track_peak"))
		key = ID3_RG_TRACK_PEAK;
	if (!strcasecmp(out, "replaygain_album_gain"))
		key = ID3_RG_ALBUM_GAIN;
	if (!strcasecmp(out, "replaygain_album_peak"))
		key = ID3_RG_ALBUM_PEAK;
	if (!strcasecmp(out, "album artist"))
		key = ID3_ALBUMARTIST;
	if (!strcasecmp(out, "albumartistsort"))
		key = ID3_ALBUMARTISTSORT;
	if (!strcasecmp(out, "compilation"))
		key = ID3_COMPILATION;

	size = strlen(out) + 1;
	free(out);

	if (key == NUM_ID3_KEYS)
		return;

	buf += size;
	len -= size;
	if (len <= 0)
		return;

	out = decode_str(buf, len, encoding);
	if (!out)
		return;

	add_v2(id3, key, out);
}

static void v2_add_frame(struct id3tag *id3, struct v2_frame_header *fh, const char *buf)
{
	int encoding = *buf++;
	int len = fh->size - 1;
	int idx;

	if (encoding > 3)
		return;

	idx = frame_tab_index(fh->id);
	if (idx >= 0) {
		decode_normal(id3, buf, len, encoding, frame_tab[idx].key);
	} else if (!strncmp(fh->id, "TXXX", 4)) {
		decode_txxx(id3, buf, len, encoding);
	}
}

static void unsync(unsigned char *buf, int *lenp)
{
	int len = *lenp;
	int s, d;

	s = d = 0;
	while (s < len - 1) {
		if (buf[s] == 0xff && buf[s + 1] == 0x00) {
			/* 0xff 0x00 -> 0xff */
			buf[d++] = 0xff;
			s += 2;

			if (s < len - 2 && buf[s] == 0x00) {
				/* 0xff 0x00 0x00 -> 0xff 0x00 */
				buf[d++] = 0x00;
				s++;
			}
			continue;
		}
		buf[d++] = buf[s++];
	}
	if (s < len)
		buf[d++] = buf[s++];

	d_print("unsyncronization removed %d bytes\n", s - d);
	*lenp = d;
}

static int v2_read(struct id3tag *id3, int fd, const struct v2_header *header)
{
	char *buf;
	int rc, buf_size;
	int frame_start, i;
	int frame_header_size;

	buf_size = header->size;
	buf = xnew(char, buf_size);
	rc = read_all(fd, buf, buf_size);
	if (rc) {
		free(buf);
		return rc;
	}

	frame_start = 0;
	if (header->flags & V2_HEADER_EXTENDED) {
		struct v2_extended_header ext;

		if (!v2_extended_header_parse(&ext, buf) || ext.size > buf_size) {
			id3_debug("extended header corrupted\n");
			free(buf);
			return -2;
		}
		frame_start = ext.size;
		/* should check if update flag is set */
	}

	if (header->flags & V2_HEADER_UNSYNC) {
		int len = buf_size - frame_start;

		unsync((unsigned char *)(buf + frame_start), &len);
		buf_size = len + frame_start;
	}

	frame_header_size = 10;
	if (header->ver_major == 2)
		frame_header_size = 6;

	i = frame_start;
	while (i < buf_size - frame_header_size) {
		struct v2_frame_header fh;
		int len;

		if (header->ver_major == 2) {
			if (!v2_2_0_frame_header_parse(&fh, buf + i))
				break;
		} else if (header->ver_major == 3) {
			if (!v2_3_0_frame_header_parse(&fh, buf + i))
				break;
		} else {
			/* assume v2.4 */
			if (!v2_4_0_frame_header_parse(&fh, buf + i))
				break;
		}

		i += frame_header_size;
		if (fh.size > buf_size - i) {
			id3_debug("frame too big\n");
			break;
		}

		len = fh.size;
		if (fh.flags & V2_FRAME_UNSYNC) {
			int tmp = len;

			unsync((unsigned char *)(buf + i), &tmp);
			fh.size = tmp;
		}
		v2_add_frame(id3, &fh, buf + i);
		i += len;
	}

	free(buf);
	return 0;
}

int id3_tag_size(const char *buf, int buf_size)
{
	struct v2_header header;

	if (buf_size < 10)
		return 0;
	if (v2_header_parse(&header, buf)) {
		if (header.flags & V2_HEADER_FOOTER) {
			/* header + data + footer */
			id3_debug("v2.%d.%d with footer\n", header.ver_major, header.ver_minor);
			return 10 + header.size + 10;
		}
		/* header */
		id3_debug("v2.%d.%d\n", header.ver_major, header.ver_minor);
		return 10 + header.size;
	}
	if (buf_size >= 3 && is_v1(buf)) {
		id3_debug("v1\n");
		return 128;
	}
	return 0;
}

void id3_free(struct id3tag *id3)
{
	int i;

	for (i = 0; i < NUM_ID3_KEYS; i++)
		free(id3->v2[i]);
}

int id3_read_tags(struct id3tag *id3, int fd, unsigned int flags)
{
	off_t off;
	int rc;

	if (flags & ID3_V2) {
		struct v2_header header;
		char buf[138];

		rc = read_all(fd, buf, 10);
		if (rc)
			goto rc_error;
		if (v2_header_parse(&header, buf)) {
			rc = v2_read(id3, fd, &header);
			if (rc)
				goto rc_error;
			/* get v1 if needed */
		} else {
			/* get v2 from end and optionally v1 */

			off = lseek(fd, -138, SEEK_END);
			if (off == -1)
				goto error;
			rc = read_all(fd, buf, 138);
			if (rc)
				goto rc_error;

			if (is_v1(buf + 10)) {
				if (flags & ID3_V1) {
					memcpy(id3->v1, buf + 10, 128);
					id3->has_v1 = 1;
				}
				if (v2_footer_parse(&header, buf)) {
					/* footer at end of file - 128 */
					off = lseek(fd, -(header.size + 138), SEEK_END);
					if (off == -1)
						goto error;
					rc = v2_read(id3, fd, &header);
					if (rc)
						goto rc_error;
				}
			} else if (v2_footer_parse(&header, buf + 128)) {
				/* footer at end of file */
				off = lseek(fd, -(header.size + 10), SEEK_END);
				if (off == -1)
					goto error;
				rc = v2_read(id3, fd, &header);
				if (rc)
					goto rc_error;
			}
			return 0;
		}
	}
	if (flags & ID3_V1) {
		off = lseek(fd, -128, SEEK_END);
		if (off == -1)
			goto error;
		rc = read_all(fd, id3->v1, 128);
		if (rc)
			goto rc_error;
		id3->has_v1 = is_v1(id3->v1);
	}
	return 0;
error:
	rc = -1;
rc_error:
	return rc;
}

static char *v1_get_str(const char *buf, int len)
{
	char in[32];
	char *out;
	int i;

	for (i = len - 1; i >= 0; i--) {
		if (buf[i] != 0 && buf[i] != ' ')
			break;
	}
	if (i == -1)
		return NULL;
	memcpy(in, buf, i + 1);
	in[i + 1] = 0;
	if (u_is_valid(in))
		return xstrdup(in);
	if (utf8_encode(in, id3_default_charset, &out))
		return NULL;
	return out;
}

char *id3_get_comment(struct id3tag *id3, enum id3_key key)
{
	if (id3->has_v2) {
		if (id3->v2[key])
			return xstrdup(id3->v2[key]);
	}
	if (id3->has_v1) {
		switch (key) {
		case ID3_ARTIST:
			return v1_get_str(id3->v1 + 33, 30);
		case ID3_ALBUM:
			return v1_get_str(id3->v1 + 63, 30);
		case ID3_TITLE:
			return v1_get_str(id3->v1 + 3, 30);
		case ID3_DATE:
			return v1_get_str(id3->v1 + 93, 4);
		case ID3_GENRE:
			{
				unsigned char idx = id3->v1[127];

				if (idx >= NR_GENRES)
					return NULL;
				return xstrdup(genres[idx]);
			}
		case ID3_TRACK:
			{
				char *t;

				if (id3->v1[125] != 0)
					return NULL;
				t = xnew(char, 4);
				snprintf(t, 4, "%d", ((unsigned char *)id3->v1)[126]);
				return t;
			}
		default:
			return NULL;
		}
	}
	return NULL;
}
