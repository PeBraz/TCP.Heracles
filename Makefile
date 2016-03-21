
obj-m += heracles.o
heracles-objs := hydra.o tcp_heracles.o
#http://www.tldp.org/LDP/lkmpg/2.6/html/lkmpg.html

all: hydra.o tcp_heracles.o
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD)/src modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD)/src clean

