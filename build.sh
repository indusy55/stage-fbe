#!/bin/sh
# 构建 FBE 的两个小程序。
#
# 说明：这台机器的 clang 没有 aarch64 sysroot，不能直接交叉编译带 glibc 的代码。
# 以前这些程序是在**设备上的 Debian 里用 gcc 直接编**的（二进制里能看到
# "GCC: (Debian 16.2.0-4)"）。所以这里有两条路：
#
#   A) 设备上编（默认、最省事）：./build_on_device.sh
#   B) 有 aarch64 工具链时：CC=aarch64-linux-gnu-gcc ./build.sh
#
set -e
cd "$(dirname "$0")"
mkdir -p bin

CC=${CC:-}
if [ -z "$CC" ]; then
  for c in aarch64-linux-gnu-gcc aarch64-linux-gnu-clang; do
    command -v "$c" >/dev/null 2>&1 && { CC=$c; break; }
  done
fi
if [ -z "$CC" ]; then
  echo "没有 aarch64 交叉编译器。请在设备上编： ./build_on_device.sh"
  exit 1
fi

CFLAGS=${CFLAGS:--O2 -static}
echo "[kmkey]    $CC"
$CC $CFLAGS -o bin/kmkey src/kmkey.c
echo "[dmcreate] $CC"
$CC $CFLAGS -o bin/dmcreate src/dmcreate.c
ls -l bin/
