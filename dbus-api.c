#include <stdlib.h>
#include <dbus/dbus-glib-bindings.h>
#include <glib-object.h>
#include <glib/gprintf.h>

#include "dbus-server.h"
#include "dbus-api.h"

#include "output.h"

gboolean
get_int(DBusCmus *obj, int *out, GError **err)
{
	*out = -2;
	return TRUE;
}

gboolean
dbus_cmus_pause(DBusCmus *obj, GError **err)
{
	player_pause();
	return TRUE;
}
