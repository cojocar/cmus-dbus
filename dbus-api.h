#ifndef  DBUS_API_INC
#define  DBUS_API_INC

#include <glib.h>

#include "dbus-server.h"

gboolean dbus_cmus_cmd(DBusCmus *obj, char *cmd, int *ret, GError **err);

#endif   /* ----- #ifndef DBUS_API_INC  ----- */
