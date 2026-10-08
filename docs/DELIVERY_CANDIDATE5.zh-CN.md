# candidate.5 实际交付核验

2026-10-08。现有 main 分支与环境；旧 candidate.4 和下载保留。

- 实现提交：[e6d52c3](https://github.com/geturin/uniBlacklist/commit/e6d52c3b3a2dc6dac807dbc534b41ff2b091f19e)。源码 ZIP 对应 [9d030be](https://github.com/geturin/uniBlacklist/commit/9d030be8a86fc68f85bc8b8ca2dfffa8966e4bfd)，包含 Windows 检查脚本修正。发布提交：[d6596ec](https://github.com/geturin/uniBlacklist/commit/d6596ece8e710d2a6159e86fd474e591fb2ef0be)。本交付记录追加保存，不改变已经验收并发布的 ZIP。
- [Release](https://github.com/geturin/uniBlacklist/releases/tag/v0.1.0-candidate.5)；[成功 workflow](https://github.com/geturin/uniBlacklist/actions/runs/37728394893)。Windows 与发布两个 job 均 success。
- Windows Server 2022：实际最终 GUI 默认 Wi-Fi 关闭、实际勾选与单独 options.ini 写入、没有生成账号名单、正常退出／重开恢复／取消保存；32 位 Windows PowerShell 实际加载最终 PE32 DLL，调用导出验证 null、ABI2 与非游戏进程拒绝。没有启动原游戏、没有游戏注入或真实 Steam 对象，未验收字体／DPI／真实联机拦截。
- 首次 [37728268295](https://github.com/geturin/uniBlacklist/actions/runs/37728268295) 停在 PowerShell 查找窗口。把 FindWindow 的空标题调用移入 C#、明确原生 null 后重跑通过。两次使用的 GUI／DLL 和 Windows ZIP 完全相同；未通过的首次执行不算验收成功。
- Wine/QEMU：89 项原生、64 项元数据／Wi-Fi、41 项 GUI 历史／策略、18 项最终 GUI 操作及最终 DLL 检查进程退出0。原生控制 stderr 有未归属的67字节 QEMU SIGSEGV，其余最终检查 stderr 为空；详细范围见 [VALIDATION](VALIDATION.zh-CN.md)。不以这些结果声称真实游戏稳定性。

## 实际下载

云端在 `2026-10-08T04:39:06Z` 通过下面的公开 Release 下载链接重新 GET，两份 ZIP 均 HTTP 200；核对 SHA256、字节数、ZIP CRC、文件数。Windows 包14个文件，源码41个文件。还下载并核对 SHA256SUMS 与发布回执；不含测试集、原游戏文件、实际名单或凭据。

| 文件 | 字节数 | SHA256 |
| --- | --- | --- |
| [Windows ZIP](https://github.com/geturin/uniBlacklist/releases/download/v0.1.0-candidate.5/UNI2Blacklist-0.1.0-candidate.5-win32.zip) | 688016 | `8ae5e6786ffaeabf1bcdfa799be9e78e8ab4d9096be34b18229997f3263e1294` |
| [源码 ZIP](https://github.com/geturin/uniBlacklist/releases/download/v0.1.0-candidate.5/UNI2Blacklist-0.1.0-candidate.5-source.zip) | 109646 | `2162c8f3966ad68bf824297a0b900ec4b558a1c9d688d69007a13523a39928a0` |

机器可读下载核验追加在 `downloads/0.1.0-candidate.5/DOWNLOAD_VERIFICATION.json`，发布后端验证在 Release 的 `delivery-receipt.json`，原检查范围与 stderr 哈希在 `package-receipt.json`。

## 升级与功能边界

关闭旧 GUI，**重启游戏**，完整解压新 Windows ZIP。ABI3 的 GUI／DLL 成套使用，既有 blacklist.tsv 沿用。Wi-Fi 选项第一次默认关闭，之后单独保存；勾选排除已观察 Wi-Fi 账号，不写入黑名单，取消后名单保持不变。

名字来自已完成游戏缓存，延迟是 Steam 缓存路径估计；字段缺失仍可显示未知。未知类型／未曾观察的被动来访者放行。观察过的 Wi-Fi 缓存120秒，分类变化／冲突撤销，战斗暂停规则。真实 Steam 生命周期、名单与 Wi-Fi 联机拒绝仍由用户实测。

图标调查仅做说明，未新增信号／FPS／抖动筛选。连接类型、信号档位、未知和本人状态已分开；相同“四格有线”细微图形的完整绘制条件仍未知，见 [连接状态分类](CONNECTION_STATES.zh-CN.md)。
