#!/bin/sh
# Tester script for assignment 1 and assignment 2
# Author: Siddhant Jajoo

set -e
set -u

NUMFILES=10
WRITESTR=AELD_IS_FUN
WRITEDIR=/tmp/aeld-data
username=$(cat /etc/finder-app/conf/username.txt)
RESULTFILE=/tmp/assigmnent-4-result.txt

WRITER=writer
FINDER=finder.sh
set +e 
if [[ -e $WRITER ]]; then
	WRITER=./$WRITER
elif [[ -z $(command -v $WRITER) ]]; then
   	printf "Can't find %s in current directory or PATH!\n" $WRITER
	exit 1
fi

if [[ -e $FINDER ]]; then
	FINDER=./$FINDER
elif [[ -z $(command -v $FINDER) ]]; then
   	printf "Can't find %s in current directory or PATH!\n" $FINDER
	exit 1
fi
set -e

if [ $# -lt 2 ]
then
	echo "Using default value ${WRITESTR} for string to write"
	if [ $# -lt 1 ]
	then
		echo "Using default value ${NUMFILES} for number of files to write"
	else
		NUMFILES=$1
	fi	
else
	NUMFILES=$1
	WRITESTR=$2
fi

MATCHSTR="The number of files is ${NUMFILES} and the number of matching lines is ${NUMFILES}"

echo "Writing ${NUMFILES} files containing string ${WRITESTR} to ${WRITEDIR}"

rm -rf "${WRITEDIR}"
mkdir -p "$WRITEDIR"

#The WRITEDIR is in quotes because if the directory path consists of spaces, then variable substitution will consider it as multiple argument.
#The quotes signify that the entire string in WRITEDIR is a single string.
#This issue can also be resolved by using double square brackets i.e [[ ]] instead of using quotes.
if [ -d "$WRITEDIR" ]
then
	echo "$WRITEDIR created"
else
	exit 1
fi


for i in $( seq 1 $NUMFILES)
do
	$WRITER "$WRITEDIR/${username}$i.txt" "$WRITESTR"
done

set +e
echo ${OUTPUTSTRING} | grep "${MATCHSTR}"
if [ $? -eq 0 ]; then
	echo "success"
	echo ${OUTPUTSTRING} > ${RESULTFILE}
	exit 0
else
	echo "failed: expected  ${MATCHSTR} in ${OUTPUTSTRING} but instead found"
	exit 1
fi
