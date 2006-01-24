/*
 * Copyright 2005 Timo Hirvonen
 */

#include <filters.h>
#include <expr.h>
#include <window.h>
#include <search.h>
#include <uchar.h>
#include <lib.h>
#include <misc.h>
#include <file.h>
#include <ui_curses.h>
#include <xmalloc.h>
#include <debug.h>

#include <ctype.h>

struct window *filters_win;
struct searchable *filters_searchable;
LIST_HEAD(filters_head);

static const char *recursive_filter;

static inline void filter_entry_to_iter(struct filter_entry *e, struct iter *iter)
{
	iter->data0 = &filters_head;
	iter->data1 = e;
	iter->data2 = NULL;
}

static GENERIC_ITER_PREV(filters_get_prev, struct filter_entry, node)
static GENERIC_ITER_NEXT(filters_get_next, struct filter_entry, node)

static void dummy(void *data)
{
}

static int filters_search_get_current(void *data, struct iter *iter)
{
	return window_get_sel(filters_win, iter);
}

static int filters_search_matches(void *data, struct iter *iter, const char *text)
{
	char **words = get_words(text);
	int matched = 0;

	if (words[0] != NULL) {
		struct filter_entry *e;
		int i;

		e = iter_to_filter_entry(iter);
		for (i = 0; ; i++) {
			if (words[i] == NULL) {
				window_set_sel(filters_win, iter);
				matched = 1;
				break;
			}
			if (u_strcasestr(e->name, words[i]) == NULL)
				break;
		}
	}
	free_str_array(words);
	return matched;
}

static const struct searchable_ops filters_search_ops = {
	.lock = dummy,
	.unlock = dummy,
	.get_prev = filters_get_prev,
	.get_next = filters_get_next,
	.get_current = filters_search_get_current,
	.matches = filters_search_matches
};

static void free_filter(struct filter_entry *e)
{
	free(e->name);
	free(e->filter);
	free(e);
}

static struct filter_entry *find_filter(const char *name)
{
	struct filter_entry *e;

	list_for_each_entry(e, &filters_head, node) {
		if (strcmp(e->name, name) == 0)
			return e;
	}
	return NULL;
}

static const char *get_filter(const char *name)
{
	struct filter_entry *e = find_filter(name);

	if (e) {
		if (e->visited) {
			recursive_filter = e->name;
			return NULL;
		}
		e->visited = 1;
		return e->filter;
	}
	return NULL;
}

void filters_activate(void)
{
	struct filter_entry *f;
	struct expr *e, *expr = NULL;

	/* mark visited and AND together all selected filters
	 * mark any other filters unvisited */
	list_for_each_entry(f, &filters_head, node) {
		f->visited = 0;
		if (!f->selected)
			continue;

		f->visited = 1;
		e = expr_parse(f->filter);
		if (e == NULL) {
			error_msg("error parsing filter %s: %s", f->name, expr_error());
			if (expr)
				expr_free(expr);
			return;
		}

		if (expr == NULL) {
			expr = e;
		} else {
			struct expr *and = xnew(struct expr, 1);

			and->type = EXPR_AND;
			and->key = NULL;
			and->left = expr;
			and->right = e;
			expr->parent = and;
			e->parent = and;
			expr = and;
		}
	}

	recursive_filter = NULL;
	if (expr && expr_check_leaves(&expr, get_filter)) {
		if (recursive_filter) {
			error_msg("recursion detected in filter %s", recursive_filter);
		} else {
			error_msg("error parsing filter: %s", expr_error());
		}
		expr_free(expr);
		return;
	}

	/* update active flag */
	list_for_each_entry(f, &filters_head, node) {
		f->active = 0;
		if (f->selected)
			f->active = 1;
	}
	lib_set_filter(expr);
	filters_win->changed = 1;
}

static int for_each_name(const char *str, int (*cb)(const char *name))
{
	char buf[64];
	int s, e, len;

	e = 0;
	do {
		s = e;
		while (str[s] == ' ')
			s++;
		e = s;
		while (str[e] && str[e] != ' ')
			e++;

		len = e - s;
		if (len == 0)
			return 0;
		if (len >= sizeof(buf)) {
			error_msg("filter name too long");
			return -1;
		}

		memcpy(buf, str + s, len);
		buf[len] = 0;

		if (cb(buf))
			return -1;
	} while (1);
}

static int ensure_filter_name(const char *name)
{
	if (find_filter(name) == NULL) {
		error_msg("no such filter %s", name);
		return -1;
	}
	return 0;
}

static int select_filter(const char *name)
{
	struct filter_entry *e = find_filter(name);

	BUG_ON(e == NULL);
	e->selected = 1;
	return 0;
}

void filters_activate_names(const char *str)
{
	struct filter_entry *f;

	/* first validate all filter names */
	if (str && for_each_name(str, ensure_filter_name))
		return;

	/* mark all filters unselected  */
	list_for_each_entry(f, &filters_head, node)
		f->selected = 0;

	/* select the filters */
	if (str)
		for_each_name(str, select_filter);

	/* activate selected */
	filters_activate();
}

void filters_toggle_filter(void)
{
	struct iter iter;

	if (window_get_sel(filters_win, &iter)) {
		struct filter_entry *e;

		e = iter_to_filter_entry(&iter);
		e->selected ^= 1;
		filters_win->changed = 1;
	}
}

void filters_delete_filter(void)
{
	struct iter iter;

	if (window_get_sel(filters_win, &iter)) {
		struct filter_entry *e;

		e = iter_to_filter_entry(&iter);
		if (yes_no_query("Delete filter '%s'? [y/N]", e->name)) {
			window_row_vanishes(filters_win, &iter);
			list_del(&e->node);
			free_filter(e);
		}
	}
}

static int validate_filter_name(const char *name)
{
	int i;

	for (i = 0; name[i]; i++) {
		if (isalnum(name[i]))
			continue;
		if (name[i] == '_' || name[i] == '-')
			continue;
		return 0;
	}
	return i != 0;
}

static void do_filters_set_filter(const char *keyval, int active)
{
	const char *eq = strchr(keyval, '=');
	char *key, *val;
	struct expr *expr;
	struct filter_entry *new;
	struct list_head *item;

	if (eq == NULL) {
		if (ui_initialized)
			error_msg("invalid argument ('key=value' expected)");
		return;
	}
	key = xstrndup(keyval, eq - keyval);
	val = xstrdup(eq + 1);
	if (!validate_filter_name(key)) {
		if (ui_initialized)
			error_msg("invalid filter name (can only contain 'a-zA-Z0-9_-' characters)");
		free(key);
		free(val);
		return;
	}
	expr = expr_parse(val);
	if (expr == NULL) {
		if (ui_initialized)
			error_msg("error parsing filter %s: %s", val, expr_error());
		free(key);
		free(val);
		return;
	}
	expr_free(expr);

	new = xnew(struct filter_entry, 1);
	new->name = key;
	new->filter = val;
	new->active = active;
	new->selected = active;

	/* add or replace filter */
	list_for_each(item, &filters_head) {
		struct filter_entry *e = container_of(item, struct filter_entry, node);
		int res = strcmp(key, e->name);

		if (res < 0)
			break;
		if (res == 0) {
			/* replace */
			struct iter iter;

			if (ui_initialized) {
				filter_entry_to_iter(e, &iter);
				window_row_vanishes(filters_win, &iter);
			}
			item = item->next;
			list_del(&e->node);
			free_filter(e);
			break;
		}
	}
	/* add before item */
	list_add_tail(&new->node, item);
	if (ui_initialized)
		window_changed(filters_win);
}

void filters_init(void)
{
	struct iter iter;

	filters_win = window_new(filters_get_prev, filters_get_next);
	window_set_contents(filters_win, &filters_head);
	window_changed(filters_win);

	iter.data0 = &filters_head;
	iter.data1 = NULL;
	iter.data2 = NULL;
	filters_searchable = searchable_new(NULL, &iter, &filters_search_ops);
}

void filters_exit(void)
{
	searchable_free(filters_searchable);
	window_free(filters_win);
}

void filters_set_filter(const char *keyval)
{
	do_filters_set_filter(keyval, 0);
}

struct expr *parse_filter(const char *val)
{
	struct expr *e = NULL;
	struct filter_entry *f;

	if (val) {
		e = expr_parse(val);
		if (e == NULL) {
			error_msg("error parsing filter %s: %s", val, expr_error());
			return NULL;
		}
	}

	/* mark all unvisited so that we can check recursion */
	list_for_each_entry(f, &filters_head, node)
		f->visited = 0;

	recursive_filter = NULL;
	if (e && expr_check_leaves(&e, get_filter)) {
		if (recursive_filter) {
			error_msg("recursion detected in filter %s", recursive_filter);
		} else {
			error_msg("error parsing filter: %s", expr_error());
		}
		expr_free(e);
		return NULL;
	}
	return e;
}

void filters_set_anonymous(const char *val)
{
	struct filter_entry *f;
	struct expr *e = parse_filter(val);

	if (e == NULL)
		return;

	/* deactive all filters */
	list_for_each_entry(f, &filters_head, node)
		f->active = 0;
	lib_set_filter(e);

	filters_win->changed = 1;
}
