#ifndef  DBUS_API_INC
#define  DBUS_API_INC

#include <glib.h>

#include "dbus-server.h"

gboolean get_int(DBusCmus *obj, int *out, GError **err);
gboolean dbus_cmus_pause(DBusCmus *obj, GError **err);

#endif   /* ----- #ifndef DBUS_API_INC  ----- */
