AC_PREREQ([2.69])
AC_INIT([bepsi], [0.0.1], [https://github.com/moebiusV/bepsi/issues],
        [bepsi], [https://github.com/moebiusV/bepsi])
AC_CONFIG_SRCDIR([compat/imsg.c])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIRS([m4])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE([1.13 gnu std-options subdir-objects dist-xz no-dist-gzip])
AM_SILENT_RULES([yes])

AC_PROG_CC
AC_USE_SYSTEM_EXTENSIONS
AM_PROG_AR
LT_INIT([disable-shared])

dnl BSD functions the vendored imsg code needs; supplied from compat/ when
dnl libc lacks them.
AC_CHECK_FUNCS([explicit_bzero strlcpy freezero])
AM_CONDITIONAL([NEED_EXPLICIT_BZERO], [test "x$ac_cv_func_explicit_bzero" != xyes])
AM_CONDITIONAL([NEED_STRLCPY],        [test "x$ac_cv_func_strlcpy" != xyes])
AM_CONDITIONAL([NEED_FREEZERO],       [test "x$ac_cv_func_freezero" != xyes])

AC_SEARCH_LIBS([sqlite3_open], [sqlite3], [],
  [AC_MSG_ERROR([SQLite 3 is required (libsqlite3-dev)])])
AC_CHECK_HEADERS([sqlite3.h], [], [AC_MSG_ERROR([sqlite3.h is required])])

dnl Where to install the systemd unit and sysusers snippet.  Packagers
dnl override these, e.g. --with-systemdsystemunitdir=/lib/systemd/system.
AC_ARG_WITH([systemdsystemunitdir],
  [AS_HELP_STRING([--with-systemdsystemunitdir=DIR],
    [directory for systemd service files [LIBDIR/systemd/system]])],
  [], [with_systemdsystemunitdir='${libdir}/systemd/system'])
AC_ARG_WITH([sysusersdir],
  [AS_HELP_STRING([--with-sysusersdir=DIR],
    [directory for sysusers.d files [LIBDIR/sysusers.d]])],
  [], [with_sysusersdir='${libdir}/sysusers.d'])
AC_SUBST([systemdsystemunitdir], [$with_systemdsystemunitdir])
AC_SUBST([sysusersdir], [$with_sysusersdir])

dnl bepsi-httpd (vendored civetweb) needs OpenSSL 3 for TLS.
AC_ARG_ENABLE([httpd],
  [AS_HELP_STRING([--disable-httpd],
    [do not build bepsi-httpd (needs OpenSSL 3 and pthreads)])],
  [], [enable_httpd=yes])
AS_IF([test "x$enable_httpd" = xyes], [
  AC_CHECK_HEADERS([openssl/ssl.h], [],
    [AC_MSG_ERROR([openssl/ssl.h not found (libssl-dev); or use --disable-httpd])])
  AC_MSG_CHECKING([for OpenSSL 3])
  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <openssl/opensslv.h>
#if OPENSSL_VERSION_NUMBER < 0x30000000L
#error "OpenSSL 3 is required"
#endif]], [[]])],
    [AC_MSG_RESULT([yes])],
    [AC_MSG_RESULT([no]); AC_MSG_ERROR([OpenSSL 3 is required; or use --disable-httpd])])
  AC_CHECK_LIB([ssl], [SSL_new], [HTTPD_LIBS="-lssl -lcrypto -lpthread"],
    [AC_MSG_ERROR([libssl not found; or use --disable-httpd])], [-lcrypto])
])
AC_SUBST([HTTPD_LIBS])
AM_CONDITIONAL([BUILD_HTTPD], [test "x$enable_httpd" = xyes])

dnl The payment watchers need a websocket client library.
AC_ARG_ENABLE([watchers],
  [AS_HELP_STRING([--disable-watchers],
    [do not build the payment watchers (they need libwebsockets)])],
  [], [enable_watchers=yes])
AS_IF([test "x$enable_watchers" = xyes], [
  AC_CHECK_HEADERS([libwebsockets.h], [],
    [AC_MSG_ERROR([libwebsockets.h not found (libwebsockets-dev); or use --disable-watchers])])
  AC_CHECK_LIB([websockets], [lws_create_context], [LWS_LIBS=-lwebsockets],
    [AC_MSG_ERROR([libwebsockets not found; or use --disable-watchers])])
])
AC_SUBST([LWS_LIBS])
AM_CONDITIONAL([BUILD_WATCHERS], [test "x$enable_watchers" = xyes])

AC_CONFIG_FILES([scripts/bepsi-mkcert], [chmod +x scripts/bepsi-mkcert])
AC_CONFIG_FILES([scripts/bepsi-passwd], [chmod +x scripts/bepsi-passwd])
AC_CONFIG_FILES([Makefile compat/Makefile lib/Makefile src/Makefile data/Makefile www/Makefile scripts/Makefile tests/Makefile])
AC_OUTPUT
