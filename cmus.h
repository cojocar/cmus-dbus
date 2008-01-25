#ifndef _CMUS_H
#define _CMUS_H

#include "track_info.h"
#include "locking.h"

/*
 * these types are only used to determine what jobs we should cancel.
 * for example ":load" cancels jobs for the current view before loading
 * new playlist.
 */

#define JOB_TYPE_LIB	1
#define JOB_TYPE_PL	2
#define JOB_TYPE_QUEUE	3

enum file_type {
	/* not found, device file... */
	FILE_TYPE_INVALID,

	FILE_TYPE_URL,
	FILE_TYPE_PL,
	FILE_TYPE_DIR,
	FILE_TYPE_FILE
};

typedef int (*track_info_cb)(void *data, struct track_info *ti);

/* lib_for_each, pl_for_each */
typedef int (*for_each_ti_cb)(track_info_cb cb, void *data);

/* lib_for_each_sel, pl_for_each_sel, play_queue_for_each_sel */
typedef int (*for_each_sel_ti_cb)(track_info_cb cb, void *data, int reverse);

/* lib_add_track, pl_add_track, play_queue_append, play_queue_prepend */
typedef void (*add_ti_cb)(struct track_info *);

extern pthread_mutex_t track_db_mutex;
extern struct track_db *track_db;

#define track_db_lock() cmus_mutex_lock(&track_db_mutex)
#define track_db_unlock() cmus_mutex_unlock(&track_db_mutex)

int cmus_init(void);
void cmus_exit(void);
void cmus_play_file(const char *filename);

/* detect file type, returns absolute path or url in @ret */
enum file_type cmus_detect_ft(const char *name, char **ret);

/* add to library, playlist or queue view
 *
 * @add   callback that does the actual adding
 * @name  playlist, directory, file, URL
 * @ft    detected FILE_TYPE_*
 * @jt    JOB_TYPE_{LIB,PL,QUEUE}
 *
 * returns immediately, actual work is done in the worker thread.
 */
void cmus_add(add_ti_cb, const char *name, enum file_type ft, int jt);

int cmus_save(for_each_ti_cb for_each_ti, const char *filename);

void cmus_update_lib(void);
void cmus_update_tis(struct track_info **tis, int nr);

int cmus_is_playlist(const char *filename);
int cmus_is_playable(const char *filename);
int cmus_is_supported(const char *filename);

int cmus_playlist_for_each(const char *buf, int size, int reverse,
		int (*cb)(void *data, const char *line),
		void *data);

void cmus_next(void);
void cmus_prev(void);

#endif
