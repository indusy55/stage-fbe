# 部署到 ramdisk

把这一套装进 initramfs，让**内核一起来就把 userdata 解开**，然后才切进真正的 rootfs。

## 1. ramdisk 里长什么样

以本仓库验证过的机器（piano，init_boot 分区，8 MB）为例：

    (cpio 根)
    ├── init                    我们自己编的静态 init（替换掉原厂 /init）
    ├── init.ksu                原厂 init 备份（改名留存，便于对照）
    ├── lib/modules/            扁平目录，不是 lib/modules/<krel>/
    │   ├── qcom_scm.ko  ufs_qcom.ko  phy-qcom-ufs*.ko  ...   存储链
    │   ├── tmecom-intf.ko  qseecom_proxy.ko
    │   ├── si_core_module.ko  mem_object.ko  smcinvoke_dlkm.ko   TEE 链
    │   └── ...（约 158 个，够用就行，别的不要塞）
    ├── kmkey                   本仓库编出的静态二进制
    ├── dmcreate                同上
    └── inner.bin               254B 内层 KMKD（**设备绑定，逐台打包**）

两条硬性细节：

- **模块目录是扁平的** `/lib/modules/<名字>.ko`。init 里的加载顺序依赖这一点，
  找不到时它会退化成 `/<名字>.ko`（所以把 .ko 直接丢根目录也能跑）。
- **key 材料必须随镜像走**。`/metadata` 自己也是加密的（metadata encryption），
  在 userdata 解开之前读不到里面的 `encrypted_key`；能从裸分区读的只有 keymaster 分区，
  但那只有 214B 的 wrapped blob，还得再经一次 TA unwrap。所以最省事的形态就是
  构建时把每台机器的 254B `inner.bin` 打进镜像。

## 2. 打包流程（本仓库验证过的顺序）

    # a) 取原厂镜像与它的盐（AVB 要沿用，否则 bootloader 拒绝）
    avbtool info_image --image init_boot_a.img     # 记下 Salt 和 partition_size

    # b) 解开原厂 ramdisk（lz4 包着的 cpio）
    python3 - <<PY
    import struct
    d = open("init_boot_a.img","rb").read()
    ks, rs = struct.unpack_from("<II", d, 8)
    open("orig.lz4","wb").write(d[4096:4096+rs])
    PY
    lz4 -d -f orig.lz4 orig.cpio && mkdir tree && (cd tree && cpio -id < ../orig.cpio)

    # c) 换 init、塞 extras
    mv tree/init tree/init.ksu
    cp init kmkey dmcreate inner.bin tree/
    mkdir -p tree/lib/modules && cp <需要的>.ko tree/lib/modules/

    # d) 重新打成 cpio + lz4
    python3 mkcpio.py tree new.cpio && lz4 -l -f new.cpio new.lz4

    # e) 封成 boot image，并沿用原盐补 AVB hash footer
    mkbootimg --header_version 4 --pagesize 4096 --ramdisk new.lz4 --output probe.img
    avbtool add_hash_footer --image probe.img --partition_name init_boot \
            --partition_size <原值> --hash_algorithm sha256 --salt <原盐>

    # f) 写进去（A/B 机器注意槽位）
    adb push probe.img /tmp/ && adb shell "dd if=/tmp/probe.img of=/dev/block/by-name/init_boot_a bs=1M"

## 3. init 里要按什么顺序做

    1. 建 devtmpfs，打开日志与控制台
    2. 读 BCB（misc）：如果是 boot-recovery 就交回原厂流程
    3. load_set(modlist, passes=2)    # 依赖是两轮才收敛的
         -> 模块进 /lib/modules/*.ko，其中包含 TEE 那几个
    4. kmkey  <svc> <TA名>  /inner.bin  /key.bin      # 100 字节
    5. 把 /key.bin 转成 200 字符 hex
    6. dmcreate userdata 0 <扇区数> default-key \
           aes-xts-plain64 <hex> 0 /dev/<userdata> 0 3 \
           sector_size:4096 iv_large_sectors wrappedkey_v0
    7. mount /dev/mapper/userdata /newroot f2fs
    8. bind <rootfs>/lib/firmware /lib/firmware      # 内核固件加载器只看 initramfs 根
    9. unshare(CLONE_NEWPID) + fork，子进程 unshare(CLONE_NEWNS)、挂 proc、
       MS_MOVE 到 /、chroot(.)、exec /sbin/init
   10. 父进程留作监督：USB 通了就常驻，否则写 BCB 回原厂

第 4~6 步就是本仓库 `kmkey` / `dmcreate` 的职责，参数换成 `fbe-run.sh` 里那一套即可。

## 4. 换设备要改什么

| 项 | 怎么定 |
|---|---|
| userdata 块设备 | `pull/pull_fstab.sh` 抓分区表，或运行 `pull/pull_profile.sh` |
| 扇区数 | `BLKGETSIZE64 / 4096`，`dmcreate -k` 会自动算 |
| 目标类型 | 内核有 `DM_DEFAULT_KEY` 就用 `dm-default-key`，否则 `dm-crypt` |
| 需要的 .ko 清单 | 从正常 Android 的 `/proc/modules` 取，只保留存储 + TEE 链；`pull/pull_modules.sh` 抓文件 |
| TA 名 / 服务号 / smci 节点 | `pull/probe.sh` 输出里看 `dmesg | grep keymaster` 与 `/dev/smcinvoke` |
| 打包参数 | 每台机器的 `init_boot`/`vendor_boot` 分区大小与 AVB 盐不同，务必先 `avbtool info_image` |

## 5. 大小与坑

- 分区就是 8 MB（本机），原厂 ramdisk + 158 个 .ko 已经很紧；**别把显示/GPU 那一堆塞进来**，
  那些等切进 rootfs 再载。
- 两个槽位（`init_boot_a` / `init_boot_b`）各写一份，或确认只从当前槽启动。
- AVB 不通过就直接黑屏重启，`avbtool add_hash_footer` 必须在 `mkbootimg` 之后、用原盐。
- init 是 `-nostdlib -static` 编的，不要引入 libc；`kmkey`/`dmcreate` 如果动态链接，
  必须把 loader 和依赖一起放进 ramdisk（本仓库默认静态编）。
- 调试口：把 dmesg 快照写到未加密分区（本机写到 `/newroot/stage7-kmsg-*.log`），
  否则出问题只能靠 BCB 回原厂再看。

## 6. 安全

镜像里有 key 材料，所以：

- **逐台构建**，不要把 A 机器的 `inner.bin` 给 B 机器（解不开，还白泄露一份材料）
- 镜像不要外发、不要传网盘；本仓库的 `.gitignore` 只保护仓库，管不了你手上的 img
- 需要重装时先 `pull/pull_key_material.sh` 重新抓一份，用完即删
