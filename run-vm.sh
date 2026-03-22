IMG="/var/lib/libvirt/images/debian13.qcow2"
MOD="container-build/remote_bootselect.mod"

# ensure nbd0 is disconnected
sudo qemu-nbd --disconnect /dev/nbd0 && \
sudo umount -l /dev/nbd0p2
sudo modprobe -r nbd
sudo modprobe nbd max_part=8 && \
sudo qemu-nbd --connect=/dev/nbd0 "$IMG" && \
sleep 0.5 && \
sudo mount /dev/nbd0p2 mnt && \
sudo cp "$MOD" mnt/boot/grub/x86_64-efi/ && \
sudo umount mnt && \
sudo qemu-nbd --disconnect /dev/nbd0 && \
sleep 0.5 && \
sudo qemu-system-x86_64 \
  -machine q35 \
  -m 1024 \
  -drive if=pflash,format=raw,readonly=on,file=/home/bobby/Downloads/RELEASEX64_OVMF.fd \
  -drive file="$IMG",format=qcow2 \
  -netdev tap,id=n1,ifname=tap0,script=no,downscript=no \
  -device e1000,netdev=n1
