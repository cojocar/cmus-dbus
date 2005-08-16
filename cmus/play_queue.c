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

#include <play_queue.h>
#include <ui_curses.h>
#include <window.h>
#include <misc.h>
#include <xmalloc.h>
#include <debug.h>

#include <curses.h>

struct window *play_queue_win;
struct searchable *play_queue_searchable;
int play_queue_changed = 0;
pthread_mutex_t play_queue_mutex = CMUS_MUTEX_INITIALIZER;

static LIST_HEAD(play_queue_head);

static inline void play_queue_entry_to_iter(struct play_queue_entry *e, struct iter *iter)
{
	iter->data0 = &play_queue_head;
	iter->data1 = e;
	iter->data2 = NULL;
}

static int play_queue_get_prev(struct iter *iter)
{
	struct list_head *head = iter->data0;
	struct play_queue_entry *e = iter->data1;

	BUG_ON(iter->data0 == NULL);
	BUG_ON(iter->data2);
	if (e == NULL) {
		/* head, get last */
		if (head->prev == head) {
			/* empty, iter points to the head already */
			return 0;
		}
		iter->data1 = container_of(head->prev, struct play_queue_entry, node);
		return 1;
	}
	if (e->node.prev == head) {
		iter->data1 = NULL;
		return 0;
	}
	iter->data1 = container_of(e->node.prev, struct play_queue_entry, node);
	return 1;
}

static int play_queue_get_next(struct iter *iter)
{
	struct list_head *head = iter->data0;
	struct play_queue_entry *e = iter->data1;

	BUG_ON(iter->data0 == NULL);
	BUG_ON(iter->data2);
	if (e == NULL) {
		/* head, get first */
		if (head->next == head) {
			/* empty, iter points to the head already */
			return 0;
		}
		iter->data1 = container_of(head->next, struct play_queue_entry, node);
		return 1;
	}
	if (e->node.next == head) {
		iter->data1 = NULL;
		return 0;
	}
	iter->data1 = container_of(e->node.next, struct play_queue_entry, node);
	return 1;
}

static void play_queue_search_lock(void *data)
{
	play_queue_lock();
}

static void play_queue_search_unlock(void *data)
{
	play_queue_unlock();
}

static int play_queue_search_get_current(void *data, struct iter *iter)
{
	return window_get_sel(play_queue_win, iter);
}

static int play_queue_search_matches(void *data, struct iter *iter, const char *text)
{
	struct play_queue_entry *e;

	e = iter_to_play_queue_entry(iter);
	if (!track_info_matches(e->track_info, text))
		return 0;
	window_set_sel(play_queue_win, iter);
	play_queue_changed = 1;
	return 1;
}

static const struct searchable_ops play_queue_search_ops = {
	.lock = play_queue_search_lock,
	.unlock = play_queue_search_unlock,
	.get_prev = play_queue_get_prev,
	.get_next = play_queue_get_next,
	.get_current = play_queue_search_get_current,
	.matches = play_queue_search_matches
};

void play_queue_init(void)
{
	struct iter iter;

	play_queue_win = window_new(play_queue_get_prev, play_queue_get_next);
	window_set_contents(play_queue_win, &play_queue_head);
	window_changed(play_queue_win);

	iter.data0 = &play_queue_head;
	iter.data1 = NULL;
	iter.data2 = NULL;
	play_queue_searchable = searchable_new(NULL, &iter, &play_queue_search_ops);
}

void play_queue_exit(void)
{
	struct list_head *item;

	searchable_free(play_queue_searchable);
	item = play_queue_head.next;
	while (item != &play_queue_head) {
		struct list_head *next = item->next;
		struct play_queue_entry *e;

		e = list_entry(item, struct play_queue_entry, node);
		track_info_unref(e->track_info);
		free(e);
		item = next;
	}
	list_init(&play_queue_head);
	window_free(play_queue_win);
}

void __play_queue_append(struct track_info *track_info)
{
	struct play_queue_entry *e;

	track_info_ref(track_info);

	e = xnew(struct play_queue_entry, 1);
	e->track_info = track_info;
	list_add_tail(&e->node, &play_queue_head);
	window_changed(play_queue_win);
	play_queue_changed = 1;
}

void __play_queue_prepend(struct track_info *track_info)
{
	struct play_queue_entry *e;

	track_info_ref(track_info);

	e = xnew(struct play_queue_entry, 1);
	e->track_info = track_info;
	list_add(&e->node, &play_queue_head);
	window_changed(play_queue_win);
	play_queue_changed = 1;
}

void play_queue_append(struct track_info *track_info)
{
	play_queue_lock();
	__play_queue_append(track_info);
	play_queue_unlock();
}

void play_queue_prepend(struct track_info *track_info)
{
	play_queue_lock();
	__play_queue_prepend(track_info);
	play_queue_unlock();
}

struct track_info *play_queue_remove(void)
{
	struct list_head *item;
	struct play_queue_entry *e;
	struct track_info *info;
	struct iter iter;

	play_queue_lock();
	item = play_queue_head.next;
	if (item == &play_queue_head) {
		play_queue_unlock();
		return NULL;
	}

	e = container_of(item, struct play_queue_entry, node);
	play_queue_entry_to_iter(e, &iter);
	window_row_vanishes(play_queue_win, &iter);
	list_del(item);

	info = e->track_info;
	free(e);

	/* can't update directly because this might have been called from
	 * the player thread */
	play_queue_changed = 1;

	play_queue_unlock();
	return info;
}

static void play_queue_delete(void)
{
	struct iter iter;

	if (window_get_sel(play_queue_win, &iter)) {
		struct play_queue_entry *e;

		e = iter_to_play_queue_entry(&iter);
		window_row_vanishes(play_queue_win, &iter);
		list_del(&e->node);

		track_info_unref(e->track_info);
		free(e);
	}
}

int play_queue_ch(uchar ch)
{
	int rc = 1;

	play_queue_lock();
	switch (ch) {
	case 'D':
		play_queue_delete();
		break;
	case 'j':
		window_move(play_queue_win, 1);
		break;
	case 'k':
		window_move(play_queue_win, -1);
		break;
	case 6: /* ^f */
		window_page_down(play_queue_win);
		break;
	case 2: /* ^b */
		window_page_up(play_queue_win);
		break;
	case 'g':
		window_goto_top(play_queue_win);
		break;
	case 'G':
		window_goto_bottom(play_queue_win);
		break;
	default:
		rc = 0;
		break;
	}
	play_queue_unlock();
	return rc;
}

int play_queue_key(int key)
{
	int rc = 1;

	play_queue_lock();
	switch (key) {
	case KEY_DC:
		play_queue_delete();
		break;
	case KEY_UP:
		window_move(play_queue_win, -1);
		break;
	case KEY_DOWN:
		window_move(play_queue_win, 1);
		break;
	case KEY_PPAGE:
		window_page_up(play_queue_win);
		break;
	case KEY_NPAGE:
		window_page_down(play_queue_win);
		break;
	case KEY_HOME:
		window_goto_top(play_queue_win);
		break;
	case KEY_END:
		window_goto_bottom(play_queue_win);
		break;
	default:
		rc = 0;
		break;
	}
	play_queue_unlock();
	return rc;
}
