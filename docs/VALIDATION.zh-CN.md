# 0.1.0-candidate.7 验证范围

2026-10-08，现有环境、uniBlacklist/main。本轮改紧凑双语 GUI 和请求记录窗口。用户已确认 candidate.6 过滤功能基本正常；匹配、姓名、延迟、Wi-Fi 解析及原生拦截源代码保持不变。没有启动原游戏，没有调用旧 Python 战斗模拟。旧 [candidate.6 验证](VALIDATION_CANDIDATE6.zh-CN.md) 和已发布包保留。

## 产品行为

- 主窗口默认 880×620 逻辑像素，最小 760×520。候选 5 列、黑名单 1 列，删除手动 SteamID64、备注输入、手动添加及复制账号按钮。完整版诊断仍可保存到文件。
- 中文／English 即时切换窗口名称、控件、表头、状态、记录动作及自身错误；独立 language.ini 原子保存，不改变 options.ini 或 blacklist.tsv。操作系统提供的文件对话框按系统语言显示。
- 独立请求窗口读取现有 candidate_skips、request_rejects、send_rejects、receive_drops 原生累计计数。500ms GUI 观察周期把增量分类型汇总，显示观察时间、动作和数量，最多200条。没有逐条请求时间或玩家/IP归属，不能把计数当作网络丢包率。
- 连接时建立基线，不伪造连接前事件；清空仅清显示，原计数和基线保留。关记录窗口仍观察，重开恢复；新游戏进程建立新上下文。主窗口关闭仍停止原生过滤。
- 原120秒候选保留、按完整账号保持选择、永久名单及 Wi-Fi 临时排除沿用，协议仍ABI3、原文件SHA检查不变。完整说明见 [GUI_REDESIGN](GUI_REDESIGN.zh-CN.md)。

## 检查及证据

编译使用 MinGW-w64 GCC14 / PE32 / 静态运行库，启用 -Wall -Wextra -Werror；新增 Common Controls v6、DPI manifest、原创 UB 图标和中英版本资源。资源COFF、XML、图标9种尺寸及透明角检查通过。临时控制仅在忽略目录，不上传测试集或放入发布ZIP。

- 请求记录模型250项：四类增量、无变化不重复、基线、清空、PID切换、DWORD回绕、200条边界以及墙钟调整。
- 语言配置37项：默认、即时保存/重读、非法/损坏文件、写入失败保留旧文件、独立名单/Wi-Fi选项、已知错误和附加Windows错误码英文转换。在Wine/QEMU中退出0，stderr有134字节（两条未归属QEMU signal11），保留原记录，不视为原生Windows验证。
- 57项编入生产GUI的控制验证真实列表、姓名/64ms/网线/Wi-Fi、选择稳定、两分钟TTL、共享策略、关闭规则、游戏退出与自动诊断；新增记录窗口实际计数增量、语言切换保留选择及记录、清空保留计数、关子窗继续观察。进程退出0、stderr为空，结果和指纹见同版本package-receipt.json。
- 53项最终GUI EXE的跨进程控制验证紧凑尺寸、删除入口、中文/English窗口和按钮、语言及Wi-Fi独立重启保存、请求窗口打开/清空/关闭、原名单读取/解除和正常退出。进程退出0、stderr为空。Wine远程列文本读取受到ERROR_ACCESS_DENIED，已移除该控制的远程内存操作；不是产品功能。原生列表翻译由生产GUI控制和真实Windows截图核对。
- DLL逻辑未修改；candidate.6 的89项原生及77项元数据控制作为历史证据沿用，明确不是本轮重新运行。最终配套DLL仍有实际加载和null/ABI2/非游戏拒绝检查。

Wine/QEMU用于功能控制，不证明Windows字体、DPI或真实Steam行为；其Fontconfig及QEMU异常按实际stderr保留。精确项数、进程结果、当前与沿用证据区分见package-receipt.json。

## Windows发布门槛

GitHub windows-2022 runner在同版本ZIP上实际启动GUI，检查紧凑尺寸、删控件、双语主窗/记录窗、语言与Wi-Fi保存重启、记录开关/清空、原名单解除及正常退出；32位PowerShell实际加载最终DLL和拒绝请求。还生成中英主窗/请求窗四张真实空界面截图，拒绝全黑或空白截图。仅该门槛通过后才上传Release并重下载校验SHA、大小、CRC和文件数。具体成功运行及截图下载回执在Release delivery-receipt.json，不提前把尚未执行的workflow记为通过。

Windows门槛不启动原游戏、不注入游戏、不读取真实Steam对象，也不证明高DPI多显示器、所有字体、真实联机或多插件共存；实际游戏效果由用户验证。没有虚构玩家或网络事件的发布截图。

## 使用边界

升级请关闭旧主窗口并重启游戏，完整解压同版本EXE/DLL。旧名单和Wi-Fi设置沿用，默认中文，无规则时仍只观察。真实匹配的过滤范围、未知类型放行、未观察到的被动来访者放行、Battle暂停、延迟为Steam估计、原先退出根因尚未确认等边界不改变。原生证据和限制见上一版验证及NATIVE_CONTRACT。

包不含原游戏文件、Steam DLL、个人日志、名单、凭据或测试集。实际下载回执在对应downloads目录和Release。
