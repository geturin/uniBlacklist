# candidate.3：快速匹配闪退调查与诊断合同

2026-10-08，Asia/Tokyo。沿用当前环境、uniBlacklist/main。原游戏未运行。

## 已有证据与结论

用户报告：只有游戏退出，GUI 存活；黑名单为空、仅观察玩家列表，进入快速匹配后2–3秒百分百卡顿并退出；GUI 曾显示原搜索1，其他计数0。上传的 candidate.2 状态文本 PID 和全部计数为0，没有异常位置。检查 GUI 源码发现退出会清空 snapshot/PID，因此该报告不足以判断故障模块，也不能证明一次搜索就必然是崩溃根因。

**尚无真实 Windows 异常代码、故障模块/RVA或故障现场，根因未定。** 不将额外 SDK 调用、栈压力或通信表接入中的任何一项写成已确认原因。

## 静态核对的入口

同 SHA256 EXE 和 Steam DLL；只读取原文件和已有 REA 反编译/反汇编。

| 位置 | 核对结果 |
| --- | --- |
| EXE `0x4f5470` | 搜索生产函数：ECX=self，1个栈参数，RET4；AL表示成功 |
| `0x4f5f90`、调用点 `0x4f6099` / 返回 `0x4f609e` | 调用后测试AL，再执行 completion；completion 参数与本挂钩未改 |
| `0x4e6040` | Join，64位 lobby 栈参数，RET8 |
| `0x4e5b50/0x4e5970/0x4e5a00` | P2P请求、聊天、成员事件，RET4；继续保留退出/清理事件 |
| `0x4ef6b0` | 原 PingLocation 解析/估计调用，经 Utils vtable+0x18/+0x10 |
| Steam flat 导出 Persona/GetLobbyData/GetLobbyOwner/ParsePing/EstimatePing | DLL 真实包装体核对了 cdecl+self、64位 ID/返回；未发现“缺少self”的证据 |
| EXE `0x4f33e0/0x4f3260` | 搜索玩家记录/成员数组；记录+0x14是十进制 SteamID 字符串，不能冒充玩家名 |

旧 candidate.2 `hook_search` 编译后固定局部栈预留 `0x5b6c`=23404字节，进入原函数前已有 `__chkstk_ms`。新版薄调用帧固定预留 `0x3c`=60字节，缓存移入不内联的捕获函数并使用堆。数值是局部预留，**不是整个调用链最大栈深度**。尚未确定原搜索线程栈大小，也没有栈溢出的实测证据。

## 排位动态列表与黑名单的区别

本项目主要场景是排位匹配，候选可能上一分钟有人、下一分钟无人。游戏每次搜索仍持续捕获；最新结果0人时 GUI 保留未过期历史行，状态区分别显示本次捕获人数、最近2分钟人数和已拉黑人数。安装/过滤策略只检查持久黑名单 `blocked_count/id_count`，绝不使用 `candidate_count/capture_count` 来关闭保护。已拉黑玩家暂时不在搜索结果中，黑名单仍保存并在后续请求/通信时判断其账号。

## 实现变化

1. 首次黑名单为空/开关关闭时只安装搜索观察入口。首次启用有效非空黑名单、且非战斗时才安装其余4个游戏入口及两套通信组；失败锁存、全部过滤停用。已安装入口不会在名单清空时卸载，各 hook 先判断是否应过滤，无需过滤时直接调用原函数，不额外查询 owner、读身份或改写接收数组。黑名单为空不会报告有效保护。
2. 原函数调用保留独立小栈帧。成功后捕获原结果；本插件 C++ 分配失败按原结果放行，不把原函数置于该 catch 内，不使用异常恢复替代游戏逻辑。保持完整EAX、原始 LastError 和原调用约定。
3. 搜索捕获不额外调用 Persona、LobbyData/PingLocation、ParsePing 或 EstimatePing。当前仅记录原缓存内已有 SteamID64、LobbyID、观测时间、过滤标记；**姓名与估计延迟临时未知**。Join 在启用非空过滤时仍按既有原房间信息查询 owner，这条真实调用尚需复验。
4. GUI 两分钟历史由生产端 observation tick 计算，去重、稳定行序/选择；不重新写入游戏候选。2048行边界满时保留旧行并记录省略数量；旧快照重读/名单更新不延长玩家寿命。
5. 游戏退出保留最后可读共享状态及PID，取得真实进程退出码；report 明确 disconnected、last_before_exit，有效保护强制0。正常退出同样自动保存状态；文件不因关闭GUI而删除。

## 异常记录格式与边界

`include/fault_trace.h`：每条64字节、magic `0x31464255`、version1，最多8条、最近条目循环覆盖，文件最多512字节。字段是 PID/TID、序号、tick、异常code/flags、EIP、读写执行类型及故障地址（有参数时）、指令页 AllocationBase、当前线程阶段、已捕获搜索次数。不写堆栈、完整寄存器、内存转储或玩家信息。

启动时预开文件并分配 TLS；VEH 只作小固定结构、有限 Windows 调用、文件写入，不分配堆、不取得控制/策略锁、不调用 Steam，不改变现场；始终 `EXCEPTION_CONTINUE_SEARCH`。GetLastError 在诊断 scope 和 handler 中保留。原游戏的搜索执行与捕获、Join、房间回调、网络过滤及安装分别标记；阶段嵌套/线程隔离。如果异常发生在 hook 已返回后的 completion，会显示 outside_hook，不能据此排除先前损坏。

AV/in-page/非法或特权指令/整数除零/栈溢出/堆损坏/栈缓冲失败代码为可记录类别。它们仍可能是游戏正常处理的第一时间异常。Handler 不保证捕获 fail-fast、TerminateProcess、ExitProcess、栈已耗尽、磁盘失败或同时递归异常；竞争/重入不阻塞另一个 handler。没有异常条目不能排除闪退，也不能证明注入无影响。

GUI 在连接时缓存模块名/基址/范围，只输出文件名和RVA；连接之后新加载/卸载、动态 thunk 地址可能没有可靠模块名。本地路径：`%LOCALAPPDATA%\UNI2Blacklist\diagnostics\uni2-<PID>-fault.bin` 和 `uni2-<PID>-status.txt`。自动保存文本已展开记录，无需上传原二进制或完整游戏目录。报告可用的旧安装位只是历史值。

## 仍需要的原生证据

- candidate.3 黑名单为空快速匹配是否还出现2–3秒退出；报告应 capture/search增长、hooks_ready0、effective0、拦截计数0。
- 如再退出：新版文本的 game_exit_code、fault.*.code/module/module_rva/eip/stage、search_count、snapshot_source。没有 fault 时仍保留退出码/最后计数，并据此决定是否需要独立故障捕获工具，不直接归因。
- 若 EIP 在 `0x4f5470` 或调用者 completion 附近，继续核对该 RVA、指令操作数、对象生命周期、原实际线程/栈及 trampoline。若在 SDK/runtime thunk，核对模块/RVA与对应接口实际调用约定/槽位归属。
- 加入名单后的真实网络接入、SDK共享表、多实例调用及原对象生命周期，尚未完成 Windows 实测。首先确认黑名单为空观察，再验证已知账号拦截；不能用自建测试替代对局证据。
- 安全的姓名/估计延迟缓存来源仍需进一步证据，不把数字账号/区域字符串当姓名，也不在尚未定位闪退时恢复搜索额外调用。

本轮临时控制和证据检查不提交到仓库/源包。历史 candidate.1/2 保留，不覆盖下载。
