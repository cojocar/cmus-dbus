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

#ifndef _SEARCH_H
#define _SEARCH_H

#include <iter.h>

enum search_direction { SEARCH_FORWARD, SEARCH_BACKWARD };

struct searchable_ops {
	void (*lock)(void *data);
	void (*unlock)(void *data);
	int (*get_prev)(struct iter *iter);
	int (*get_next)(struct iter *iter);
	int (*get_current)(void *data, struct iter *iter);
	int (*matches)(void *data, struct iter *iter, const char *text, int restricted);
};

struct searchable;

extern struct searchable *searchable_new(void *data, const struct iter *head, const struct searchable_ops *ops);
extern void searchable_free(struct searchable *s);

extern int search(struct searchable *s, const char *text, enum search_direction dir, int restricted, int beginning);
extern int search_next(struct searchable *s, const char *text, enum search_direction dir, int restricted);

#endif
