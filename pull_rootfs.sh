#!/bin/bash
# 把设备上的 Debian rootfs 拉回主机：<repo>/poc-rootfs
set -e
DEST=/home/indusy/Workspace/re-piano/poc-rootfs
STATE=$(adb devices | sed -n "2p" | tr -s " \t" " ")
echo "device: $STATE"
# recovery 里 tar 整棵 /data/debian2 最简单：里面的 proc/sys/dev 都是空目录
echo "== packing on device"
adb shell "cd /data && rm -f poc-rootfs.tar && tar -cf poc-rootfs.tar debian2 && ls -l poc-rootfs.tar"
echo "== pulling"
adb pull /data/poc-rootfs.tar /tmp/poc-rootfs.tar
ls -l /tmp/poc-rootfs.tar
echo "== extracting to $DEST"
mkdir -p "$DEST"
tar -C "$DEST" --strip-components=1 -xf /tmp/poc-rootfs.tar
du -sh "$DEST"
echo DONE
