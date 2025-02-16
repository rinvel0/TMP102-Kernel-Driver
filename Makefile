obj-m += tmp102_driver.o
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

default:
	$(MAKE) -C $(KDIR) M=$(PWD) O=$(BUILD_DIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) O=$(BUILD_DIR) clean

modules:
	$(MAKE) -C $(KDIR) M=$(PWD) O=$(BUILD_DIR) modules
