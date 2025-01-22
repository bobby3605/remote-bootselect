.PHONY: all build-dir clean install configure-grub bootstrap-grub user

all: build/remote-bootselect-server build/remote-bootselect.mod

build-dir:
	mkdir -p build/

clean:
	rm -rf build/

build/remote-bootselect-server: build-dir
	gcc -O3 src/server/remote-bootselect-server.c src/server/util.c -o build/remote-bootselect-server

bootstrap-grub:
	cd grub; echo "module = {name = remote-bootselect;common = remote-bootselect/remote-bootselect.c;};" >> grub-core/Makefile.core.def; ./bootstrap;

configure-grub:
	cd grub; ./configure --build=x86_64 --with-platform=efi;

build/remote-bootselect.mod: build-dir
	mkdir -p grub/grub-core/remote-bootselect/
	cp src/grub/remote-bootselect.c grub/grub-core/remote-bootselect/
	cd grub; make -j;
	cp grub/grub-core/remote-bootselect.mod build/remote-bootselect.mod

user:
	sudo useradd -r remote-bootselect

install: user
	sudo cp build/remote-bootselect.mod /boot/grub/x86_64-efi/remote-bootselect.mod
	sudo cp src/grub/01-remote-bootselect /etc/grub.d/01-remote_bootselect || echo "/etc/grub.d/ not found. If you are on nixos, add the contents of src/grub/01-remote-bootselect to boot.loader.grub.extraConfig"
