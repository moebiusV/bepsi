## libbepsi: the wire protocol and client calls shared by bepsid, the
## watchers and the web UI.

noinst_LTLIBRARIES = libbepsi.la

libbepsi_la_SOURCES = bepsi_proto.c bepsi_proto.h bepsi_client.c bepsi_client.h \
		      bepsi_web.c bepsi_web.h
libbepsi_la_CPPFLAGS = -I$(top_srcdir)/compat
libbepsi_la_CFLAGS = -Wall -Wextra
