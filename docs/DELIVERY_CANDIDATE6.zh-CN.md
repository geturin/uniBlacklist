# candidate.6 Wi-Fi 身份修正交付

2026-10-08 15:44 JST。现有 uniBlacklist/main；旧 candidate.5／4 下载保留。

修正与源码提交：[8762df4](https://github.com/geturin/uniBlacklist/commit/8762df4c3c112fe297b5a1803428d0f800bc8895)。发布包提交：[18863ff](https://github.com/geturin/uniBlacklist/commit/18863ff61fdc0b7220e7c50c4f866ca6a4e2fa09)。本记录追加保存，不改变已验证的 ZIP；[Release](https://github.com/geturin/uniBlacklist/releases/tag/v0.1.0-candidate.6)。

## 修正及验证范围

candidate.5错把匹配设置中的前一个账号当发布者，造成正常Wi-Fi／有线数据全未知。排位发布者改为0x7c，房间改为解码后0xc2；Wi-Fi值仍取0x78／0xc0。姓名与估计延迟路径、临时排除、永久名单、两分钟历史保留。[原因、原函数、旧验证漏检与边界](WIFI_IDENTITY_FIX.zh-CN.md)。

- 原生产及原解析机器码8组隔离执行，覆盖有线／Wi-Fi、前一个ID为0／不同，发布者与值、消费长度检查通过。旧解析器8组错误拒绝及4组错误账号绑定已复现；不启动原EXE或真实Steam。
- 当前代码Wine/QEMU：77项元数据／Wi-Fi、89项既有原生过滤、41项GUI历史／策略、18项最终GUI及最终DLL检查退出0。原生与DLL stderr各有67字节未归属QEMU SIGSEGV，其余最终stderr为空；保留哈希和边界，不据此认定真实游戏稳定。
- [Windows及发布workflow](https://github.com/geturin/uniBlacklist/actions/runs/37739187065)两项job均success。Windows Server 2022实际运行最终GUI默认开关、勾选保存、无名单写入、正常关闭／重开／取消；32位Windows PowerShell加载最终DLL并实际调用导出检查null／ABI2／非游戏拒绝。没有启动游戏、注入游戏、真实Steam匹配或字体／DPI验收。
- [较早运行](https://github.com/geturin/uniBlacklist/actions/runs/37739142262)误在尚未含包的源码提交触发，缺package-receipt.json，在包检查阶段失败；没有进入产品操作或发布。完整打包提交随后完成上述成功运行，不把较早失败算通过。

## 下载核验

本云环境15:44 JST实际重新GET公开下载链接，两份ZIP均HTTP 200，SHA256、字节数、ZIP CRC与文件数一致。没有原游戏文件、真实名单、凭据或测试集。

| 下载 | 字节数 | 文件数 | SHA256 |
| --- | --- | --- | --- |
| [Windows ZIP](https://github.com/geturin/uniBlacklist/releases/download/v0.1.0-candidate.6/UNI2Blacklist-0.1.0-candidate.6-win32.zip) | 695444 | 16 | `4bcc2eb3ee46fa43d2ccf0831410a552f5927ba7a518d8803be4306ba184507f` |
| [源码 ZIP](https://github.com/geturin/uniBlacklist/releases/download/v0.1.0-candidate.6/UNI2Blacklist-0.1.0-candidate.6-source.zip) | 119589 | 44 | `7481dc2d8df1f5d94089afb0dda7a26e7f83544777af51e9f39e52ef02409052` |

原程序与控制范围回执在同版本package-receipt.json；发布端Windows范围与重下载结果在Release delivery-receipt.json；本环境公开HTTP核验追加在downloads/0.1.0-candidate.6/DOWNLOAD_VERIFICATION.json。

## 本机复验

**关闭旧黑名单窗口并重启游戏，然后完整解压candidate.6配套EXE／DLL。** 仅关窗口不会卸载已注入的旧DLL。继续读取游戏自然搜索结果；连接类型应该按记录显示Wi-Fi／有线。读取失败或发布者不符继续未知；未观察过的被动来访者仍放行。真实Steam匹配中的显示与排除效果由用户复验。

新增connection_wired_count／connection_wifi_count／connection_unknown_count为当前快照的分类计数，最多128行，不含账号。出现未知时可用“保存诊断状态”核对当前计数。该计数不同于GUI两分钟历史人数和临时Wi-Fi缓存人数。
