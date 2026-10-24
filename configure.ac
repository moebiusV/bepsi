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

AC_CONFIG_FILES([Makefile compat/Makefile tests/Makefile])
AC_OUTPUT
