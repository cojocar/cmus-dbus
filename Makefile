all: main plugins man

include config.mk
include scripts/lib.mk

CFLAGS	+= -I. -g

# programs {{{
cmus-y := \
	browser.o buffer.o cmdline.o cmus.o command_mode.o comment.o \
	db.o debug.o editable.o expr.o filters.o \
	format_print.o glob.o history.o http.o input.o \
	keys.o lib.o load_dir.o locking.o mergesort.o misc.o options.o \
	output.o pcm.o pl.o play_queue.o player.o \
	read_wrapper.o server.o search.o \
	search_mode.o spawn.o tabexp.o tabexp_file.o \
	track.o track_db.o track_info.o tree.o uchar.o ui_curses.o window.o \
	worker.o xstrjoin.o

$(cmus-y): CFLAGS += $(PTHREAD_CFLAGS) $(NCURSES_CFLAGS) $(ICONV_CFLAGS) $(DL_CFLAGS)

cmus: $(cmus-y) file.o path.o prog.o xmalloc.o
	$(call cmd,ld,$(PTHREAD_LIBS) $(NCURSES_LIBS) $(ICONV_LIBS) $(DL_LIBS) -lm)

cmus-remote: main.o file.o path.o prog.o xmalloc.o
	$(call cmd,ld,)
# }}}

# input plugins {{{
flac-objs		:= flac.lo
mad-objs		:= id3.lo mad.lo nomad.lo utf8_encode.lo
modplug-objs		:= modplug.lo
mpc-objs		:= mpc.lo
vorbis-objs		:= vorbis.lo
wav-objs		:= wav.lo

ip-$(CONFIG_FLAC)	+= flac.so
ip-$(CONFIG_MAD)	+= mad.so
ip-$(CONFIG_MODPLUG)	+= modplug.so
ip-$(CONFIG_MPC)	+= mpc.so
ip-$(CONFIG_VORBIS)	+= vorbis.so
ip-$(CONFIG_WAV)	+= wav.so

$(flac-objs):		CFLAGS += $(FLAC_CFLAGS)
$(mad-objs):		CFLAGS += $(MAD_CFLAGS)
$(modplug-objs):	CFLAGS += $(MODPLUG_CFLAGS)
$(mpc-objs):		CFLAGS += $(MPC_CFLAGS)
$(vorbis-objs):		CFLAGS += $(VORBIS_CFLAGS)

flac.so: $(flac-objs)
	$(call cmd,ld_dl,$(FLAC_LIBS))

mad.so: $(mad-objs)
	$(call cmd,ld_dl,$(MAD_LIBS))

modplug.so: $(modplug-objs)
	$(call cmd,ld_dl,$(MODPLUG_LIBS))

mpc.so: $(mpc-objs)
	$(call cmd,ld_dl,$(MPC_LIBS))

vorbis.so: $(vorbis-objs)
	$(call cmd,ld_dl,$(VORBIS_LIBS))

wav.so: $(wav-objs)
	$(call cmd,ld_dl,)
# }}}

# output plugins {{{
alsa-objs		:= alsa.lo mixer_alsa.lo
arts-objs		:= arts.lo
oss-objs		:= oss.lo mixer_oss.lo
sun-objs		:= sun.lo mixer_sun.lo
ao-objs			:= ao.lo

op-$(CONFIG_ALSA)	+= alsa.so
op-$(CONFIG_ARTS)	+= arts.so
op-$(CONFIG_OSS)	+= oss.so
op-$(CONFIG_SUN)	+= sun.so
op-$(CONFIG_AO)		+= ao.so

$(alsa-objs): CFLAGS	+= $(ALSA_CFLAGS)
$(arts-objs): CFLAGS	+= $(ARTS_CFLAGS)
$(oss-objs):  CFLAGS	+= $(OSS_CFLAGS)
$(sun-objs):  CFLAGS	+= $(SUN_CFLAGS)
$(ao-objs):   CFLAGS	+= $(AO_CFLAGS)

alsa.so: $(alsa-objs)
	$(call cmd,ld_dl,$(ALSA_LIBS))

arts.so: $(arts-objs)
	$(call cmd,ld_dl,$(ARTS_LIBS))

oss.so: $(oss-objs)
	$(call cmd,ld_dl,$(OSS_LIBS))

sun.so: $(sun-objs)
	$(call cmd,ld_dl,$(SUN_LIBS))

ao.so: $(ao-objs)
	$(call cmd,ld_dl,$(AO_LIBS))
# }}}

# man {{{
man1	:= Doc/cmus.1 Doc/cmus-remote.1

$(man1): Doc/ttman

%.1: %.txt
	$(call cmd,ttman)

Doc/ttman: Doc/ttman.o
	$(call cmd,ld,)

quiet_cmd_ttman = MAN    $@
      cmd_ttman = Doc/ttman $< $@
# }}}

data		= $(wildcard data/*)

clean		+= *.o *.lo *.so cmus cmus-remote Doc/*.o Doc/ttman Doc/*.1
distclean	+= config.mk config/*.h tags

main: cmus cmus-remote
plugins: $(ip-y) $(op-y)
man: $(man1)

install-main: main
	$(INSTALL) -m755 $(bindir) cmus cmus-remote

install-plugins: plugins
	$(INSTALL) -m755 $(libdir)/cmus/ip $(ip-y)
	$(INSTALL) -m755 $(libdir)/cmus/op $(op-y)

install-data: man
	$(INSTALL) -m644 $(datadir)/cmus $(data)
	$(INSTALL) -m755 $(datadir)/doc/cmus/examples cmus-status-display
	$(INSTALL) -m644 $(mandir)/man1 $(man1)

install: all install-main install-plugins install-data

tags:
	exuberant-ctags *.[ch]

# generating tarball using GIT {{{
REV	= HEAD

# version from an annotated tag
_ver0	= $(shell git describe $(REV) 2>/dev/null)
# version from a plain tag
_ver1	= $(shell git describe --tags $(REV) 2>/dev/null)
# SHA1
_ver2	= $(shell git rev-parse --verify --short $(REV) 2>/dev/null)

TARNAME	= cmus-$(or $(_ver0),$(_ver1),g$(_ver2))

dist:
	@tarname=$(TARNAME);						\
	test "$(_ver2)" || { echo "No such revision $(REV)"; exit 1; };	\
	echo "   DIST   $$tarname.tar.bz2";				\
	git tar-tree $(REV)^{tree} $$tarname | bzip2 -c -9 > $$tarname.tar.bz2

# }}}

.PHONY: all main plugins man dist tags
.PHONY: install install-main install-plugins install-man
