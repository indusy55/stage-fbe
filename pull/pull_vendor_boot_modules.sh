#!/bin/sh
# pull_vendor_boot_modules.sh <设备名> — 从 vendor_boot 分区的 vendor ramdisk 里取 /lib/modules
#
# 背景（piano 实测）：我们自己打的 init_boot ramdisk 里根本没有 /lib/modules，
# 但启动后 init 能在 /lib/modules/*.ko 找到模块——因为 bootloader 会把
# vendor_boot 的 vendor ramdisk 片段合并进 first-stage initramfs。
# vendor_boot v4 的 vendor ramdisk 是 legacy-LZ4（magic 02 21 4c 18）包着的 cpio，
# 解出来约 88 MB，lib/modules 下有 449 个 .ko（含 tmecom-intf / qseecom_proxy /
# si_core_module / mem_object 这四个 TEE 模块）。
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
NAME=${1:?用法: pull_vendor_boot_modules.sh <设备名>}
SLOT=${2:-a}
OUT="$ROOT/devices/$NAME/modules"
TMP=$(mktemp -d)
mkdir -p "$OUT"

DEV=$(adb shell "for x in /sys/class/block/*; do n=\$(grep -m1 ^PARTNAME= \$x/uevent 2>/dev/null | cut -d= -f2); [ \"\$n\" = vendor_boot_$SLOT ] && echo /dev/\$(basename \$x); done" | tr -d "\r" | head -1)
[ -n "$DEV" ] || { echo "[vbm] 找不到 vendor_boot_$SLOT"; exit 1; }
echo "[vbm] 分区 $DEV"

adb shell "dd if=$DEV of=/data/local/tmp/vb.img bs=1M 2>/dev/null; ls -l /data/local/tmp/vb.img"
adb pull /data/local/tmp/vb.img "$TMP/vb.img" >/dev/null 2>&1 || { echo "[vbm] pull 失败"; exit 1; }
adb shell "rm -f /data/local/tmp/vb.img" >/dev/null 2>&1 || true

python3 - "$TMP" <<"PY"
import struct, subprocess, sys, os
t = sys.argv[1]
d = open(os.path.join(t, "vb.img"), "rb").read()
assert d[:8] == b"VNDRBOOT", "不是 vendor_boot 镜像"
hv, ps = struct.unpack_from("<II", d, 8)
vrsize, = struct.unpack_from("<I", d, 0x18)
print(f"[vbm] header_version={hv} page={ps} vendor_ramdisk_size={vrsize}")
blob = d[ps:ps+vrsize]
open(os.path.join(t, "vr.lz4"), "wb").write(blob)
r = subprocess.run(["lz4", "-d", "-f", os.path.join(t, "vr.lz4"), os.path.join(t, "vr.cpio")],
                   capture_output=True)
if r.returncode != 0:
    print("[vbm] lz4 解压失败（可能是多片段或非 legacy-LZ4）:", r.stderr.decode()[:200])
    sys.exit(2)
print("[vbm] cpio:", os.path.getsize(os.path.join(t, "vr.cpio")), "字节")
PY

mkdir -p "$TMP/tree" && (cd "$TMP/tree" && cpio -id --quiet < "$TMP/vr.cpio" 2>/dev/null)
if [ -d "$TMP/tree/lib/modules" ]; then
  n=0
  for f in "$TMP/tree/lib/modules/"*.ko; do
    [ -f "$f" ] || continue
    cp -f "$f" "$OUT/$(basename "$f")"; n=$((n+1))
  done
  echo "[vbm] 取出 $n 个 .ko -> $OUT"
  for m in tmecom-intf qseecom_proxy si_core_module mem_object; do
    [ -f "$OUT/$m.ko" ] && echo "[vbm]   有 $m.ko" || echo "[vbm]   缺 $m.ko"
  done
else
  echo "[vbm] vendor ramdisk 里没有 lib/modules"; exit 1
fi
rm -rf "$TMP"
