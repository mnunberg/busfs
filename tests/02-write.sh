#!/bin/sh
FILE=$1/$2
for i in {0..10}
do
    echo "This is line $i"
done | cat > $FILE
