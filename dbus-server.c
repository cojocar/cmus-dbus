/*
 * Cojocar Lucian cojocar-gmail.com
 * 
 * cmus dbus implementation
 * Fri Sep 18 22:07:34 EEST 2009
 * GPL
 */

#include <stdlib.h>
#include <dbus/dbus-glib-bindings.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <glib-object.h>

#include "config/dbus.h"
#include "dbus-server.h"
#include "dbus-api.h"
#include "dbus-bindings.h"
#include "dbus-marshal.h"
#include "player.h"

G_DEFINE_TYPE(DBusCmus, cmus, G_TYPE_OBJECT);

static guint sig_num;
static GMainLoop *dbus_loop;
static GThread *dbus_thread;
static GObject *obj;

static void
cmus_init(DBusCmus *cmus)
{
}

static void
cmus_class_init(DBusCmusClass *cmus_class)
{
	sig_num = g_signal_new("now_playing", 
		G_OBJECT_CLASS_TYPE(cmus_class),
		G_SIGNAL_RUN_LAST,
		0,
		NULL,
		NULL,
		g_cclosure_user_marshal_VOID__INT_STRING_STRING_STRING_INT_INT_INT,
		G_TYPE_NONE,
		7,
		G_TYPE_INT,
		G_TYPE_STRING,
		G_TYPE_STRING,
		G_TYPE_STRING,
		G_TYPE_INT,
		G_TYPE_INT,
		G_TYPE_INT);
	dbus_g_object_type_install_info(
			CMUS_TYPE, &dbus_glib_cmus_object_info);
}

static gpointer
cmus_dbus_thread(gpointer data)
{
	GError *error = NULL;
	DBusGConnection *connection;
	DBusGProxy *driver_proxy;
	guint32 request_name_ret;

	g_type_init();
	dbus_loop = g_main_loop_new(NULL, FALSE);

	connection = dbus_g_bus_get(DBUS_BUS_SESSION, &error);
	if (connection == NULL)	{
		g_printerr("Failed to open connection to bus: %s\n",
				error->message);
		g_error_free(error);
		return NULL;
	}
	obj = g_object_new(CMUS_TYPE, NULL);

	dbus_g_connection_register_g_object(connection,
			"/media/player/cmus", obj);

	driver_proxy = dbus_g_proxy_new_for_name(connection,
			DBUS_SERVICE_DBUS,
			DBUS_PATH_DBUS,
			DBUS_INTERFACE_DBUS);

	if (!org_freedesktop_DBus_request_name (driver_proxy, CMUS_SERVICE_NAME,
				0, &request_name_ret, &error)) {
		g_printerr("Error %s\n", error->message);
		g_error_free(error);
		g_object_unref(obj);
		return NULL;
	}

	if (request_name_ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
		g_error ("Got result code %u from requesting name", request_name_ret);
		g_object_unref(obj);
		return NULL;
	}
	
	g_main_loop_run(dbus_loop);

	g_object_unref(obj);
	g_object_unref(driver_proxy);
	return NULL;
}

void
cmus_dbus_start(void)
{
	GError *err = NULL;
	g_thread_init(NULL);
	dbus_thread = g_thread_create(cmus_dbus_thread, NULL, TRUE, &err);
}

void
cmus_dbus_stop(void)
{
	g_main_loop_quit(dbus_loop);
	g_thread_join(dbus_thread);
}

/*
 * Inspired by 
 * http://www.hci-matters.com/blog/2008/05/06/c-music-player-audioscrobblerlastfm-patch/
 */
void
cmus_dbus_hook(enum dbus_actions a)
{
	const char *artist, *album, *track_name;
	int secs, pos, track_number;
	track_number = 0;

	player_info_lock();
	if (player_info.ti == NULL) {
		player_info_unlock();
		return;
	}
	album = keyvals_get_val(player_info.ti->comments, "album");
	if (!album)
		album = "";
	artist = keyvals_get_val(player_info.ti->comments, "artist");
	if (!artist)
		artist = "";
	track_name = keyvals_get_val(player_info.ti->comments, "title");
	if (!track_name)
		track_name = "";

	secs = player_info.ti->duration;
	pos = player_info.pos;

	/* Sending the Now Playing Signal */
	g_signal_emit(obj,
		sig_num, 0,
		a,
		artist,
		track_name,
		album,
		track_number,
		secs,
		pos);
	player_info_unlock();
}
