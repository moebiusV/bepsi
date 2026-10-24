AM_CPPFLAGS = -I$(top_srcdir)/compat -I$(top_srcdir)/lib
AM_CFLAGS = -Wall -Wextra

check_PROGRAMS = test_imsg test_proto
TESTS = $(check_PROGRAMS)

test_imsg_SOURCES = test_imsg.c
test_imsg_LDADD = $(top_builddir)/compat/libcompat.la

test_proto_SOURCES = test_proto.c
test_proto_LDADD = $(top_builddir)/lib/libbepsi.la \
		   $(top_builddir)/compat/libcompat.la
