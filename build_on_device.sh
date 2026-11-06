#!/bin/sh
# 在设备上的 Debian 里编译 FBE 组件（设备有 gcc 15/16 与 glibc 头）。
# 前置：设备已经启动到 Debian，且 adb 可见（piano00000001）。
set -e
cd "$(dirname "$0")"

adb shell "mkdir -p /root/fbe/src"
adb push src/kmkey.c src/dmcreate.c /root/fbe/src/ >/dev/null
adb shell "cd /root/fbe/src && gcc -O2 -static -o /root/fbe/kmkey kmkey.c && gcc -O2 -static -o /root/fbe/dmcreate dmcreate.c && ls -l /root/fbe"
mkdir -p bin
adb pull /root/fbe/kmkey bin/kmkey >/dev/null
adb pull /root/fbe/dmcreate bin/dmcreate >/dev/null
ls -l bin/
