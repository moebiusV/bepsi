## bepsid: the hardware daemon (GPIO, stock database).

EXTRA_DIST =

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

if BUILD_HTTPD
sbin_PROGRAMS += bepsi-httpd

## The vendored civetweb, built as a stand-alone server (see
## third_party/civetweb/README.bepsi).  Its sources are not ours, so they are
## built without our warning flags.
bepsi_httpd_SOURCES = $(top_srcdir)/third_party/civetweb/main.c \
	$(top_srcdir)/third_party/civetweb/civetweb.c \
	$(top_srcdir)/third_party/civetweb/civetweb.h
bepsi_httpd_CPPFLAGS = -I$(top_srcdir)/third_party/civetweb \
	-DNO_SSL_DL -DOPENSSL_API_3_0 -DUSE_IPV6 \
	-DBEPSI_PACKAGE='"$(PACKAGE_NAME)"' -DBEPSI_VERSION='"$(PACKAGE_VERSION)"'
bepsi_httpd_LDADD = $(HTTPD_LIBS)

## The pages: one CGI program, installed as index.cgi, status.cgi and
## admin.cgi next to the static files (the name chooses the page).
pkglibexec_PROGRAMS = bepsi-cgi
bepsi_cgi_SOURCES = bepsi-cgi.c
bepsi_cgi_CPPFLAGS = -I$(top_srcdir)/compat -I$(top_srcdir)/lib
bepsi_cgi_CFLAGS = -Wall -Wextra
bepsi_cgi_LDADD = $(top_builddir)/lib/libbepsi.la \
	       $(top_builddir)/compat/libcompat.la

install-exec-hook:
	$(MKDIR_P) "$(DESTDIR)$(pkgdatadir)/www"
	for p in index pay status admin; do \
	  $(LN_S) -f "$(pkglibexecdir)/bepsi-cgi" "$(DESTDIR)$(pkgdatadir)/www/$$p.cgi"; \
	done

uninstall-hook:
	for p in index pay status admin; do rm -f "$(DESTDIR)$(pkgdatadir)/www/$$p.cgi"; done
EXTRA_DIST += $(top_srcdir)/third_party/civetweb/handle_form.inl \
	$(top_srcdir)/third_party/civetweb/match.inl \
	$(top_srcdir)/third_party/civetweb/md5.inl \
	$(top_srcdir)/third_party/civetweb/openssl_dl.inl \
	$(top_srcdir)/third_party/civetweb/response.inl \
	$(top_srcdir)/third_party/civetweb/sha1.inl \
	$(top_srcdir)/third_party/civetweb/sort.inl \
	$(top_srcdir)/third_party/civetweb/timer.inl \
	$(top_srcdir)/third_party/civetweb/LICENSE.md \
	$(top_srcdir)/third_party/civetweb/CREDITS.md \
	$(top_srcdir)/third_party/civetweb/README.bepsi
endif
