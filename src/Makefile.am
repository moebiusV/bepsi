## bepsid: the hardware daemon (GPIO, stock database).

sbin_PROGRAMS = bepsid

bepsid_SOURCES = bepsid.c conf.c conf.h db.c db.h gpio.c gpio.h
bepsid_CPPFLAGS = -I$(top_srcdir)/compat -I$(top_srcdir)/lib \
		  -DSYSCONFDIR='"$(sysconfdir)"'
bepsid_CFLAGS = -Wall -Wextra
bepsid_LDADD = $(top_builddir)/lib/libbepsi.la \
	       $(top_builddir)/compat/libcompat.la
bepsid_LDADD += $(LIBS)

if BUILD_WATCHERS
sbin_PROGRAMS += bepsi-watch-lightning

bepsi_watch_lightning_SOURCES = watch-lightning.c
bepsi_watch_lightning_CPPFLAGS = -I$(top_srcdir)/compat -I$(top_srcdir)/lib \
		  -DSYSCONFDIR='"$(sysconfdir)"'
bepsi_watch_lightning_CFLAGS = -Wall -Wextra
bepsi_watch_lightning_LDADD = $(top_builddir)/lib/libbepsi.la \
	       $(top_builddir)/compat/libcompat.la $(LWS_LIBS)
endif
