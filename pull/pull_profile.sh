#!/bin/sh
# pull_profile.sh <设备名> — 生成 devices/<设备名>/fbe.conf（只探测，不改设备）
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
NAME=${1:-$(adb shell getprop ro.serialno 2>/dev/null | tr -d "\r")}
[ -n "$NAME" ] || NAME=device
OUT="$ROOT/devices/$NAME"
mkdir -p "$OUT"
echo "[profile] 设备名 $NAME -> $OUT"
"$HERE/probe.sh" > "$OUT/probe.txt" 2>&1
echo "[profile] probe -> $OUT/probe.txt"

SMCI=$(grep -o "/dev/smcinvoke" "$OUT/probe.txt" | head -1)
[ -n "$SMCI" ] || SMCI=/dev/smcinvoke
TA=$(grep -oE "keymaster[0-9]*|keymint[0-9]*" "$OUT/probe.txt" | sort | uniq -c | sort -rn | head -1 | awk "{print \$2}")
[ -n "$TA" ] || TA=keymaster64
MODDIR=$(grep -oE "/[a-z_/]*lib/modules/[0-9][^ ]*" "$OUT/probe.txt" | head -1)
[ -n "$MODDIR" ] || MODDIR=/usr/lib/modules/$(adb shell uname -r | tr -d "\r")

if grep -q "CONFIG_DM_DEFAULT_KEY=y" "$OUT/probe.txt"; then
  TGT=dm-default-key; PARAMS="sector_size:4096 iv_large_sectors wrappedkey_v0"
else
  TGT=dm-crypt; PARAMS="sector_size:4096 iv_large_sectors"
fi

USERDATA=$(grep -E "userdata" "$OUT/probe.txt" | awk "{print \$1}" | head -1)

MODS=""
for m in tmecom-intf qseecom_proxy si_core_module mem_object; do
  grep -q "/$m.ko" "$OUT/probe.txt" && MODS="$MODS $m"
done

cp "$ROOT/conf/fbe.conf.example" "$OUT/fbe.conf"
sed -i "s#^FBE_SMCI_DEV=.*#FBE_SMCI_DEV=$SMCI#; \
        s#^FBE_TA_NAME=.*#FBE_TA_NAME=$TA#; \
        s#^FBE_USERDATA=.*#FBE_USERDATA=$USERDATA#; \
        s#^FBE_DM_TARGET=.*#FBE_DM_TARGET=$TGT#; \
        s#^FBE_DM_PARAMS=.*#FBE_DM_PARAMS=$PARAMS#; \
        s#^FBE_MODULES=.*#FBE_MODULES=\"$MODS\"#; \
        s#^FBE_MODDIR=.*#FBE_MODDIR=$MODDIR#; \
        s#^FBE_BLOB=.*#FBE_BLOB=$OUT/keys/encrypted_key#; \
        s#^FBE_KEY_OUT=.*#FBE_KEY_OUT=/run/fbe/storage.key#; \
        s#^FBE_KEY_HEX=.*#FBE_KEY_HEX=/run/fbe/storage.key.hex#" "$OUT/fbe.conf"
echo "[profile] 写好 $OUT/fbe.conf："
grep -E "^FBE_(SMCI_DEV|TA_NAME|USERDATA|DM_TARGET|MODULES|MODDIR|BLOB)=" "$OUT/fbe.conf" | sed "s/^/    /"
