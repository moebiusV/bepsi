AM_CPPFLAGS = -I$(top_srcdir)/compat -I$(top_srcdir)/lib
AM_CFLAGS = -Wall -Wextra

check_PROGRAMS = test_imsg test_proto test_bepsid
TESTS = $(check_PROGRAMS)
TESTS_ENVIRONMENT = BEPSID=$(abs_top_builddir)/src/bepsid; export BEPSID;

test_imsg_SOURCES = test_imsg.c
test_imsg_LDADD = $(top_builddir)/compat/libcompat.la

test_proto_SOURCES = test_proto.c
test_proto_LDADD = $(top_builddir)/lib/libbepsi.la \
		   $(top_builddir)/compat/libcompat.la

test_bepsid_SOURCES = test_bepsid.c
test_bepsid_LDADD = $(top_builddir)/lib/libbepsi.la \
		    $(top_builddir)/compat/libcompat.la
