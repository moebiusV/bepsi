## Process this file with automake to produce Makefile.in

ACLOCAL_AMFLAGS = -I m4

SUBDIRS = compat lib src data
if BUILD_HTTPD
SUBDIRS += www
endif
SUBDIRS += scripts tests
DIST_SUBDIRS = compat lib src data www scripts tests

EXTRA_DIST = README.md COPYING

dist_doc_DATA = etc/bepsid.conf.example etc/watch-ws.conf.example etc/httpd.conf.example
