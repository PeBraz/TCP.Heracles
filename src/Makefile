
obj-m += heracles.o
heracles-objs := hydra.o tcp_heracles.o
#http://www.tldp.org/LDP/lkmpg/2.6/html/lkmpg.html
ccflags-y := -std=gnu99 -Wno-declaration-after-statement
all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

