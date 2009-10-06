#ifndef  DBUS_SERVER_INC
#define  DBUS_SERVER_INC

#define CMUS_TYPE	(cmus_get_type())
#define CMUS_SERVICE_NAME	"media.player.cmus"

#include <glib-object.h>

typedef struct {
	GObject parent;
} DBusCmus;

typedef struct {
	GObjectClass parent;
} DBusCmusClass;

enum dbus_actions {
	DBUS_SEEK,
	DBUS_NEXT,
	DBUS_PREV,
	DBUS_STOP,
	DBUS_SORTED_ENTER,
	DBUS_PL_ENTER,
	DBUS_TREE_ENTER,
	DBUS_AUTONEXT,
};

void cmus_dbus_hook(enum dbus_actions);
void cmus_dbus_start(void);
void cmus_dbus_stop(void);
GType cmus_get_type(void);

#endif   /* ----- #ifndef DBUS_SERVER_INC  ----- */
