#!/bin/bash

die() {
	echo "$*" >&2
	exit 1
}
test $# -eq 1 || die "Usage: ${0} [log directory]"

test -e ltc ||
test -e ltc.c && test -e Makefile && make ||
die "Ensure ltc.c and Makefile exist."

test -d "$1" || die "$1 is does not exist or not a directory."
./ltc "$1" | sort -n -k1,1
