AM_CPPFLAGS = -I$(top_srcdir)/compat -I$(top_srcdir)/lib
AM_CFLAGS = -Wall -Wextra

check_PROGRAMS = test_imsg test_proto test_wsmsg test_bepsid
TESTS = $(check_PROGRAMS) bepsictl.sh
TESTS_ENVIRONMENT = BEPSID=$(abs_top_builddir)/src/bepsid; export BEPSID; BEPSICTL=$(abs_top_builddir)/src/bepsictl; export BEPSICTL;
if BUILD_WATCHERS
TESTS += watch-ws.sh
TESTS_ENVIRONMENT += WATCHER=$(abs_top_builddir)/src/bepsi-watch-ws; export WATCHER; FAKE_WS=$(abs_srcdir)/fake_ws.py; export FAKE_WS;
endif
EXTRA_DIST = watch-ws.sh fake_ws.py bepsictl.sh

test_wsmsg_SOURCES = test_wsmsg.c
test_wsmsg_LDADD = $(top_builddir)/lib/libbepsi.la \
		   $(top_builddir)/compat/libcompat.la

test_imsg_SOURCES = test_imsg.c
test_imsg_LDADD = $(top_builddir)/compat/libcompat.la

test_proto_SOURCES = test_proto.c
test_proto_LDADD = $(top_builddir)/lib/libbepsi.la \
		   $(top_builddir)/compat/libcompat.la

test_bepsid_SOURCES = test_bepsid.c
test_bepsid_LDADD = $(top_builddir)/lib/libbepsi.la \
		    $(top_builddir)/compat/libcompat.la
