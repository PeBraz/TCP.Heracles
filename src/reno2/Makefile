
obj-m += heracles.o
heracles-objs := hydra.o tcp_reno.o
ccflags-y := -std=gnu99 -Wno-declaration-after-statement -Wall -g
all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

upload:
	scp -r ~/Desktop/TESE/TCP.Heracles/src root@188.166.39.226:~/TCP.Heracles/


install:
	insmod heracles.ko
	echo reno2 > /proc/sys/net/ipv4/tcp_congestion_control

uninstall:
	rmmod heracles.ko
