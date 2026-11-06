# 全流程逐步说明

从按下电源到看到 KDE 桌面，一共 11 步。每步都按同一个格式写：
**是什么 / 它做什么 / 需要什么材料 / 成功的样子 / 失败会怎样 / 交给下一步什么**。

材料分两类：**[通]** = 跨设备通用（在仓库里），**[绑]** = 设备绑定（要用 `pull/` 抓）。

---

## 步骤 1：bootloader 把 ramdisk 铺成根文件系统

- **是什么**：bootloader（ABL）按 BCB/槽位选一个 boot 镜像，做 AVB 校验，把镜像里的
  ramdisk（lz4 包着的 cpio）解到内存，然后跳进内核，并把它自己知道的参数写成内核 cmdline。
- **它做什么**：提供"第一个根文件系统"。这时候还没有任何磁盘分区可用。
- **需要什么材料**：`init_boot_a` 分区里那份打过 AVB 的镜像 [绑：每台机器的盐/分区大小不同]。
  cmdline 里的 `msm_drm.dsi_display0=...`、`hwid.*`、`bootmonitor.*` 都是这一步带进来的。
- **成功的样子**：内核打印 `run /init as init process`，我们的 init 开始输出日志。
- **失败会怎样**：AVB 不过 → 直接黑屏重启（连日志都没有）。
- **交给下一步**：一个内存里的根文件系统（里面有我们的 `init`、`kmkey`、`dmcreate`、
  `inner.bin`、`/lib/modules/*.ko`）+ 一份内核 cmdline。

---

## 步骤 2：init 载入内核模块（存储链 + TEE 链）

- **是什么**：`.ko` 是内核模块，硬盘控制器驱动、文件系统、TEE 通道全在里面。
- **它做什么**：按写死的清单（约 158 个）从 `/lib/modules/<名字>.ko` 逐个 `insmod`，跑两轮
  （因为模块之间有依赖，第一轮会有一部分因缺依赖失败，第二轮才收敛）。
- **需要什么材料**：那一批 `.ko` **[绑]**——厂商私有，版本必须和内核一致，用
  `pull/pull_modules.sh` 抓。分成两组：
  - 存储组：`ufs_qcom` `phy-qcom-ufs*` `qcom_scm` `qcom_ice` … → 让 `/dev/sda*` 出现
  - TEE 组：`tmecom-intf` `qseecom_proxy` `si_core_module` `mem_object` `smcinvoke_dlkm`
    → 让 `/dev/smcinvoke` 能用、TA 能加载
- **成功的样子**：`/dev/block/...`、`/dev/sda34` 出现；`/dev/mapper/control` 存在；
  `/dev/smcinvoke` 可打开；init 日志里 `set pass0 ok=NN` / `pass1 ok=NN`。
- **失败会怎样**：后面每步都会连锁失败，init 会走 `fallback`（回 Android），不会动数据。
- **交给下一步**：可用的块设备 + 可用的 device-mapper 接口 + 可用的 TEE 通道。

---

## 步骤 3：向 TEE 要存储密钥（kmkey）

- **是什么**：Android 的 userdata 是用一个 100 字节的密钥做 AES-XTS 加密的。这个密钥本身
  被 TEE（keymaster TA）保管，普通世界拿不到明文，只能"请它做运算"。
- **它做什么**：`kmkey` 打开 `/dev/smcinvoke`，找到 keymaster TA（服务号 122），把 254 字节的
  内层 KMKD 交给它，请求 `convertStorageKeyToEphemeral`，拿回 100 字节结果。
- **需要什么材料**：
  - `inner.bin`（254B）**[绑]**：就是 `/metadata/vold/metadata_encryption/key/encrypted_key`
    去掉 28 字节头；换机器必须重抓，而且**只在目标机器上有效**。
  - TEE 通道与 TA **[绑]**：步骤 2 载的模块 + 设备自己的 keymaster 固件。
- **成功的样子**：`CONVERT status=0`，拿到 `KEY 100B`，写成 `/key.bin`。
- **失败会怎样**：拿不到密钥 → 后面的 dm 表建不起来 → 回 Android（数据一点没动）。
- **交给下一步**：一个 100 字节的密钥（十六进制 200 个字符）。

---

## 步骤 4：用密钥建 dm 映射（dmcreate）

- **是什么**：device-mapper 是内核里的"虚拟块设备"层。Android 把 userdata 做成一张
  `dm-default-key` 表：读的时候自动解密，写的时候自动加密，密钥由内核侧的硬件密钥槽持有。
- **它做什么**：`dmcreate` 打开 `/dev/mapper/control`，发四个 ioctl：
  `CREATE(3)` 建一个叫 `userdata` 的虚拟设备 → `TABLE_LOAD(9)` 装表：
  `aes-xts-plain64 <200位hex> 0 /dev/sda34 0 3 sector_size:4096 iv_large_sectors wrappedkey_v0`
  → `SUSPEND(6)`（参数为空时表示 resume，让表生效）→ 再用 `mknod` 造出 `/dev/mapper/userdata`。
- **需要什么材料**：100B 密钥（上一步）**[绑]** + userdata 块设备路径 **[绑]**
  （本机 `/dev/sda34`）+ 表参数 **[通，可配]**。
- **成功的样子**：`created userdata dev=0x...` / `table loaded` / `resumed`，
  `/dev/mapper/userdata` 存在，`dd` 读头几个扇区能看到 f2fs 魔数。
- **失败会怎样**：表不生效或挂载失败 → 回 Android。**这一步只建映射，不写数据**。
- **交给下一步**：一个"看起来是明文"的块设备 `/dev/mapper/userdata`。

---

## 步骤 5：挂载 userdata

- **是什么**：把上一步的块设备挂成目录树。
- **它做什么**：`mount /dev/mapper/userdata /newroot f2fs`。
- **需要什么材料**：f2fs 支持（内核里或模块里）**[绑/通]**、映射生效。
- **成功的样子**：`mount data=0`，`/newroot` 下能看到 `debian2/`、`stage7-*.log` 等。
- **失败会怎样**：`fallback`。这一步开始才真正"摸到"用户数据，所以之前任何失败都不会影响它。
- **交给下一步**：一棵可读写的明文文件系统（我们的 Linux rootfs 就在里面的 `debian2/`）。

---

## 步骤 6：把固件目录绑到 initramfs 根

- **是什么**：内核的固件加载器（`request_firmware`）**只在 initramfs 根找 `/lib/firmware`**，
  它不看 chroot 之后的新根。
- **它做什么**：`mount --bind /newroot/debian2/lib/firmware /lib/firmware`。
- **需要什么材料**：rootfs 里的固件文件 **[绑，但可批量复制]**（本机 626 个 / 175 MB，
  来自 vendor 分区的 `/firmware` 与 modem 分区的 `/image`）。
- **成功的样子**：`firmware bind=0 adsp.mdt=1`；之后 dmesg 里不再有
  `Direct firmware load for adsp.mdt failed with error -2`。
- **失败会怎样**：不致命，但 ADSP/GPU/相机这些后加载的驱动会报 -2，功能缺失。
- **交给下一步**：让内核在切根之后仍能加载固件。

---

## 步骤 7：写 BCB（安全网）

- **是什么**：BCB = bootloader control block，在 `misc` 分区开头的一小块，bootloader 会读它。
- **它做什么**：`bcb_set_recovery()` 写入 "boot-recovery"，于是**下一次重启会自动进 recovery**，
  而不是卡在一个可能起不来的 Linux。
- **需要什么材料**：`misc` 分区 **[绑，位置固定]**。
- **成功的样子**：`bcb set=<字节数>`；重启后确实进 recovery，能从 recovery 里的 `adb` 看日志。
- **失败会怎样**：不是数据问题，只是"回不去"，只能手动按键。
- **交给下一步**：一个失败可退出的环境。

---

## 步骤 8：切根，把控制权交给 Debian 的 systemd

- **是什么**：initramfs 是临时的，真正要跑的系统在 `/newroot/debian2`。切根就是换掉 `/`。
- **它做什么**：
  1. `unshare(CLONE_NEWPID)` + `fork`：让 systemd 在一个新的 PID 命名空间里当 PID 1
     （否则 systemd 会拒绝启动，或者它崩了会直接掀翻整机）；
  2. 子进程 `unshare(CLONE_NEWNS)`，自己挂 `proc`；
  3. `MS_MOVE` 把 `/newroot/debian2` 挪到 `/`，`chroot(".")`，`exec /sbin/init`。
- **需要什么材料**：rootfs（第 5 步挂出来的）**[绑，就是你的 Debian 树]**。
- **成功的样子**：`child ms_move=0 chroot=0`，随后 journal 里是 systemd 的启动日志，
  `systemctl is-system-running` 给出 `degraded`/`running`。
- **失败会怎样**：`fallback` 回 Android。
- **交给下一步**：一个正在跑的 Debian，`/` 就是 userdata 上的那棵树。

---

## 步骤 9：Debian 里先把 USB 通道建起来

- **是什么**：USB gadget + adbd —— 我们的"控制通道"，因为这台机器没有别的调试口。
- **它做什么**：先让 ADSP 跑起来（`pmic_glink` 的 typec connector 依赖它），再反复探
  `a600000.ssusb` 直到 `/sys/class/udc` 出现；然后自己建 configfs gadget（`18d1:4ee7`）
  挂 `ffs.adb`，绑到 UDC，跑 adbd。
- **需要什么材料**：内核 configfs/functionfs（内建）**[通]**、厂商 USB 模块 **[绑]**、
  ADSP 固件 **[绑]**。
- **成功的样子**：主机 `adb devices` 出现 `piano00000001  device`。
- **失败会怎样**：没有远程 shell；看门狗到点回 Android 并把日志留在 `/var/log/`。
- **交给下一步**：一条能远程执行的 shell。

---

## 步骤 10：把面板点亮（显示）

- **是什么**：`msm_drm` 是显示驱动；kwin 是合成器；plasmashell 是桌面。
- **它做什么**：`insmod msm_drm.ko dsi_display0="<面板名>:"`（不带这个参数会空指针崩）
  → `udevadm trigger --subsystem-match=drm` → `echo off` 掉两个 writeback 虚拟连接器
  → `kwin_wayland --drm` → 起来后补 `plasmashell`。
- **需要什么材料**：显示模块与 GPU 固件 **[绑]**、面板名（cmdline 里的 `dsi_display0`）**[绑]**。
- **成功的样子**：`/sys/class/drm/card0-DSI-1/enabled = enabled`，面板亮。
- **失败会怎样**：`enabled` 但 plane 空 → 黑屏（**当前就卡在这**：drmtopo 的 DSC 属性提交
  报 -E22，kwin 拿不到可用输出）。
- **交给下一步**：一个有桌面的系统（还没有网络）。

---

## 步骤 11：网络（还没做）

- **是什么**：WLAN 驱动 + 固件 + `wpa_supplicant`。
- **它做什么**：载 `cnss2` / `qca_cld3_wlan`，把 WLAN 固件放进 `/lib/firmware`，起 wpa_supplicant。
- **需要什么材料**：这两个模块在 `vendor_dlkm`（超级分区里的逻辑分区）**[绑]**，
  用 liblp + losetup 的方式取（FINDINGS 65.2 记了方法）。
- **成功的样子**：`ip -br a` 里出现 `wlan0`，能连上 AP。
- **交给下一步**：完整可用的桌面系统。

---

## 一张速查表

| 步骤 | 材料 | 通用? | 成功标志 | 交给下一步 |
|---|---|---|---|---|
| 1 bootloader | init_boot 镜像 | 绑（盐/分区） | 内核跑起 `/init` | ramdisk + cmdline |
| 2 载模块 | 158 个 .ko | 绑 | `/dev/sda34`、`/dev/smcinvoke` | 块设备 / TEE / dm 接口 |
| 3 kmkey | `inner.bin` 254B | 绑 | `KEY 100B` | 100B 密钥 |
| 4 dmcreate | 密钥 + userdata 设备 | 密钥绑/参数通 | `/dev/mapper/userdata` | 明文块设备 |
| 5 mount | f2fs 支持 | 通 | `/newroot` 可读 | 明文文件系统 |
| 6 firmware bind | 固件文件 | 绑 | `adsp.mdt=1` | 内核仍能取固件 |
| 7 BCB | misc 分区 | 绑 | 下次重启进 recovery | 可退出的保险 |
| 8 切根 | rootfs | 绑 | systemd 起来 | 正在跑的 Debian |
| 9 USB | 厂商 USB 模块 + ADSP 固件 | 绑 | `adb devices` 有设备 | 远程 shell |
| 10 显示 | msm_drm + 面板名 | 绑 | `DSI-1 enabled` | 桌面 |
| 11 网络 | cnss2/qca_cld3 + WLAN 固件 | 绑 | `wlan0` 出现 | 完整系统 |

规律很清楚：**通用的是"怎么做"（代码、参数、编排），设备绑定的是"用什么做"（密钥、模块、
固件、分区名）。** 所以在别的机器上，第 2~4 步和第 9~11 步的材料都要用 `pull/` 重抓。
