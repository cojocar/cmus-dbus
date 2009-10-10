#Last.fm scrobbler for cmus-dbus
#it uses http://code.google.com/p/pyscrobbler/
#Cojocar Lucian <cojocar gmail com>
#GPL
import dbus
import gobject
import time
from audioscrobbler import AudioScrobblerPost
from dbus.mainloop.glib import DBusGMainLoop

#see dbus-server.h
DBUS_SEEK			=0
DBUS_NEXT			=1
DBUS_PREV			=2
DBUS_STOP			=3
DBUS_SORTED_ENTER	=4
DBUS_PL_ENTER		=5
DBUS_TREE_ENTER		=6
DBUS_AUTONEXT		=7

def NowPlayingCallback(op, artist, track, album, track_number, secs, pos):
	if op == DBUS_AUTONEXT:
		track_p = dict(artist_name=artist,
			song_title=track,
			length=int(secs),
			date_played=time.strftime("%Y-%m-%d %H:%M:%S", time.gmtime()),
			album=album,
			mbid="")
		post = AudioScrobblerPost(username='dbus-cmus', password='dbus-cmus')
		post(**track_p)

DBusGMainLoop(set_as_default=True)
bus = dbus.SessionBus()
obj = bus.get_object("media.player.cmus", "/media/player/cmus")

#Command Interface
#print obj.dbus_cmus_cmd('quit')

print bus.add_signal_receiver(NowPlayingCallback,
		signal_name='NowPlaying', 
		dbus_interface=None, 
		path=None,
		bus_name=None
		)

loop = gobject.MainLoop()
loop.run()

