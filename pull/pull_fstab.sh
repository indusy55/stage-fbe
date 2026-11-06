#!/bin/sh
# pull_fstab.sh <设备名> — 抓 fstab 与分区信息（布局每台不同）
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
NAME=${1:?用法: pull_fstab.sh <设备名>}
OUT="$ROOT/devices/$NAME/docs"
mkdir -p "$OUT"

for p in /vendor/etc/fstab.qcom /vendor/etc/fstab.default /odm/etc/fstab.qcom /first_stage_ramdisk/fstab.qcom /first_stage_ramdisk/fstab.default; do
  if adb shell "[ -f $p ]" 2>/dev/null; then
    adb shell "cat $p" > "$OUT/$(basename "$p").txt" 2>/dev/null || continue
    echo "[fstab] $p -> $OUT/$(basename "$p").txt"
  fi
done

adb shell "for d in /sys/class/block/*; do n=\$(grep -m1 ^PARTNAME= \$d/uevent 2>/dev/null | cut -d= -f2); [ -n \"\$n\" ] && echo \"/dev/\$(basename \$d) \$n \$(cat \$d/size)\" ; done" > "$OUT/partitions.txt" 2>/dev/null || true
echo "[fstab] 分区表 -> $OUT/partitions.txt"

adb shell "cat /proc/cmdline" > "$OUT/cmdline.txt" 2>/dev/null || true
adb shell "getprop ro.boot.slot_suffix 2>/dev/null; getprop ro.product.device 2>/dev/null; getprop ro.board.platform 2>/dev/null" > "$OUT/props.txt" 2>/dev/null || true
echo "[fstab] cmdline/props 已记录"
