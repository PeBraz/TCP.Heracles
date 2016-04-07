
obj-m += tester.o
tester-objs := hydra.o hydra_tester.o
#http://www.tldp.org/LDP/lkmpg/2.6/html/lkmpg.html
ccflags-y := -std=gnu99 -g -Wno-declaration-after-statement
all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean


install:
	sudo insmod tester.ko 
uninstall:
	sudo rmmod tester.ko
show:
	tail -f /var/log/kern.log

upload:
	scp -r ~/Desktop/TESE/TCP.Heracles/src root@188.166.30.112:~/TCP.Heracles

## helpful stuff (http://derekmolloy.ie/writing-a-linux-kernel-module-part-1-introduction/)
## 
##	modinfo <*.ko> - list module information
##	tail -f /var/log/kern.log  - print debugging 
##	(or dmesg)
##

