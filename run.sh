#!/bin/bash

img=heracles.img

if [ ! -f "$img" ] ; then 
	fallocate -l 10M heracles.img
fi

make -C src install
./server/server --upload $img --cong heracles


