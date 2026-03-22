.PHONY: all build-dir clean install configure-grub bootstrap-grub oneshot

all: build/remote_bootselect.mod

build-dir:
	mkdir -p build/

clean:
	rm -rf build/

bootstrap-grub:
	cd grub; echo "module = {name = remote_bootselect;common = remote_bootselect/remote_bootselect.c;};" >> grub-core/Makefile.core.def; ./bootstrap;

configure-grub:
	cd grub; ./configure --build=x86_64 --with-platform=efi;

build/remote_bootselect.mod: build-dir
	mkdir -p grub/grub-core/remote_bootselect/
	cp src/grub/remote_bootselect.c grub/grub-core/remote_bootselect/
	cd grub; make -j;
	cp grub/grub-core/remote_bootselect.mod build/remote_bootselect.mod

install:
	sudo cp build/remote_bootselect.mod /boot/grub/x86_64-efi/remote_bootselect.mod
	sudo cp src/grub/01_remote_bootselect /etc/grub.d/01_remote_bootselect || echo "/etc/grub.d/ not found. If you are on nixos, add the contents of src/grub/01-remote_bootselect to boot.loader.grub.extraConfig"

oneshot: bootstrap-grub configure-grub all
