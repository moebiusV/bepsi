## bepsid: the hardware daemon (GPIO, stock database).

sbin_PROGRAMS = bepsid
bin_PROGRAMS = bepsictl

bepsid_SOURCES = bepsid.c conf.c conf.h db.c db.h gpio.c gpio.h
bepsid_CPPFLAGS = -I$(top_srcdir)/compat -I$(top_srcdir)/lib \
		  -DSYSCONFDIR='"$(sysconfdir)"'
bepsid_CFLAGS = -Wall -Wextra
bepsid_LDADD = $(top_builddir)/lib/libbepsi.la \
	       $(top_builddir)/compat/libcompat.la
bepsid_LDADD += $(LIBS)

bepsictl_SOURCES = bepsictl.c
bepsictl_CPPFLAGS = -I$(top_srcdir)/compat -I$(top_srcdir)/lib
bepsictl_CFLAGS = -Wall -Wextra
bepsictl_LDADD = $(top_builddir)/lib/libbepsi.la \
	       $(top_builddir)/compat/libcompat.la

if BUILD_WATCHERS
sbin_PROGRAMS += bepsi-watch-ws

bepsi_watch_ws_SOURCES = watch-ws.c
bepsi_watch_ws_CPPFLAGS = -I$(top_srcdir)/compat -I$(top_srcdir)/lib \
		  -DSYSCONFDIR='"$(sysconfdir)"'
bepsi_watch_ws_CFLAGS = -Wall -Wextra
bepsi_watch_ws_LDADD = $(top_builddir)/lib/libbepsi.la \
	       $(top_builddir)/compat/libcompat.la $(LWS_LIBS)
endif
