#!/bin/bash


bl_file=/etc/modprobe.d/tese-bl.conf

if [ "$1" != "" ]; then
	if  [ ! -f $bl_file ] || [ "$(cat $bl_file|grep $1)" == "" ] ; then
		echo "blacklist $1" >> $bl_file
	else
		echo "Module < $1 > already blacklisted."
	fi
else
	echo "USAGE: sudo blacklist.sh <module.ko>"
fi 
