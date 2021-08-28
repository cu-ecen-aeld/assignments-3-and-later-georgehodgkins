#!/bin/bash

set -euo pipefail

if [[ $# -ne 2 ]]; then
	printf "Expected usage: %s <path/to/file> <write string>\n" $0
	exit 1
fi

set +e

if $(echo "$1" | grep -q ".*/"); then
	(mkdir -p ${1%/*}) >/dev/null 2>&1
fi

if [[ $? -ne 0 ]]; then
	printf "Could not create path %s.\n" ${1%/*}
	exit 1
fi

(echo $2 > $1) >/dev/null 2>&1

if [[ $? -ne 0 ]]; then
	printf "Could not write to file %s.\n" $(realpath $1)
	exit 1
fi

exit 0 

