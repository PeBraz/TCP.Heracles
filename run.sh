#!/bin/bash

img=heracles.img

if [ ! -f "$img" ] ; then 
	fallocate -l 10M heracles.img
fi

cd src
make install
cd ..
./server/server --upload $img --cong heracles


