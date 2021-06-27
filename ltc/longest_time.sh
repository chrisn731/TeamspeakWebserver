#!/bin/bash

die() {
	echo "$*" >&2
	exit 1
}
test ${#} -eq 2 || die "Usage: ${0} [log directory]"
make
./ltc ${1} | sort -n -k1,1
