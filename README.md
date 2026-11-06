# FBE — 从 ramdisk 解开 userdata 的最小组件集

> **通用 / 不通用 的分界与抓取脚本见 [PORTABILITY.md](PORTABILITY.md)。**
> 一句话：代码是通用的（`src/`、`fbe-run.sh`、`conf/fbe.conf.example`），
> 密钥材料 / 厂商模块 / 分区布局是设备绑定的，用 `pull/pull_all.sh <设备名>` 抓。


这套东西只做一件事：在不启动 Android 的前提下，用 TEE 里的 keymaster 把 userdata
的 wrapped key 解出来，建好 dm-default-key 映射，让内核能读 `/data`。
**全程只读密钥与元数据，绝不写入/销毁 userdata。**

## 目录

> 只有 `src/`、`pull/`、`conf/*.example`、`fbe-run.sh`、`build*.sh`、
> `devices/` 与 `bin/` 全部走 `.gitignore`，用抓取脚本重建。详见 [PORTABILITY.md](PORTABILITY.md)。

    src/kmkey.c                与 keymaster TA 对话（QSEE/SMCI ioctl）取出存储密钥
    src/dmcreate.c             用取到的 key 建 dm-default-key 映射
    src/init_stage7raw24.c     ramdisk init：跑完整条链后 chroot 进 Debian
    keys/keymaster_key_blob.bin  keymaster 输出的 wrapped blob（原始材料）
    keys/encrypted_key.bin       TA unwrap 之后的 encrypted key
    keys/keymaster_a.bin         keymaster_a 分区原始副本
    bin/                         构建产物（kmkey / dmcreate，gitignore）
    devices/<设备名>/            设备绑定材料：keys/ libs/ modules/ docs/（gitignore）

## 数据流

    keymaster_key_blob (214B)
        -> keymaster TA unwrap           (kmkey, SMCI op 走 /dev/qseecom)
    encrypted_key (282B)
        -> 取内层 KMKD (254B)
        -> convertStorageKeyToEphemeral
    100B 存储密钥
        -> dm-default-key 表：
           aes-xts-plain64 <200 hex> 0 <data 设备> 0 3 sector_size:4096 iv_large_sectors wrappedkey_v0
        -> DM_TABLE_LOAD(9) / DM_DEV_CREATE(3) / SUSPEND(6) / RESUME
    /dev/dm-N -> mount -> /data 可读

## 关键 ioctl

- keymaster 通道：`_IOWR(0x98, 1, struct smci_req)`（kmkey.c 里的 `INVOKE`）
- device-mapper：CREATE=3、REMOVE=4、SUSPEND=6、TABLE_LOAD=9（dmcreate.c）
- 内核 aarch64 系统调用号按实测：pwrite64=68、syslog=116、mount=40、unshare=97

## 构建

    ./build.sh          # 需要 clang（可 --target=aarch64-linux-gnu）

产物：`bin/kmkey`、`bin/dmcreate`。ramdisk init 的构建方式见
`src/init_stage7raw24.c` 顶部注释与 `build.sh` 末尾。

## 使用顺序（与 init 中一致）

1. 载入 TEE/存储所需模块（tmecom-intf、qseecom_proxy、si_core_module、mem_object 等）
2. `kmkey` → 得到 100B 存储密钥（写进临时文件，权限 600）
3. `dmcreate` → 建 dm 设备（默认 key 表 + wrappedkey_v0）
4. `mount /dev/dm-N /data`（只读或读写都行；init 里是正常挂载）
5. 之后才 chroot 进 Debian，`/data` 就是普通目录

## 已知坑

- 不能双挂 userdata（Android 侧的自制映射 + 这里的映射同时存在会把写入回滚）
- dm 表 load 后必须 resume 才生效；suspend 前不要动设备
- keymaster TA 要求 `tee-supplicant` 已在跑（Debian 侧有 `tee-supplicant.service`）
- key 材料只存在于内存/`/run`，不要落盘到可被同步的位置

## 出处

细节推导见 `stage7/reinit/FINDINGS.md`（§62 TEE 链、§65 firmware、§66 显示）。

## 移植到另一台设备

设计上只有三处与设备相关，全部在 `conf/fbe.conf` 里：

| 变量 | 作用 | piano 上的值 |
|---|---|---|
| `FBE_SMCI_DEV` / `FBE_TA_NAME` / `FBE_TA_SVC` | 找 keymaster TA 的三个坐标 | `/dev/smcinvoke`、`keymaster64`、`122` |
| `FBE_USERDATA` | userdata 块设备（留空自动探测） | 空 -> 扫 PARTNAME=userdata |
| `FBE_DM_TARGET` / `FBE_DM_PARAMS` / `FBE_SECTOR_SIZE` | 建哪种 dm 表 | `dm-default-key`、`sector_size:4096 iv_large_sectors wrappedkey_v0`、4096 |

另外 `FBE_MODULES` 是需要预载的内核模块（TEE/存储相关），每台机器不同，没有就留空。

### 移植步骤

1. **确认内核支持**

       zcat /proc/config.gz | grep -E "DM_DEFAULT_KEY|DM_CRYPT|BLK_INLINE_ENCRYPTION"

   - 有 `DM_DEFAULT_KEY` + `BLK_INLINE_ENCRYPTION` -> 用 `dm-default-key`（本文默认）
   - 老设备只有 `DM_CRYPT` -> 把 `FBE_DM_TARGET=dm-crypt`，
     `FBE_DM_PARAMS` 改成 `aes-xts-plain64 <key> 0 <dev> 0 sector_size:4096 iv_large_sectors`
     并在 mount 前确认内核支持 `inlinecrypt`（否则读出来是密文）

2. **拿到 key 材料**（这一步必须在目标设备上做，密钥不可跨设备搬运）

   - 需要 keymaster 输出的 `encrypted_key`（282B）或它去掉 28 字节头后的内层 KMKD（254B）
   - 放到 `FBE_BLOB` 指定的路径，权限 600
   - 如果手上只有 214B 的 `keymaster_key_blob`，那还要先做一次 TA unwrap；
     本工具集只实现了后一步 `convertStorageKeyToEphemeral`，会在运行时明确提示

3. **改 profile 并空跑**

       cp conf/fbe.conf.example conf/fbe.conf   # 按上表改
       DRY_RUN=1 ./fbe-run.sh conf/fbe.conf     # 只打印命令，不执行

4. **实跑**

       ./fbe-run.sh conf/fbe.conf
       # 判据：/dev/mapper/fbe 存在，能挂到 /data，dmesg 没有 dm 报错

### 自检命令

    ./bin/dmcreate -t fbe        # 打印已加载的表（含原始字节，排错用）
    ./bin/dmcreate -s fbe        # 设备状态
    ./bin/dmcreate -r fbe        # 删除映射（不影响数据）
    ./bin/kmkey -h               # 参数说明

### 已验证的组合

- 设备：Xiaomi Pad 8 Pro（piano，SM8750 / sun）
- 内核：6.6.118-android15-8（Android 15 GKI + 厂商模块）
- 方案：`dm-default-key` + `wrappedkey_v0`，keymaster64 服务号 122，`/dev/smcinvoke`
- 结果：不启动 Android，直接从 ramdisk 解开 userdata 并 chroot 进 Debian

### 这个工具集不做什么

- 不写、不格式化、不销毁 userdata；只读密钥材料并建只读/读写映射
- 不修改 keymaster 分区、不动 metadata 里的加密元数据
- 不搬运密钥到别的设备（TA 绑定设备，搬过去也解不开）
