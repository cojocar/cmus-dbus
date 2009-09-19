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

#include "id3.h"

G_DEFINE_TYPE(DBusCmus, cmus, G_TYPE_OBJECT);

static guint sig_num;

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
		g_cclosure_user_marshal_VOID__STRING_STRING_STRING_INT_INT_STRING,
		G_TYPE_NONE,
		6,
		G_TYPE_STRING,
		G_TYPE_STRING,
		G_TYPE_STRING,
		G_TYPE_INT,
		G_TYPE_INT,
		G_TYPE_STRING);
	dbus_g_object_type_install_info(
			CMUS_TYPE, &dbus_glib_cmus_object_info);
}

static GObject *obj;
static gpointer
cmus_dbus_thread(gpointer data)
{
	GMainLoop *loop;
	GError *error = NULL;
	DBusGConnection *connection;
	DBusGProxy *driver_proxy;
	guint32 request_name_ret;

	g_type_init();
	loop = g_main_loop_new(NULL, FALSE);
///*
//*/
	printf("aici\n");

	connection = dbus_g_bus_get (DBUS_BUS_SESSION,
	//connection = dbus_g_bus_get (DBUS_BUS_STARTER,
			&error);
	if (connection == NULL)	{
		g_printerr ("Failed to open connection to bus: %s\n",
				error->message);
		g_error_free (error);
		exit (1);
	}
	obj = g_object_new(CMUS_TYPE, NULL);

	/*
	dbus_g_object_type_install_info(
			CMUS_TYPE, &dbus_glib_cmus_object_info);
	*/
	dbus_g_connection_register_g_object (connection,
			"/media/player/cmus", obj);

	driver_proxy = dbus_g_proxy_new_for_name (connection,
			DBUS_SERVICE_DBUS,
			DBUS_PATH_DBUS,
			DBUS_INTERFACE_DBUS);

	///*
	if (!org_freedesktop_DBus_request_name (driver_proxy, CMUS_SERVICE_NAME,
				0, &request_name_ret, &error)) {
					g_printf("EEE\n");
				}

		//die ("Failed to get name", error);

	if (request_name_ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
		g_error ("Got result code %u from requesting name", request_name_ret);
		exit (1);
	}
//	dbus_connection_setup_with_g_main(connection, NULL);
	//*/
	
	g_main_loop_run(loop);
	g_printf("gata_dbus\n");
	return NULL;
}

void
cmus_dbus_start(void)
{
	GThread *t;
	GError *err;
	err = NULL;
	/*
	if (g_thread_supported()) {
//		dbus_threads_init();
		dbus_g_thread_init();
	}*/
	g_thread_init(NULL);
	t = g_thread_create(cmus_dbus_thread, NULL, FALSE, &err);
}

void
cmus_dbus_stop(void)
{
}

#define STR(str)	((str == NULL) ? "" : (str))

static char *
get_val(const char *key, char *argv[])
{
	while (*argv) {
		if (!strcmp(*argv, key)) {
			return *(argv + 1);
		}
		argv += 2;
	}
	return NULL;
}

void
cmus_dbus_signal(char *argv[])
{
	++ argv;
	g_signal_emit(obj,
			sig_num,
			0,
			STR(get_val("artist", argv)),
			STR(get_val("title", argv)),
			STR(get_val("album", argv)),
			11,
			0,
			"");
}
