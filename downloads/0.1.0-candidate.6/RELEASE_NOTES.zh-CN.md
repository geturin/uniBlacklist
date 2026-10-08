修正candidate.5主动搜索／被动待机Wi-Fi列全部未知的问题。旧下载保留。

- 根因：上一版把匹配设置中的前一个账号当成发布者，用错SteamID绑定。两个账号不同或前一个为0时，有效连接数据被拒绝为未知；旧控制两账号相同而漏检。
- 排位发布者改为记录0x7c；房间发布者改为Base64解码后0xc2。Wi-Fi字段0x78／0xc0不变。继续核对完整64位发布者，不放松验证、不给未知填有线、无需新增Steam查询。
- 姓名、Steam估计延迟、两分钟列表、显式黑名单和独立Wi-Fi临时排除保留。关闭Wi-Fi不影响永久名单；未知或未曾观察的被动来访者放行，战斗中暂停规则。
- 原生产与原解析机器码8组，覆盖两个格式、有线／Wi-Fi、前一个ID为0／不同；旧解析器8组有效记录全拒绝、4组绑定错账号，已复现。修正后字段和publisher检查通过，没有启动原EXE或真实Steam。
- Wine/QEMU：77项当前元数据／Wi-Fi检查、89项既有原生、41项GUI历史／策略、18项最终GUI和DLL加载／拒绝检查退出0。原生与DLL stderr各有一条未归属QEMU SIGSEGV，其他检查stderr为空；未把自建通过当成真实游戏稳定性。
- Windows发布前运行实际最终GUI开关、保存、重开和取消；32位Windows PowerShell实际加载DLL及导出拒绝检查。成功后才发布，范围与workflow链接见delivery-receipt.json。真实Steam注入、Wi-Fi显示及联网拦截仍需用户实测。
- 新增connection_wired_count／connection_wifi_count／connection_unknown_count诊断计数，不记录账号或原始CustomData。
- 关闭旧窗口并重启游戏，再完整解压和使用candidate.6配套EXE／DLL；关闭GUI不能替换已注入旧DLL。原名单沿用，Wi-Fi选项沿用独立配置。
- 支持EXE SHA256 4ebed985ecbf330ab8e495573361e49df20bb555263289d1aff5425fac9b7ed9，严格校验既有未更新Steam DLL。源代码和包不含游戏文件、凭据、真实名单或测试集。
- 连接图标细微变体映射仍未知，没有新增FPS／丢包／信号筛选。本轮只纠正已知Wi-Fi标志的身份合同。
