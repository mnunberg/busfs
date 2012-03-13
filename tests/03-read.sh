#!/bin/sh
cat $1/$2 & PID=$!

sleep 1

kill $PID
