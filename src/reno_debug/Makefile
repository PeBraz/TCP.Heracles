
module_name=reno_debug

obj-m += $(module_name).o
ccflags-y :=  -Wno-declaration-after-statement -Wall -g

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
install:
	insmod $(module_name).ko
	dmesg -C

uninstall:
	rmmod $(module_name).ko
