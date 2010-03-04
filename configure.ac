# configure.ac for MAFW Grilo source library
# 
# Author: Xabier Rodríguez Calvar <xrcalvar@igalia.com>
#
# This file is under LGPL v2.1 license.

AC_PREREQ([2.53])
AC_INIT([mafw-grilo-source], [0.1])

AC_CONFIG_SRCDIR([mafw-grilo-source/mafw-grilo-source.h])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_AUX_DIR([build-aux])

AM_INIT_AUTOMAKE([foreign])
AM_MAINTAINER_MODE

AC_DISABLE_STATIC

AC_PROG_LIBTOOL
AC_PROG_INSTALL

dnl Prerequisites.

PKG_CHECK_MODULES(DEPS, [
                        gobject-2.0 >= 2.12
                        mafw >= 0.1
                        grilo-0.1
                        ])

plugindir=`$PKG_CONFIG --variable=plugindir mafw`
AC_SUBST(plugindir)

dnl Check for glib-genmarshal.

GLIB_GENMARSHAL=`pkg-config --variable=glib_genmarshal glib-2.0`
AC_SUBST(GLIB_GENMARSHAL)

AC_SUBST([_CFLAGS])
AC_SUBST([_LDFLAGS])
_CFLAGS="-Wall -Wmissing-prototypes -Wstrict-prototypes -Wmissing-declarations -g3"

dnl Output files.

AC_CONFIG_FILES([
        Makefile
        mafw-grilo-source/Makefile
	debian/mafw-grilo-source.install
        mafw-grilo-source-uninstalled.pc
])

AC_OUTPUT
