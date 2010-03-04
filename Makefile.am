# Makefile.am for MAFW Grilo source library
# 
# Author: Xabier Rodríguez Calvar <xrcalvar@igalia.com>
#
# This file is under LGPL v2.1 license.

SUBDIRS				= mafw-grilo-source

noinst_DATA			= mafw-grilo-source-uninstalled.pc
EXTRA_DIST			= mafw-grilo-source-uninstalled.pc.in

# Extra clean files so that maintainer-clean removes *everything*

MAINTAINERCLEANFILES		= aclocal.m4 configure Makefile.in \
				  config.h.in *-stamp

maintainer-clean-local:
	-rm -rf build-aux
