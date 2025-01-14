#!/usr/bin/env sh

mkdir ../grub/grub-core/remote_bootselect/
cp remote_bootselect.c ../grub/grub-core/remote_bootselect/remote_bootselect.c
cd ../grub/
make -j
sudo cp grub-core/remote_bootselect.mod /boot/grub/x86_64-efi/remote_bootselect.mod
