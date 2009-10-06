#ifndef  DBUS_SERVER_INC
#define  DBUS_SERVER_INC

#include "config/dbus.h"

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

#ifdef	CONFIG_DBUS

#define CMUS_TYPE	(cmus_get_type())
#define CMUS_SERVICE_NAME	"media.player.cmus"

#include <glib-object.h>

typedef struct {
	GObject parent;
} DBusCmus;

typedef struct {
	GObjectClass parent;
} DBusCmusClass;


GType cmus_get_type(void);
void cmus_dbus_hook(enum dbus_actions);
void cmus_dbus_start(void);
void cmus_dbus_stop(void);
#else	/* !CONFIG_DBUS */

#define cmus_get_type()
#define cmus_dbus_hook(a)
#define cmus_dbus_start()
#define cmus_dbus_stop()

#endif  /* !CONFIG_DBUS */


#endif   /* ----- #ifndef DBUS_SERVER_INC  ----- */
