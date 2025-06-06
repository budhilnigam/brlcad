#!/bin/sh
#                          D S P . S H
# BRL-CAD
#
# Copyright (c) 2010-2025 United States Government as represented by
# the U.S. Army Research Laboratory.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following
# disclaimer in the documentation and/or other materials provided
# with the distribution.
#
# 3. The name of the author may not be used to endorse or promote
# products derived from this software without specific prior written
# permission.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
# OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
# GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
# NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
###

# Ensure /bin/sh
export PATH || (echo "This isn't sh."; sh $0 $*; kill $$)

# source common library functionality, setting ARGS, NAME_OF_THIS,
# PATH_TO_THIS, and THIS.
. "$1/regress/library.sh"

# Tests should use a local cache
BU_DIR_CACHE="`pwd`/cache"
rm -rf $BU_DIR_CACHE && mkdir $BU_DIR_CACHE
export BU_DIR_CACHE
LIBRT_CACHE="`pwd`/rtcache"
rm -rf $LIBRT_CACHE && mkdir $LIBRT_CACHE
export LIBRT_CACHE

if test "x$LOGFILE" = "x" ; then
    LOGFILE=`pwd`/dsp.log
    rm -f $LOGFILE
fi
log "=== TESTING dsp primitive ==="

MGED="`ensearch mged`"
if test ! -f "$MGED" ; then
    log "Unable to find mged, aborting"
    exit 1
fi

CV="`ensearch cv`"
if test ! -f "$CV" ; then
    log "Unable to find cv, aborting"
    exit 1
fi

A2P="`ensearch asc2pix`"
if test ! -f "$A2P" ; then
    log "Unable to find asc2pix, aborting"
    exit 1
fi

FAILED=0

# note that cases 0 and 1 are not functional yet
#CASES='0 1 2 3'
CASES='2 3'

for i in $CASES ; do
    run "$PATH_TO_THIS/run-dsp-case-set-$i.sh" "$1"
    ret=$?
    if [ $ret -gt 0 ] ; then
	FAILED="`expr $FAILED + 1`"
    fi
done

# TODO: create random 10x10 datasets


if [ $FAILED = 0 ] ; then
    log "-> dsp.sh succeeded"
else
    log "-> dsp.sh FAILED, see $LOGFILE"
    cat "$LOGFILE"
fi

# Cleanup
rm -rf "$BU_DIR_CACHE"
rm -rf "$LIBRT_CACHE"

exit $FAILED

# Local Variables:
# mode: sh
# tab-width: 8
# sh-indentation: 4
# sh-basic-offset: 4
# indent-tabs-mode: t
# End:
# ex: shiftwidth=4 tabstop=8
