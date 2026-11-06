# Security

## 这个项目是什么

用设备自己的 TEE（keymaster TA）只读地取出 userdata 的存储密钥，在 initramfs 阶段
建好 dm 映射，使 Linux 能直接读 Android 的 FBE 分区。

它**不**破解、不绕过密钥、不猜测口令：密钥来自设备自己的 keymaster，TEE 之外拿不到明文。

## 不会做的事

- 不写入、不格式化、不销毁 userdata；只做只读的密钥请求与 dm 建表
- 不修改 keymaster 分区、不动 metadata 里的加密元数据
- 不通过网络收发任何东西；所有抓取都走本地 `adb`

## 仓库里不会出现的材料

`devices/*/keys/`、`libs/`、`modules/`、`docs/` 全部被 `.gitignore` 挡住。这些是
设备绑定材料：换一台机器解不开，但对**同一台机器**来说就是 userdata 的钥匙。
如果你要分享经验，请只描述结构与尺寸（214 / 282 / 254 字节各是什么），不要附真实字节。

## 报告问题

如果发现某条路径可能导致数据损坏（例如非预期的写入、映射叠加、错误的 dm 目标类型），
请开 issue 并附上：设备型号、内核版本、`dmcreate -t <name>` 输出、`dmesg` 里 dm 相关行。
不要附真实密钥材料。
