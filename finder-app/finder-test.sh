#!/bin/sh
# Tester script for assignment 1 and assignment 2
# Author: Siddhant Jajoo

set -e
set -u

NUMFILES=10
WRITESTR=AELD_IS_FUN
WRITEDIR=/tmp/aeld-data
username=$(cat /etc/finder-app/conf/username.txt)

if [ $# -lt 3 ]
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
    WRITEDIR=/tmp/aeld-data/$3
fi

MATCHSTR="The number of files are ${NUMFILES} and the number of matching lines are ${NUMFILES}"

echo "Writing ${NUMFILES} files containing string ${WRITESTR} to ${WRITEDIR}"

# # Clean previous build artifacts
# echo "Cleaning previous build artifacts..."
# rm -f writer   # Remove the old writer binary if it exists
# rm -f *.o      # Remove any old object files if they exist

# # Compile the writer utility using native compilation (assuming writer.c is the C source file)
# echo "Compiling writer utility..."
# gcc -o writer writer.c

# # Ensure the writer compiled correctly
# if [ ! -f writer ]; then
#     echo "Compilation failed. Exiting."
#     exit 1
# fi

# Create the directory if it's for assignment 2
assignment=$(cat  /etc/finder-app/conf/assignment.txt)

if [ $assignment != 'assignment1' ]
then
    mkdir -p "$WRITEDIR"

    if [ -d "$WRITEDIR" ]
    then
        echo "$WRITEDIR created"
    else
        exit 1
    fi
fi

# Writing files with the new compiled writer utility
for i in $( seq 1 $NUMFILES)
do
    writer "$WRITEDIR/${username}$i.txt" "$WRITESTR"  # Use the compiled writer binary
done

# Run the finder script
OUTPUTSTRING=$(finder.sh "$WRITEDIR" "$WRITESTR")
echo ${OUTPUTSTRING} > /tmp/assignment4-result.txt

# Clean up the temporary directories
rm -rf /tmp/aeld-data

set +e
echo ${OUTPUTSTRING} | grep "${MATCHSTR}"
if [ $? -eq 0 ]; then
    echo "success"
    exit 0
else
    echo "failed: expected  ${MATCHSTR} in ${OUTPUTSTRING} but instead found"
    exit 1
fi
