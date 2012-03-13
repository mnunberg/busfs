#!/bin/bash
MOUNTPOINT=$1

if [ -z "$MOUNTPOINT" ]; then
    MOUNTPOINT="$PWD/mountpoint"
fi

BUSFS_PID=
set -e

fusermount -u $MOUNTPOINT || true;
./busfs -f -o intr -o nonempty -d $MOUNTPOINT & BUSFS_PID=$!
trap "kill -9 $BUSFS_PID; fusermount -u $MOUNTPOINT" EXIT
sleep 0.5


for tfile in tests/*; do
    echo "Running $tfile"
    bash -x $tfile $MOUNTPOINT "foo"
done

