#!/bin/sh

autoreconf -v -f -i || exit 1
test $NOCONFIGURE || ./configure --enable-debug --enable-maintainer-mode "$@"
