#!/bin/sh
# pull_modules.sh <设备名> — 抓厂商 TEE 模块与 libkeymaster*.so（设备绑定）
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
NAME=${1:?用法: pull_modules.sh <设备名>}
OUTM="$ROOT/devices/$NAME/modules"; OUTL="$ROOT/devices/$NAME/libs"
mkdir -p "$OUTM" "$OUTL"

MODS=${FBE_MODULES:-"tmecom-intf qseecom_proxy si_core_module mem_object qcom_iommu_debug"}
DIRS="/vendor/lib/modules /vendor_dlkm/lib/modules /odm/lib/modules /odm_dlkm/lib/modules /system_dlkm/lib/modules"
KREL=$(adb shell uname -r | tr -d "\r")
DIRS="$DIRS /usr/lib/modules/$KREL /lib/modules/$KREL"

got=0
for m in $MODS; do
  for d in $DIRS; do
    p="$d/$m.ko"
    if adb shell "[ -f $p ]" 2>/dev/null; then
      adb shell "cat $p" > "$OUTM/$m.ko" 2>/dev/null || continue
      echo "[mod] $p -> $OUTM/$m.ko ($(wc -c < "$OUTM/$m.ko") 字节)"
      got=$((got+1)); break
    fi
  done
done
[ "$got" -gt 0 ] || echo "[mod] 一个都没抓到：确认设备已 root 且模块目录可见"

for d in /vendor/lib64 /system/lib64 /odm/lib64 /usr/lib/aarch64-linux-gnu; do
  for f in libkeymasterdeviceutils.so libkeymaster_messages.so libkeymasterutils.so libkeymaster4.so; do
    adb shell "[ -f $d/$f ]" 2>/dev/null || continue
    adb shell "cat $d/$f" > "$OUTL/$f" 2>/dev/null || continue
    echo "[lib] $d/$f -> $OUTL/$f ($(wc -c < "$OUTL/$f") 字节)"
  done
done
