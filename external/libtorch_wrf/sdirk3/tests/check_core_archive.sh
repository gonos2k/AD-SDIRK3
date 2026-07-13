#!/bin/sh
# check_core_archive.sh (PR 5, commit 3) — archive/member <-> manifest contract.
#
# Usage: check_core_archive.sh <archive> <manifest> [<other-archive-for-parity>]
#
# Asserts EXACT set equality between the archive's normalized object stems and
# the manifest's source stems: missing, extra, and duplicate members all fail.
# Any wrf_sdirk3_cmake_stub* member fails unconditionally (the pre-PR5 fake
# archive marker). With a third argument, additionally asserts the two
# archives' member sets are identical (Make/CMake parity — membership, not
# byte identity).
set -eu
ARCHIVE="$1"; MANIFEST="$2"; PARITY="${3:-}"

[ -f "$ARCHIVE" ]  || { echo "FAIL: archive not found: $ARCHIVE"; exit 1; }
[ -f "$MANIFEST" ] || { echo "FAIL: manifest not found: $MANIFEST"; exit 1; }

norm_members() {
    # foo.cpp.o -> foo ; foo.o -> foo ; foo.obj -> foo ; drop ranlib SYMDEF
    ar t "$1" | grep -v '^__\.SYMDEF' \
        | sed -e 's/\.cpp\.o$//' -e 's/\.obj$//' -e 's/\.o$//' | sort
}

exp=$(awk 'NF && $1 !~ /^#/ { print $1 }' "$MANIFEST" | sed 's/\.cpp$//' | sort)
got=$(norm_members "$ARCHIVE")

if ar t "$ARCHIVE" | grep -q '^wrf_sdirk3_cmake_stub'; then
    echo "FAIL: pre-PR5 stub member present in $ARCHIVE"; exit 1
fi
dups=$(printf '%s\n' "$got" | uniq -d)
if [ -n "$dups" ]; then
    echo "FAIL: duplicate members in $ARCHIVE:"; printf '%s\n' "$dups"; exit 1
fi
if [ "$exp" != "$got" ]; then
    echo "FAIL: member set != manifest set for $ARCHIVE"
    echo "--- manifest-only:"; printf '%s\n' "$exp" | grep -Fxv "$got" || true
    echo "--- archive-only:";  printf '%s\n' "$got" | grep -Fxv "$exp" || true
    exit 1
fi
echo "OK: $ARCHIVE members == manifest ($(printf '%s\n' "$got" | wc -l | tr -d ' ') members)"

if [ -n "$PARITY" ]; then
    [ -f "$PARITY" ] || { echo "FAIL: parity archive not found: $PARITY (build the production Make archive first)"; exit 1; }
    got2=$(norm_members "$PARITY")
    if [ "$got" != "$got2" ]; then
        echo "FAIL: Make/CMake archive membership differs"; exit 1
    fi
    echo "OK: Make/CMake archive membership identical"
fi
