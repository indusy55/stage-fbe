#!/bin/sh
# probe.sh — 打印目标设备的 FBE 相关能力（不改任何东西）
# 用法: probe.sh [> probe.txt]
set -u
echo "== uname";      adb shell "uname -r; uname -m"
echo "== kernel.cfg"; adb shell "zcat /proc/config.gz 2>/dev/null | grep -E \"^(CONFIG_DM_DEFAULT_KEY|CONFIG_DM_CRYPT|CONFIG_BLK_INLINE_ENCRYPTION|CONFIG_DM_CRYPT=|# CONFIG_DM_CRYPT)\" || echo NO_CONFIG_GZ"
echo "== dm";         adb shell "ls -l /dev/mapper/control 2>&1; grep -c . /proc/modules; ls /sys/module | grep -E \"^dm_\" | tr \"\n\" \" \""
echo "== tee";        adb shell "ls -l /dev/smcinvoke /dev/qseecom 2>&1"
echo "== keymaster";  adb shell "dmesg 2>/dev/null | grep -iE \"keymaster|keymint\" | tail -6"
echo "== partitions"; adb shell "for d in /sys/class/block/*; do n=\$(grep -m1 \"^PARTNAME=\" \$d/uevent 2>/dev/null | cut -d= -f2); case \"\$n\" in *userdata*|*metadata*|*keymaster*|*misc*) echo \"/dev/\$(basename \$d) \$n\";; esac; done"
echo "== fstab";      adb shell "ls -l /vendor/etc/fstab* /odm/etc/fstab* /first_stage_ramdisk/fstab* 2>/dev/null"
echo "== moddirs";    adb shell "ls -d /vendor/lib/modules /vendor_dlkm/lib/modules /odm/lib/modules /usr/lib/modules/* 2>/dev/null"
echo "== tee modules";adb shell "for m in tmecom-intf qseecom_proxy si_core_module mem_object qcom_iommu_debug; do for d in /vendor/lib/modules /vendor_dlkm/lib/modules /odm/lib/modules /usr/lib/modules/*; do [ -f \"\$d/\$m.ko\" ] && echo \"\$d/\$m.ko\"; done; done"
echo "== tee libs";   adb shell "ls -l /vendor/lib64/libkeymaster*.so /system/lib64/libkeymaster*.so /usr/lib/aarch64-linux-gnu/libkeymaster*.so 2>/dev/null"
echo "== key files";  adb shell "ls -l /metadata/vold/metadata_encryption/ 2>/dev/null; echo \"-- key dir --\"; ls -l /metadata/vold/metadata_encryption/key 2>/dev/null; echo \"-- key_back --\"; ls -l /metadata/vold/metadata_encryption/key_back 2>/dev/null"
echo "== blockdevs";  adb shell "ls /dev/block/by-name 2>/dev/null | tr \"\n\" \" \"; echo; ls /dev/block/mapper 2>/dev/null | tr \"\n\" \" \" | head -c 400"
