# 通用 / 不通用 分界

这个工具集里只有两类东西：**跨设备不变的代码**，和**只能从目标设备上抓的材料**。
下面把每一份文件明确归到其中一边，并给出抓取脚本。

## 通用（跟着 git 走，不需要改）

| 文件 | 说明 |
|---|---|
| `src/kmkey.c` | 与 TEE 对话取存储密钥。TA 名字/服务号/设备节点都是参数，不写死 |
| `src/dmcreate.c` | device-mapper 建表。目标类型、参数、扇区大小都是参数 |
| `fbe-run.sh` | 编排：载模块 -> 找 userdata -> 取 key -> 建表 -> 挂载 |
| `conf/fbe.conf.example` | 配置模板（不含任何设备真实值） |
| `build.sh` / `build_on_device.sh` | 构建 |
| 
代码里唯一与平台绑定的假设是**接口族**，不是具体取值：

- 假设 TEE 是 Qualcomm smcinvoke 一代（`/dev/smcinvoke` + `INVOKE = _IOWR(0x98,1)`）
- 假设加密方案是 Android 的 `dm-default-key` + `wrappedkey_v0`（或可切的 `dm-crypt`）

换到 `/dev/qseecom` 的老高通、或 MTK/Exynos，需要另写 TEE 前端，但 `dmcreate` 与
编排逻辑仍然可用。

## 不通用（每台设备必须重新抓，脚本在 `pull/`）

| 材料 | 为什么不能通用 | 抓取脚本 | 落到哪 |
|---|---|---|---|
| keymaster 输出的 key 材料（214B blob / 282B encrypted_key / 254B inner KMKD） | TA 绑定设备，搬走也解不开 | `pull/pull_key_material.sh` | `devices/<name>/keys/` |
| 厂商 TEE 用户态库 `libkeymaster*.so` | 与设备固件配套 | `pull/pull_modules.sh` | `devices/<name>/libs/` |
| TEE/存储相关内核模块（`tmecom-intf`、`qseecom_proxy`、`si_core_module`、`mem_object` …） | 厂商私有模块，版本与内核绑定 | `pull/pull_modules.sh` | `devices/<name>/modules/` |
| `fstab` / 分区表（userdata 名字、扇区大小、dm 参数） | 每台机器布局不同 | `pull/pull_fstab.sh` | `devices/<name>/docs/` |
| 设备 profile（TA 名、服务号、userdata 设备、目标类型、模块清单） | 上面前四行的汇总 | `pull/pull_profile.sh` | `devices/<name>/fbe.conf` |

一次抓齐：

    pull/pull_all.sh <设备名>        # 例如 pull/pull_all.sh piano2

抓完得到 `devices/<设备名>/`，把它连同 `conf/fbe.conf.example` 一起拷到目标机，
`fbe-run.sh devices/<设备名>/fbe.conf` 即可。

## 已经抓好的样例

`devices/piano/` 是本仓库验证过的那台（Xiaomi Pad 8 Pro / SM8750）抓取结果，
可以当作对照：key 材料、profile 都在里面。**换设备时不要复用它的 keys。**

## 安全边界（建仓库前必读）

**绝对不要提交的东西**（`.gitignore` 已挡，别用 `git add -f` 绕过）：

| 路径 | 为什么 |
|---|---|
| `devices/*/keys/` | keymaster 的 `encrypted_key` / `keymaster_key_blob`。虽然被 TEE 包着、换机也解不开，但它加上设备本身就能解出 userdata，属于用户数据的钥匙 |
| `devices/*/libs/` | 厂商 `libkeymaster*.so`，专有二进制，转发有许可问题 |
| `devices/*/modules/` | 厂商 TEE 内核模块，同上 |
| `devices/*/docs/` | fstab / 分区表，属设备布局 |
| `devices/*/fbe.conf` | 由抓取脚本生成，含本机路径 |

**代码侧已核对**：`src/kmkey.c`、`src/dmcreate.c`、`src/init_stage7raw24.c` 里没有任何
硬编码密钥；init 是在运行时把 100 字节 key 读出来再转 hex 拼 dm 表的（`/key.bin`）。
仓库里 grep `[0-9a-fA-F]{64,}` 为空。

**如果要放样例**：只描述结构和尺寸（214/282/254 与各自含义），不要放真实字节。

## 拉取来源：设备上的确切路径

下表的"本机实测"列是 piano 在 recovery 里跑出来的真实结果（`pull/probe.sh` 的输出）。

| 拉取项 | 设备路径 | 脚本 | 备注 |
|---|---|---|---|
| key 材料 | `/metadata/vold/metadata_encryption/key/` 下的 `encrypted_key`(282B)、`keymaster_key_blob`(214B)、`secdiscardable`(16K)、`version` | `pull_key_material.sh` | `key_back/` 是同一份的备份；`/metadata` 自己是加密的，**必须在 vold 已挂起来之后**读（正常 Android 或 recovery） |
| keymaster 分区 | `/dev/block/by-name/keymaster_a` -> `/dev/block/sde11` | `pull_key_material.sh`（兜底） | 里面只有 214B 级 wrapped 原件，不能直接当 key 用 |
| 厂商 TEE 模块 | `/vendor/lib/modules`、`/vendor_dlkm/lib/modules`、`/odm/lib/modules`、`/odm_dlkm/lib/modules`、`/system_dlkm/lib/modules`；Debian 下是 `/usr/lib/modules/$(uname -r)/` | `pull_modules.sh` | **recovery 里 vendor\* 不挂**，实测全空；要么正常 Android 启动，要么在 Debian 里抓 |
| 厂商 keymaster 库 | `/vendor/lib64/libkeymaster*.so`、`/system/lib64/libkeymaster*.so` | `pull_modules.sh` | 本机这几份在 `/system/lib64`（`libkeymaster4support.so` 等） |
| fstab | `/vendor/etc/fstab.qcom`、`/odm/etc/fstab.qcom`、`/first_stage_ramdisk/fstab.qcom` | `pull_fstab.sh` | 本机实测只有 `/first_stage_ramdisk/fstab.qcom` |
| 分区表 | `/sys/class/block/*/uevent` 的 `PARTNAME=`、`/dev/block/by-name/*` | `pull_fstab.sh` | 本机：`userdata=sda34`、`metadata=sda20`、`keymaster_a=sde11`、`misc=sda12` |
| cmdline / 属性 | `/proc/cmdline`、`getprop ro.boot.slot_suffix / ro.product.device / ro.board.platform` | `pull_fstab.sh` | cmdline 里还有 `msm_drm.dsi_display0=`、`hwid.*` 等 |
| 内核能力 | `/proc/config.gz`（`DM_DEFAULT_KEY` / `DM_CRYPT` / `BLK_INLINE_ENCRYPTION`）、`/dev/smcinvoke` vs `/dev/qseecom`、`dmesg | grep keymaster` | `probe.sh` | 决定 dm 目标类型与 TA 坐标 |

### 能看到什么，取决于怎么启动的

| 启动方式 | key 材料 | vendor 模块/库 | fstab | 内核 config / smcinvoke |
|---|---|---|---|---|
| recovery（TWRP） | 可见（`/metadata` 已解密） | **不可见**（vendor\* 未挂） | 只有 `/first_stage_ramdisk` | 可见 |
| 正常 Android | 可见 | 可见 | 可见 | 可见 |
| 我们自己的 Debian | `/metadata` 未挂，不可见 | 模块已在 `/usr/lib/modules/$(uname -r)` | 不可见 | 可见 |

所以完整抓取要跑两次：recovery 抓 key + fstab + 内核能力，Android 或 Debian 抓模块与库。
`pull_all.sh` 会把两次的结果都放进同一个 `devices/<设备名>/`。
