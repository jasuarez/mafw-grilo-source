# Makefile.am for MAFW Grilo source library
# 
# Author: Xabier Rodríguez Calvar <xrcalvar@igalia.com>
#
# This file is under LGPL v2.1 license.

mafwext_LTLIBRARIES		= mafw-grilo-source.la

mafw_grilo_source_la_CPPFLAGS	= $(DEPS_CFLAGS) $(_CFLAGS)
mafw_grilo_source_la_LIBADD	= $(DEPS_LIBS)
mafw_grilo_source_la_LDFLAGS 	= -module -avoid-version $(_LDFLAGS)

noinst_HEADERS			= mafw-grilo-source.h

mafw_grilo_source_la_SOURCES	= mafw-grilo-source.c \
				  mafw-grilo-source.h

mafwextdir			= $(plugindir)

MAINTAINERCLEANFILES		= Makefile.in
