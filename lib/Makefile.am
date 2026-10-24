## libbepsi: the wire protocol and client calls shared by bepsid, the
## watchers and the web UI.

noinst_LTLIBRARIES = libbepsi.la

libbepsi_la_SOURCES = bepsi_proto.c bepsi_proto.h bepsi_client.c bepsi_client.h \
		      bepsi_web.c bepsi_web.h bepsi_pay.c bepsi_pay.h \
		      bepsi_qr.c bepsi_qr.h
libbepsi_la_CPPFLAGS = -I$(top_srcdir)/compat -I$(top_srcdir)/third_party/qrcodegen
libbepsi_la_CFLAGS = -Wall -Wextra

## The vendored QR generator is not ours, so it is built without our flags.
noinst_LTLIBRARIES += libqrcodegen.la
libqrcodegen_la_SOURCES = $(top_srcdir)/third_party/qrcodegen/qrcodegen.c \
			  $(top_srcdir)/third_party/qrcodegen/qrcodegen.h
libbepsi_la_LIBADD = libqrcodegen.la
EXTRA_DIST = $(top_srcdir)/third_party/qrcodegen/README.bepsi
