#!/bin/sh
# pull_all.sh <设备名> — 一次抓齐所有「不通用」部分
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
NAME=${1:?用法: pull_all.sh <设备名>}
echo "== 1/4 fstab";   "$HERE/pull_fstab.sh" "$NAME"
echo "== 2/4 modules"; "$HERE/pull_modules.sh" "$NAME"
echo "== 3/4 key";     "$HERE/pull_key_material.sh" "$NAME"
echo "== 4/4 profile"; "$HERE/pull_profile.sh" "$NAME"
echo
echo "完成：devices/$NAME/ —— 拷到目标机后用 fbe-run.sh devices/$NAME/fbe.conf 运行"
