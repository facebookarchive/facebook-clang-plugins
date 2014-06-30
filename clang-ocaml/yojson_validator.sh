#!/bin/bash
# Script to validate Yojson outputs w.r.t. ATD specifications.
# This works by running a given 'converter' to parse and pretty-print the outputs, then observing the difference with gunzip + pretty-print.

CONVERTER="$1"
shift

while [ -n "$1" ]
do
    if ! diff -q <(gunzip -c "$1"| ydump) <("$CONVERTER" --pretty "$1" /dev/stdout) >/dev/null 2>&1; then
        echo "The file '$1' does not respect the ATD format implemented by $CONVERTER."
        echo "Here is the command that shows the problem:"
        echo "  diff <(gunzip -c \"$1\"| ydump) <(\"$CONVERTER\" --pretty \"$1\" /dev/stdout)"
        exit 2
    fi

    shift;
done
