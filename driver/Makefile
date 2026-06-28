# Build the audient_id companion kernel module.
#
# CachyOS (and other distros) build their kernels with Clang/LLVM. If the target
# kernel was built with Clang, this Makefile automatically passes LLVM=1 to
# kbuild, so a plain `make` (and `sudo make install`) just works. You can still
# force it explicitly with `make LLVM=1`, or disable with `make LLVM=0`.
obj-m += audient_id.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

# Auto-detect a Clang-built kernel unless the caller set LLVM explicitly.
ifndef LLVM
  ifeq ($(shell grep -qs '^CONFIG_CC_IS_CLANG=y' $(KDIR)/include/config/auto.conf && echo 1),1)
    LLVM := 1
    export LLVM
  endif
endif

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

# Quick local install (without DKMS): copies the built module into the running
# kernel's extra/ dir and refreshes module dependencies + udev.
install: all
	sudo install -Dm644 audient_id.ko /lib/modules/$(shell uname -r)/extra/audient_id.ko
	sudo depmod -a
	sudo install -Dm644 99-audient-id.rules /etc/udev/rules.d/99-audient-id.rules
	sudo udevadm control --reload-rules
	@echo "Installed. Load now with:  sudo modprobe audient_id   (or replug the device)"

uninstall:
	sudo rm -f /lib/modules/$(shell uname -r)/extra/audient_id.ko
	sudo rm -f /etc/udev/rules.d/99-audient-id.rules
	sudo depmod -a
	@echo "Removed. Unload the running module with:  sudo rmmod audient_id"

.PHONY: all clean install uninstall
