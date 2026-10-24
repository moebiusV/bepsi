## Vendored OpenBSD imsg(3) (ISC) plus shims for functions glibc may lack.

noinst_LTLIBRARIES = libcompat.la

libcompat_la_SOURCES = imsg.c imsg-buffer.c imsg.h compat.h
# The vendored files are unmodified and do not include config.h, so force it
# (for _GNU_SOURCE) and the shim declarations.
libcompat_la_CPPFLAGS = -I$(top_builddir) -I$(srcdir) -include config.h -include $(srcdir)/compat.h

if NEED_EXPLICIT_BZERO
libcompat_la_SOURCES += explicit_bzero.c
endif
if NEED_STRLCPY
libcompat_la_SOURCES += strlcpy.c
endif
if NEED_FREEZERO
libcompat_la_SOURCES += freezero.c
endif

## The vendored man page is kept for reference only, not installed.
EXTRA_DIST = imsg_init.3
