
ip=188.166.39.226
all:
	$(MAKE) -C server
	$(MAKE) -C src
	
	
upload: 
	
	$(MAKE) -C src upload
	ssh root@188.166.39.226 "cd TCP.Heracles && make && ./run.sh"


clean: 
	$(MAKE) -C server clean
	$(MAKE) -C src clean
