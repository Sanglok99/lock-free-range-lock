obj-m := range_lock_module.o

range_lock_module-objs := lockfree_list.o f3fs_range_lock_mod.o

KDIR := /lib/modules/$(shell uname -r)/build

PWD := $(shell pwd)

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean

