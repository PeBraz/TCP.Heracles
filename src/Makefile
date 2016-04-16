
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
	tc qdisc add dev lo root tbf rate 1kbit latency 50ms burst 3000

unlimit: 
	tc qdisc delete dev lo root tbf rate 1kbit latency 50ms burst 3000

install:
	dmesg -C
	insmod heracles.ko

uninstall:
	rmmod heracles.ko
