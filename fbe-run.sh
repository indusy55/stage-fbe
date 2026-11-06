#!/bin/sh
# fbe-run.sh — 按 profile 解开 userdata（只读密钥/元数据，不销毁数据）
#
#   fbe-run.sh [profile]     默认 /etc/fbe.conf，其次 conf/fbe.conf
#   DRY_RUN=1 fbe-run.sh     只打印将要执行的命令
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
CONF=${1:-}
[ -z "$CONF" ] && [ -f /etc/fbe.conf ] && CONF=/etc/fbe.conf
[ -z "$CONF" ] && [ -f "$HERE/conf/fbe.conf" ] && CONF=$HERE/conf/fbe.conf
[ -z "$CONF" ] && [ -f "$HERE/conf/fbe.conf.example" ] && CONF=$HERE/conf/fbe.conf.example
[ -n "$CONF" ] && . "$CONF"

: "${FBE_KMKEY:=$HERE/bin/kmkey}"
: "${FBE_DMCREATE:=$HERE/bin/dmcreate}"
: "${FBE_SMCI_DEV:=/dev/smcinvoke}"
: "${FBE_TA_NAME:=keymaster64}"
: "${FBE_TA_SVC:=122}"
: "${FBE_BLOB:=/run/fbe/encrypted_key.bin}"
: "${FBE_KEY_OUT:=/run/fbe/storage.key}"
: "${FBE_KEY_HEX:=/run/fbe/storage.key.hex}"
: "${FBE_USERDATA:=}"
: "${FBE_DM_NAME:=fbe}"
: "${FBE_DM_TARGET:=dm-default-key}"
: "${FBE_DM_PARAMS:=sector_size:4096 iv_large_sectors wrappedkey_v0}"
: "${FBE_SECTOR_SIZE:=4096}"
: "${FBE_MOUNT:=/data}"
: "${FBE_MOUNT_OPTS:=}"
: "${FBE_MODULES:=}"
: "${FBE_MODDIR:=/usr/lib/modules/$(uname -r)}"
: "${FBE_RUNDIR:=/run/fbe}"
export FBE_SMCI_DEV FBE_TA_NAME FBE_TA_SVC FBE_KEY_OUT FBE_KEY_HEX FBE_DM_TARGET FBE_DM_PARAMS

say() { echo "[fbe] $*"; }
run() { if [ "${DRY_RUN:-0}" = "1" ]; then echo "  + $*"; else "$@"; fi; }

say "profile ${CONF:-<none>}"

# 1) 预载模块
for m in $FBE_MODULES; do
  grep -q "^$m " /proc/modules 2>/dev/null && continue
  [ -f "$FBE_MODDIR/$m.ko" ] || { say "跳过模块 $m（无 .ko）"; continue; }
  run insmod "$FBE_MODDIR/$m.ko" || say "insmod $m 失败（继续）"
done

# 2) userdata 设备
DEV=$FBE_USERDATA
if [ -z "$DEV" ]; then
  for c in /dev/block/by-name/userdata /dev/block/mapper/userdata; do
    [ -e "$c" ] && { DEV=$c; break; }
  done
fi
if [ -z "$DEV" ]; then
  for d in /sys/class/block/*; do
    n=$(grep -m1 "^PARTNAME=" "$d/uevent" 2>/dev/null | cut -d= -f2)
    [ "$n" = "userdata" ] && { DEV=/dev/$(basename "$d"); break; }
  done
fi
[ -n "$DEV" ] || { say "找不到 userdata，请在 profile 里设 FBE_USERDATA"; exit 1; }
say "userdata = $DEV"

# 3) 运行目录
run mkdir -p "$FBE_RUNDIR"
run chmod 700 "$FBE_RUNDIR"

# 4) 取 key
[ -e "$FBE_BLOB" ] || { say "缺少 key 材料 $FBE_BLOB（见 README 获取步骤）"; exit 1; }
say "取存储密钥: kmkey -D $FBE_SMCI_DEV -T $FBE_TA_NAME -S $FBE_TA_SVC -b $FBE_BLOB"
run "$FBE_KMKEY" -D "$FBE_SMCI_DEV" -T "$FBE_TA_NAME" -S "$FBE_TA_SVC" -b "$FBE_BLOB" -o "$FBE_KEY_OUT" -x "$FBE_KEY_HEX" || { say "kmkey 失败"; exit 1; }
[ "${DRY_RUN:-0}" = "1" ] || { chmod 600 "$FBE_KEY_OUT" "$FBE_KEY_HEX"; say "key: $(wc -c < "$FBE_KEY_OUT") 字节"; }

# 5) 建 dm
say "建映射: $FBE_DM_NAME on $DEV"
run "$FBE_DMCREATE" -r "$FBE_DM_NAME" >/dev/null 2>&1 || true
run "$FBE_DMCREATE" -k "$FBE_DM_NAME" "$DEV" "$FBE_KEY_OUT" --target "$FBE_DM_TARGET" --sector-size "$FBE_SECTOR_SIZE" --params "$FBE_DM_PARAMS" || { say "dmcreate 失败"; exit 1; }

# 6) 挂载
NODE=/dev/mapper/$FBE_DM_NAME
if [ "${DRY_RUN:-0}" = "1" ]; then
  say "DRY_RUN 完成"
  exit 0
fi
[ -e "$NODE" ] || { say "dm 节点 $NODE 不存在"; exit 1; }
run mkdir -p "$FBE_MOUNT"
run mount $FBE_MOUNT_OPTS "$NODE" "$FBE_MOUNT" || { say "mount 失败"; exit 1; }
say "OK: $NODE -> $FBE_MOUNT"
