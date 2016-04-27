
ip=188.166.39.226
all:
	$(MAKE) -C server
	$(MAKE) -C src
	
upload: 
	# upload files	
	$(MAKE) -C src upload
	# debugging console
#	gnome-terminal -e "ssh root@$(ip) \" dmesg -wH \" "	
	#make and run server
#	ssh root@188.166.39.226 "cd TCP.Heracles && make && ./run.sh"

terminal:
	gnome-terminal -e "ssh root@$(ip)"	

clean: 
	$(MAKE) -C server clean
	$(MAKE) -C src clean
