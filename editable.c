/*
 * Copyright 2006 Timo Hirvonen
 */

#include "editable.h"
#include "search.h"
#include "track.h"
#include "track_info.h"
#include "expr.h"
#include "filters.h"
#include "locking.h"
#include "mergesort.h"
#include "xmalloc.h"

pthread_mutex_t editable_mutex = CMUS_MUTEX_INITIALIZER;

static const struct searchable_ops simple_search_ops = {
	.get_prev = simple_track_get_prev,
	.get_next = simple_track_get_next,
	.get_current = simple_track_search_get_current,
	.matches = simple_track_search_matches
};

static struct simple_track *get_selected(struct editable *e)
{
	struct iter sel;

	if (window_get_sel(e->win, &sel))
		return iter_to_simple_track(&sel);
	return NULL;
}

void editable_init(struct editable *e, void (*free_track)(struct list_head *item))
{
	struct iter iter;

	list_init(&e->head);
	e->nr_tracks = 0;
	e->nr_marked = 0;
	e->total_time = 0;
	e->sort_keys = xnew(const char *, 1);
	e->sort_keys[0] = NULL;
	e->sort_str[0] = 0;
	e->free_track = free_track;

	e->win = window_new(simple_track_get_prev, simple_track_get_next);
	window_set_contents(e->win, &e->head);

	iter.data0 = &e->head;
	iter.data1 = NULL;
	iter.data2 = NULL;
	e->searchable = searchable_new(e->win, &iter, &simple_search_ops);
}

void editable_add(struct editable *e, struct simple_track *track)
{
	sorted_list_add_track(&e->head, track, e->sort_keys);
	e->nr_tracks++;
	if (track->info->duration != -1)
		e->total_time += track->info->duration;
	window_changed(e->win);
}

void editable_remove_track(struct editable *e, struct simple_track *track)
{
	struct track_info *ti = track->info;
	struct iter iter;

	editable_track_to_iter(e, track, &iter);
	window_row_vanishes(e->win, &iter);

	e->nr_tracks--;
	e->nr_marked -= track->marked;
	if (ti->duration != -1)
		e->total_time -= ti->duration;

	list_del(&track->node);

	e->free_track(&track->node);
}

void editable_remove_sel(struct editable *e)
{
	struct simple_track *t;

	if (e->nr_marked) {
		/* treat marked tracks as selected */
		struct list_head *next, *item = e->head.next;

		while (item != &e->head) {
			next = item->next;
			t = to_simple_track(item);
			if (t->marked)
				editable_remove_track(e, t);
			item = next;
		}
	} else {
		t = get_selected(e);
		if (t)
			editable_remove_track(e, t);
	}
}

static const char **sort_keys;

static int list_cmp(const struct list_head *a_head, const struct list_head *b_head)
{
	const struct simple_track *a = to_simple_track(a_head);
	const struct simple_track *b = to_simple_track(b_head);

	return track_info_cmp(a->info, b->info, sort_keys);
}

void editable_sort(struct editable *e)
{
	sort_keys = e->sort_keys;
	list_mergesort(&e->head, list_cmp);
	window_changed(e->win);
	window_goto_top(e->win);
}

static void keys_to_str(const char **keys, char *buf)
{
	int i, pos = 0;

	for (i = 0; keys[i]; i++) {
		const char *key = keys[i];
		int len = strlen(key);

		if (sizeof(buf) - pos - len - 2 < 0)
			break;

		memcpy(buf + pos, key, len);
		pos += len;
		buf[pos++] = ' ';
	}
	if (pos > 0)
		pos--;
	buf[pos] = 0;
}

void editable_set_sort_keys(struct editable *e, const char **keys)
{
	free(e->sort_keys);
	e->sort_keys = keys;
	editable_sort(e);
	keys_to_str(keys, e->sort_str);
}

void editable_toggle_mark(struct editable *e)
{
	struct simple_track *t;

	t = get_selected(e);
	if (t) {
		e->nr_marked -= t->marked;
		t->marked ^= 1;
		e->nr_marked += t->marked;
		e->win->changed = 1;
		window_down(e->win, 1);
	}
}

static void move_item(struct editable *e, struct list_head *head, struct list_head *item)
{
	struct simple_track *t = to_simple_track(item);
	struct iter iter;

	editable_track_to_iter(e, t, &iter);
	window_row_vanishes(e->win, &iter);

	list_del(item);
	list_add(item, head);
}

static void move_sel(struct editable *e, struct list_head *after)
{
	struct simple_track *t;
	struct list_head *item, *next;
	struct iter iter;
	LIST_HEAD(tmp_head);

	if (e->nr_marked) {
		/* collect marked */
		item = e->head.next;
		while (item != &e->head) {
			t = to_simple_track(item);
			next = item->next;
			if (t->marked)
				move_item(e, &tmp_head, item);
			item = next;
		}
	} else {
		/* collect the selected track */
		t = get_selected(e);
		move_item(e, &tmp_head, &t->node);
	}

	/* put them back to the list after @after */
	item = tmp_head.next;
	while (item != &tmp_head) {
		next = item->next;
		list_add(item, after);
		item = next;
	}

	/* select top-most of the moved tracks */
	editable_track_to_iter(e, to_simple_track(after->next), &iter);
	window_set_sel(e->win, &iter);
	window_changed(e->win);
}

static struct list_head *find_insert_after_point(struct editable *e, struct list_head *item)
{
	if (e->nr_marked == 0) {
		/* move the selected track down one row */
		return item->next;
	}

	/* move marked after the selected
	 *
	 * if the selected track itself is marked we find the first unmarked
	 * track (or head) before the selected one
	 */
	while (item != &e->head) {
		struct simple_track *t = to_simple_track(item);

		if (!t->marked)
			break;
		item = item->prev;
	}
	return item;
}

static struct list_head *find_insert_before_point(struct editable *e, struct list_head *item)
{
	item = item->prev;
	if (e->nr_marked == 0) {
		/* move the selected track up one row */
		return item->prev;
	}

	/* move marked before the selected
	 *
	 * if the selected track itself is marked we find the first unmarked
	 * track (or head) before the selected one
	 */
	while (item != &e->head) {
		struct simple_track *t = to_simple_track(item);

		if (!t->marked)
			break;
		item = item->prev;
	}
	return item;
}

void editable_move_after(struct editable *e)
{
	struct simple_track *sel;

	if (e->nr_tracks <= 1 || e->sort_keys[0])
		return;

	sel = get_selected(e);
	if (sel)
		move_sel(e, find_insert_after_point(e, &sel->node));
}

void editable_move_before(struct editable *e)
{
	struct simple_track *sel;

	if (e->nr_tracks <= 1 || e->sort_keys[0])
		return;

	sel = get_selected(e);
	if (sel)
		move_sel(e, find_insert_before_point(e, &sel->node));
}

void editable_clear(struct editable *e)
{
	struct list_head *item, *next;

	item = e->head.next;
	while (item != &e->head) {
		next = item->next;
		editable_remove_track(e, to_simple_track(item));
		item = next;
	}
}

void editable_mark(struct editable *e, const char *filter)
{
	struct expr *expr = NULL;
	struct simple_track *t;

	if (filter) {
		expr = parse_filter(filter);
		if (expr == NULL)
			return;
	}

	list_for_each_entry(t, &e->head, node) {
		e->nr_marked -= t->marked;
		t->marked = 0;
		if (expr == NULL || expr_eval(expr, t->info)) {
			t->marked = 1;
			e->nr_marked++;
		}
	}
	e->win->changed = 1;
}

void editable_unmark(struct editable *e)
{
	struct simple_track *t;

	list_for_each_entry(t, &e->head, node) {
		e->nr_marked -= t->marked;
		t->marked = 0;
	}
	e->win->changed = 1;
}

void editable_invert_marks(struct editable *e)
{
	struct simple_track *t;

	list_for_each_entry(t, &e->head, node) {
		e->nr_marked -= t->marked;
		t->marked ^= 1;
		e->nr_marked += t->marked;
	}
	e->win->changed = 1;
}

int __editable_for_each_sel(struct editable *e, int (*cb)(void *data, struct track_info *ti),
		void *data, int reverse)
{
	int rc = 0;

	if (e->nr_marked) {
		/* treat marked tracks as selected */
		rc = simple_list_for_each_marked(&e->head, cb, data, reverse);
	} else {
		struct simple_track *t = get_selected(e);

		if (t)
			rc = cb(data, t->info);
	}
	return rc;
}

int editable_for_each_sel(struct editable *e, int (*cb)(void *data, struct track_info *ti),
		void *data, int reverse)
{
	int rc;

	rc = __editable_for_each_sel(e, cb, data, reverse);
	if (e->nr_marked == 0)
		window_down(e->win, 1);
	return rc;
}
