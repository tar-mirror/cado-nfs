#!/usr/bin/env bash

set -e

# This is readlink -f on many unices. Alas, not on mac.
if [ "`uname -s`" = Darwin ] ; then
    # use code from https://github.com/mkropat/sh-realpath, MIT-licensed.
    source "`dirname $0`/realpath.sh"
    readlink_f() { realpath "$@" ; }
else
    readlink_f() { readlink -f "$@" ; }
fi

me="$(readlink_f "$0")"
dir="$(dirname "$me")"
"$dir/check_file_lists.pl"
"$dir/check_compilation_units_policy.pl"
