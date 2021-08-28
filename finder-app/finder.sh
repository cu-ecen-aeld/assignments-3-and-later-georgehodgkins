#!/bin/bash

set -euo pipefail

if [[ $# -ne 2 ]]; then
	printf "Expected usage: %s <dir/to/search> <search pattern>\n" $0
	exit 1
fi

if [[ ! -d $1 ]]; then
	printf "%s is not a directory path.\n" $1
	exit 1
fi

file_count=$(find $(realpath $1) -type f | wc -l)
match_count=$(grep -rFhc -e "$2" $1 | awk '{n += $1}; END{print n}')

printf "The number of files is %d and the number of matching lines is %d\n" $file_count $match_count
exit 0

