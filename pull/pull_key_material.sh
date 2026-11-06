#!/bin/sh
# pull_key_material.sh <设备名> — 抓 keymaster 的 key 材料（设备绑定，必须逐台抓）
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
NAME=${1:?用法: pull_key_material.sh <设备名>}
OUT="$ROOT/devices/$NAME/keys"
mkdir -p "$OUT"
grab() {   # grab <设备路径> <本地名>
  p=$1; bn=$2
  if adb shell "[ -f $p ]" 2>/dev/null; then
    adb shell "cat $p" > "$OUT/$bn" 2>/dev/null || return 1
    s=$(wc -c < "$OUT/$bn")
    [ "$s" -gt 0 ] && { echo "[key] $p -> $OUT/$bn ($s 字节)"; return 0; }
    rm -f "$OUT/$bn"
  fi
  return 1
}

echo "[key] 1) 常见文件路径"
found=0
for p in \
  /metadata/vold/metadata_encryption/keymaster_key_blob \
  /metadata/vold/metadata_encryption/key/keymaster_key_blob \
  /metadata/vold/metadata_encryption/key_back/keymaster_key_blob \
  /data/misc/vold/keymaster_key_blob \
  /data/vendor/vold/metadata_encryption/keymaster_key_blob \
  /mnt/vendor/metadata/vold/metadata_encryption/keymaster_key_blob \
  /mnt/vendor/metadata/vold/metadata_encryption/key/keymaster_key_blob \
  /mnt/vendor/persist/keymaster/keymaster_key_blob ; do
  grab "$p" "$(basename "$p")" && found=1
done

echo "[key] 2) 扫 metadata_encryption 下的目录（key 里拿到就不再看 key_back）"
for d in /metadata/vold/metadata_encryption/key /metadata/vold/metadata_encryption/key_back \
         /mnt/vendor/metadata/vold/metadata_encryption/key /mnt/vendor/metadata/vold/metadata_encryption/key_back; do
  for f in $(adb shell "ls $d 2>/dev/null" | tr -d "\r"); do
    case "$f" in *keymaster*|*key_blob*|*encrypted*)
      grab "$d/$f" "$(basename "$f")" && found=1 ;;
    esac
  done
done

if [ "$found" = 0 ]; then
  echo "[key] 3) 都没找到，改抓 keymaster 分区前 512K 原件"
  DEV=$(adb shell "for x in /sys/class/block/*; do n=\$(grep -m1 ^PARTNAME= \$x/uevent 2>/dev/null | cut -d= -f2); [ \"\$n\" = keymaster_a ] && echo /dev/\$(basename \$x); done" | tr -d "\r" | head -1)
  if [ -n "$DEV" ]; then
    adb shell "dd if=$DEV of=/data/local/tmp/km_a.bin bs=4096 count=128 2>/dev/null; ls -l /data/local/tmp/km_a.bin"
    adb pull /data/local/tmp/km_a.bin "$OUT/keymaster_a.bin" >/dev/null 2>&1 || true
    adb shell "rm -f /data/local/tmp/km_a.bin" >/dev/null 2>&1 || true
    [ -s "$OUT/keymaster_a.bin" ] && echo "[key] $DEV -> $OUT/keymaster_a.bin"
  fi
fi

echo "[key] 尺寸判定："
for f in "$OUT"/*; do
  [ -f "$f" ] || continue
  s=$(wc -c < "$f")
  case "$s" in
    254) t="inner KMKD -> 可直接用（FBE_BLOB 指向它）" ;;
    282) t="encrypted_key -> 可直接用（自动跳过 28 字节头）" ;;
    214) t="keymaster wrapped blob -> 还需一次 TA unwrap（本工具集不含）" ;;
    *)   t="未知尺寸（分区原件不是 key，只是留存）" ;;
  esac
  echo "    $(basename "$f")  $s  $t"
done
