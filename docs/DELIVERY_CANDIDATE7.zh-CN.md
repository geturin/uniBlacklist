# candidate.7 紧凑双语 GUI 交付

2026-10-08。现有 uniBlacklist/main；candidate.6 和更早已发布下载保留。仅调整GUI、本地语言配置和读取现有原生计数，src/runtime.cpp、src/settings.cpp及candidate_history.h对上一检查点无代码变更。

实现提交 [78fe6a2](https://github.com/geturin/uniBlacklist/commit/78fe6a24a1d6fb8395886e2cb89cd6e9d81b8eca)；最终源码包包含 [886ff8d](https://github.com/geturin/uniBlacklist/commit/886ff8d1cc551133a08c5e71996b52484f5de5cd)；发布包提交 [4c12b83](https://github.com/geturin/uniBlacklist/commit/4c12b8341a0177904d129041ffe2d818f9109959)。[Release](https://github.com/geturin/uniBlacklist/releases/tag/v0.1.0-candidate.7)。本交付记录及公开下载回执追加保存，不改已核验的ZIP。

## 改动与范围

- 主窗880×620逻辑像素，5列玩家、1列名单，浅色卡片和新UB图标。去除说明段落、SteamID64/备注输入、手动添加和复制账号入口。
- 中文/English即时切换主窗及请求窗标题、品牌、控件、表头、行状态和自身已知错误，单独language.ini保存。原名单、Wi-Fi选项和两分钟历史保留。
- 请求窗四个实际累计计数及500ms观察增量，最新在前、最多200条。清空只清显示，关子窗保留并继续观察；不宣称单个请求时间或玩家/IP归属，不是丢包率。
- 三个列表使用LVS_SHAREIMAGELISTS，GUI显式管理共享图像，关闭子窗不释放主列表仍在使用的资源。

## 实际验证

- 250项记录模型、37项语言配置、57项生产GUI历史/策略/计数/退出、53项最终EXE跨进程控件，以及最终DLL实际加载与null/ABI2/非游戏拒绝。临时控制只在忽略目录，没有上传测试集。
- Wine/QEMU检查进程退出0。语言stderr134字节（两条未归属signal11）；53项GUI返回时stderr为空，后来同文件追加67字节signal11，固化回执以最终67为准。57项生产GUI及最终DLL stderr为空。没有确定这些QEMU信息的来源或将其归因于原游戏。远程列文本写入Win32=5限制已记录，列翻译在生产GUI与Windows截图核对。
- [原生Windows与发布工作流](https://github.com/geturin/uniBlacklist/actions/runs/37746776650)两个job均success。Windows Server2022实际运行同ZIP的GUI，测得880×620、DPI96；删入口/列数、中英主窗及子窗即时切换、语言及Wi-Fi独立重启保存、清记录/关子窗/再开、已有名单解除、正常退出通过。32位PowerShell实际加载最终DLL并检查导出拒绝行为通过。
- 四张截图来自该次真实Windows桌面：主窗880×620、请求窗620×440，中英文均实际绘制，人工查看无黑屏、文字正常、列表和按钮无重叠；为空界面，无玩家账号、个人日志或原素材。它们不代表所有DPI、多显示器或每一种字体验证。
- 历史89项原生和77项元数据控制在candidate.6已验证，本次未重新运行，原生过滤代码未变。未启动原游戏、未注入原游戏、没有真实Steam匹配或对局验收。

早期 [37745349735](https://github.com/geturin/uniBlacklist/actions/runs/37745349735)在回执读取阶段因Windows默认CP1252而失败，未进入产品运行；显式UTF-8后修正。[37745884377](https://github.com/geturin/uniBlacklist/actions/runs/37745884377)完成主窗/尺寸/Wi-Fi保存及中文截图，因验收查找子窗时PowerShell传空字符串标题而失败；改为C#内部真实null后，上述最终同包验收通过。两次未进入发布阶段，不计为成功运行。

## 公开下载核验

本环境在 2026-10-08T08:02:11.465874+00:00 实际GET两个ZIP、四张PNG及回执/校验文件，均HTTP200。ZIP大小、SHA256、CRC和文件数匹配；PNG大小和SHA匹配，人工检查完成。无原游戏、Steam DLL、用户设置、个人日志、凭据或测试集。

| 下载 | 字节数 | 文件数 | SHA256 |
| --- | --- | --- | --- |
| [UNI2Blacklist-0.1.0-candidate.7-win32.zip](https://github.com/geturin/uniBlacklist/releases/download/v0.1.0-candidate.7/UNI2Blacklist-0.1.0-candidate.7-win32.zip) | 740101 | 18 | `9db6b9dc17af389cecddad37b43a78f49eab0c882bb95c371bcfea74551a915b` |
| [UNI2Blacklist-0.1.0-candidate.7-source.zip](https://github.com/geturin/uniBlacklist/releases/download/v0.1.0-candidate.7/UNI2Blacklist-0.1.0-candidate.7-source.zip) | 171850 | 54 | `0d130e5487be405f9f87767d5436fb8d788857f673a5250a46e17f193113fca9` |

[中文主窗](https://github.com/geturin/uniBlacklist/releases/download/v0.1.0-candidate.7/UNI2Blacklist-0.1.0-candidate.7-ui-zh.png) · [英文主窗](https://github.com/geturin/uniBlacklist/releases/download/v0.1.0-candidate.7/UNI2Blacklist-0.1.0-candidate.7-ui-en.png) · [中文请求窗](https://github.com/geturin/uniBlacklist/releases/download/v0.1.0-candidate.7/UNI2Blacklist-0.1.0-candidate.7-requests-zh.png) · [英文请求窗](https://github.com/geturin/uniBlacklist/releases/download/v0.1.0-candidate.7/UNI2Blacklist-0.1.0-candidate.7-requests-en.png)

范围和stderr哈希见同版本package-receipt.json；原生Windows及发布端重下载见Release delivery-receipt.json；本环境HTTP核验见 downloads/0.1.0-candidate.7/DOWNLOAD_VERIFICATION.json。

## 升级

关闭旧主窗并重启游戏，再完整解压candidate.7配套EXE/DLL。关闭主窗不能卸载旧DLL。旧名单和Wi-Fi设置沿用，语言默认中文。真实匹配过滤、未观察到的来访者/未知类型放行、Battle暂停等原边界不变，最终游戏使用由用户验证。
