#include <stdlib.h>
#include <dbus/dbus-glib-bindings.h>
#include <glib-object.h>
#include <glib/gprintf.h>

#include "dbus-server.h"
#include "dbus-api.h"
#include "command_mode.h"
#include "output.h"

gboolean
dbus_cmus_cmd(DBusCmus *obj, char *cmd, int *ret, GError **err)
{
	*ret = run_command(cmd);
	return TRUE;
}
