#!/usr/bin/env bash
set -e -u

#export FAIL_EXPERIMENT=idle-test
#fail-client -q -f bochs.input

# ../qemu/build/qemu-system-arm -S -s -M virt -m 104 -kernel tree/images/zImage -monitor stdio -parallel none -icount shift=0,sleep=off -vga none
../qemu/build/qemu-system-arm -S -s -M virt -m 104 -kernel tree/images/zImage -monitor stdio -parallel none -icount shift=0,sleep=off -vga none -chardev timesync,id=ts0,path=timesync-test.sock -serial chardev:ts0 -serial vc
