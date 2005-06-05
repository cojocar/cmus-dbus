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

#include <comment.h>
#include <xmalloc.h>
#include <utils.h>

#include <string.h>

struct comment *comments_dup(const struct comment *comments)
{
	struct comment *c;
	int i;

	for (i = 0; comments[i].key; i++)
		; /* nothing */
	c = xnew(struct comment, i + 1);
	for (i = 0; comments[i].key; i++) {
		c[i].key = xstrdup(comments[i].key);
		c[i].val = xstrdup(comments[i].val);
	}
	c[i].key = NULL;
	c[i].val = NULL;
	return c;
}

void comments_free(struct comment *comments)
{
	int i;

	for (i = 0; comments[i].key; i++) {
		free(comments[i].key);
		free(comments[i].val);
	}
	free(comments);
}

const char *comments_get_val(const struct comment *comments, const char *key)
{
	int i;

	for (i = 0; comments[i].key; i++) {
		if (strcasecmp(comments[i].key, key) == 0)
			return comments[i].val;
	}
	return NULL;
}

int comments_get_int(const struct comment *comments, const char *key)
{
	const char *val;
	long int ival;

	val = comments_get_val(comments, key);
	if (val == NULL)
		return -1;
	if (str_to_int(val, &ival) == -1)
		return -1;
	return ival;
}
