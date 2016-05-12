delay=50ms
int=nk_tap_pebraz
up=10
down=10

obj-m += heracles.o
heracles-objs := hydra.o tcp_heracles.o
ccflags-y :=  -Wno-declaration-after-statement -Wall -g
all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

upload:
	scp -r ~/Desktop/TESE/TCP.Heracles/* root@188.166.39.226:~/TCP.Heracles/


limit:
	tc qdisc add dev $(int) root handle 1: htb default 1
	tc class add dev $(int) parent 1: classid 1:1 htb rate 5Mbit ceil 5Mbit
	tc qdisc add dev $(int) parent 1:1 handle 2: pfifo limit 10

unlimit: 
	tc qdisc del dev $(int) root

install:
	insmod heracles.ko
	dmesg -C

uninstall:
	rmmod heracles.ko
