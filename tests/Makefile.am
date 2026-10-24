AM_CPPFLAGS = -I$(top_srcdir)/compat -I$(top_srcdir)/lib
AM_CFLAGS = -Wall -Wextra

check_PROGRAMS = test_imsg test_proto test_wsmsg test_web test_qr test_pay test_bepsid inputs_client
TESTS = test_imsg test_proto test_wsmsg test_web test_qr test_pay test_bepsid bepsictl.sh inputs.sh qr-decode.sh admin-js.sh
TESTS_ENVIRONMENT = BEPSID=$(abs_top_builddir)/src/bepsid; export BEPSID; BEPSICTL=$(abs_top_builddir)/src/bepsictl; export BEPSICTL; TEST_QR=$(abs_top_builddir)/tests/test_qr; export TEST_QR; INPUTS_CLIENT=$(abs_top_builddir)/tests/inputs_client; export INPUTS_CLIENT; ADMIN_JS=$(abs_top_srcdir)/www/admin.js; export ADMIN_JS; ADMIN_JS_TEST=$(abs_top_srcdir)/tests/admin_js_test.js; export ADMIN_JS_TEST;
if BUILD_WATCHERS
TESTS += watch-ws.sh
TESTS_ENVIRONMENT += WATCHER=$(abs_top_builddir)/src/bepsi-watch-ws; export WATCHER; FAKE_WS=$(abs_srcdir)/fake_ws.py; export FAKE_WS;
endif
EXTRA_DIST = watch-ws.sh fake_ws.py bepsictl.sh httpd.sh web.sh qr-decode.sh inputs.sh admin-js.sh admin_js_test.js
if BUILD_HTTPD
TESTS += httpd.sh web.sh
TESTS_ENVIRONMENT += HTTPD=$(abs_top_builddir)/src/bepsi-httpd; export HTTPD; CGI=$(abs_top_builddir)/src/bepsi-cgi; export CGI; WWW_SRC=$(abs_top_srcdir)/www; export WWW_SRC; PASSWD_TOOL=$(abs_top_builddir)/scripts/bepsi-passwd; export PASSWD_TOOL;
endif

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

test_web_SOURCES = test_web.c
test_web_LDADD = $(top_builddir)/lib/libbepsi.la \
		 $(top_builddir)/compat/libcompat.la

test_qr_SOURCES = test_qr.c
test_qr_CPPFLAGS = -I$(top_srcdir)/lib
test_qr_LDADD = $(top_builddir)/lib/libbepsi.la \
		$(top_builddir)/compat/libcompat.la

test_pay_SOURCES = test_pay.c
test_pay_LDADD = $(top_builddir)/lib/libbepsi.la \
		 $(top_builddir)/compat/libcompat.la

inputs_client_SOURCES = inputs_client.c
inputs_client_LDADD = $(top_builddir)/lib/libbepsi.la \
		      $(top_builddir)/compat/libcompat.la
