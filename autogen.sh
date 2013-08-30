##----------------------------------------------------------------------------
## NVMKV
## |- Copyright 2012-2013 Fusion-io, Inc.

## This program is free software; you can redistribute it and/or modify it under
## the terms of the GNU General Public License version 2 as published by the Free
## Software Foundation;
## This program is distributed in the hope that it will be useful, but WITHOUT
## ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
## FOR A PARTICULAR PURPOSE. See the GNU General
## Public License v2 for more details.
## A copy of the GNU General Public License v2 is provided with this package and
## can also be found at: http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
## You should have received a copy of the GNU General Public License along with
## this program; if not, write to the Free Software Foundation, Inc., 59 Temple
## Place, Suite 330, Boston, MA 02111-1307 USA.
##----------------------------------------------------------------------------
#!/bin/sh

srcdir=$(dirname $0)

currdir=$(pwd)
cd $srcdir

quit=0

# check for required tools
(autopoint --version) < /dev/null > /dev/null 2>&1 || { echo "You must have autopoint install"; quit=1; }
(autoheader --version) < /dev/null > /dev/null 2>&1 || { echo "You must have autoheader install"; quit=1; }
(autoconf --version) < /dev/null > /dev/null 2>&1 || { echo "You must have autoconf install"; quit=1; }
(automake --version) < /dev/null > /dev/null 2>&1 || { echo "You must have automake install"; quit=1; }
(libtool --version) < /dev/null > /dev/null 2>&1 || { echo "You must have libtool install"; quit=1; }
# check for libtoolize version >= 2.x.x
libtlz_ver=$(libtoolize --version | awk '/^libtoolize/ {print $4}')
libtlz_ver=${libtlz_ver:-"missing"}

if test "$quit" -eq 1; then
    exit 1
fi

rm -rf autom4te.cache

aclocal
# We support older versions with newer m4 files, so ignore the warnings if any
libtoolize 2> /dev/null
autoheader
automake --force-missing --add-missing
autoconf

echo "Run ./configure and make to compile"

cd $currdir
