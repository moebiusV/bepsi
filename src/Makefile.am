## bepsid: the hardware daemon (GPIO, stock database).

sbin_PROGRAMS = bepsid

bepsid_SOURCES = bepsid.c conf.c conf.h db.c db.h gpio.c gpio.h
bepsid_CPPFLAGS = -I$(top_srcdir)/compat -I$(top_srcdir)/lib \
		  -DSYSCONFDIR='"$(sysconfdir)"'
bepsid_CFLAGS = -Wall -Wextra
bepsid_LDADD = $(top_builddir)/lib/libbepsi.la \
	       $(top_builddir)/compat/libcompat.la
bepsid_LDADD += $(LIBS)
