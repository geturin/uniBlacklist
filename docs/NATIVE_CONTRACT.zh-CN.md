# 首版原生接口合同

依据：指定 SHA-256 的 EXE、配套 Steam DLL、REA/Ghidra 静态反编译及 x86 指令检查。地址为该 EXE 优先基址 `0x400000` 下的 VA；实现使用 RVA 加实际装载基址。不是仅依据通用 Steam 文档猜地址。`profile.json` 和 `include/profile.h` 保存五个入口的 32 字节及 HIGHLOW 重定位掩码，加载时必须匹配。

## 搜索与房间

| VA | 已查行为 | 本项目使用 |
| --- | --- | --- |
| `0x4e6130` | RequestLobbyList 及 CallResult 注册 | 不另行调用，以免重置原游戏过滤器 |
| `0x4e5c50` | 列表回调：记录 count，取消 pending | 辅助证据；不直接 hook |
| `0x4f7680` | 原搜索过滤器设置及请求 | 保留原逻辑 |
| `0x4f5f90` | 请求各房间数据；调用 `0x4f5470` 后检查 AL，然后通知 completion | 证明过滤发生在 completion 前；call 返回地址 `0x4f609e` |
| `0x4f5470` | thiscall，stack 参数一个 vector 指针，ret 4，AL 为成功标记 | 原调用一次；仅成功后压缩可见索引，保留完整 EAX 和 LastError |
| `0x4e51b0` | 解析 `RoomPropertyKey_OwnerId` 为完整 64 位账号 | 辅助确认 owner；避免调用其自定义 ABI |
| `0x4e6040` | thiscall，SteamID64 lobby 参数，ret 8；清零 self+139/13b，然后 JoinLobby | 阻止加入黑名单 owner 的房间；复制原失败路径的两个 flag 写入并返回 0 |
| `0x4e5b50` | P2PSessionRequest 事件首 8 字节账号；调用旧 Accept | 黑名单事件不转交原函数 |
| `0x4e5970` | LobbyChatMsg：user+8，type+16，chatID+20；读取并分发游戏聊天处理器 | 黑名单发送者的回调不转交 |
| `0x4e5a00` | LobbyChatUpdate：先分发游戏处理器；非 entered 时关闭旧 P2P session | 仅 change+24 == 1 且 user+8 拉黑时忽略；其余事件保留清理 |
| `0x4e69c0` | 构造并注册旧 P2P、LobbyChat、现代 Messages session 回调 | 确认两套接口与 lobby 事件均需考虑 |
| `0x4e5d10` | 构造 type=16,size=8 的现代 identity，以 Messages slot 0 给房间成员发送 | 确认新通信接口实际使用；不只拦旧 P2P |

`0x4f5470` 的 self 结构：+2c allocated count，+30 row pointer，+34 visible count，+38 u32 index pointer。row stride `0x5c`；row 开头是 lobby 十进制字符串，+58 valid byte，+2c 指向玩家记录，玩家记录+8 是 SteamID64。只在全部有界读取通过后改写 index 前缀与 visible count；失败保留原数据并计数。全部拉黑时 count 为 0，而非传空 vector 后留下旧结果。

## Steam 上下文与 ABI

上下文各 12 字节，interface 指针在 +8；不能把 init 函数指针当成 interface。

| VA | 接口 |
| --- | --- |
| `0x9a3c08` | SteamNetworking006 |
| `0x9f9774` | SteamMatchMaking009 |
| `0x9f9780` | SteamFriends017 |
| `0x9f9798` | SteamNetworkingMessages002 |
| `0x9f97a4` | SteamNetworkingUtils004 |

旧 Networking：slot 0 SendP2PPacket、slot 2 ReadP2PPacket、slot 3 AcceptP2PSessionWithUser。现代 Messages：slot 0 SendMessageToUser、slot 1 ReceiveMessagesOnChannel、slot 2 AcceptSessionWithUser。实现以 x86 thiscall 原函数指针和 fastcall detour 保留 ECX、栈参数和清栈语义。

配套 DLL flat wrapper 指令确认：现代 Accept 经 vtable+8，Receive 经+4，Send 经+0。SDK 的 identity 是 type i32+0,size i32+4,id64+8；只识别 type16,size8，其他类型放行。x86 message 前缀 identity 位于+12。配套 `SteamAPI_SteamNetworkingMessage_t_Release`（VA `0x3b4061a0`）跳转 message+`0xb4` 的释放函数；实现调用该现有 flat export 释放丢弃的对象，不猜释放器、不泄漏消息。

允许路径调用原函数。拒绝路径：旧 Send/Accept 返回 false；现代 Send 返回 EResult AccessDenied(15)，Accept 返回 false；接收路径消费已取出的黑名单消息，保留其他发送者顺序。现代消息精确 Release 一次，旧接收最多向前排空 32 次以限制单次工作量。

Steam 官方接口依据为 Valve source-sdk-2013 固定提交 `0759e2e8e179d5352d81d0d4aaded72c1704b7a9` 的公开头文件：

- [isteammatchmaking.h](https://github.com/ValveSoftware/source-sdk-2013/blob/0759e2e8e179d5352d81d0d4aaded72c1704b7a9/src/public/steam/isteammatchmaking.h)
- [isteamnetworkingmessages.h](https://github.com/ValveSoftware/source-sdk-2013/blob/0759e2e8e179d5352d81d0d4aaded72c1704b7a9/src/public/steam/isteamnetworkingmessages.h)
- [isteamnetworkingutils.h](https://github.com/ValveSoftware/source-sdk-2013/blob/0759e2e8e179d5352d81d0d4aaded72c1704b7a9/src/public/steam/isteamnetworkingutils.h)
- [steamnetworkingtypes.h](https://github.com/ValveSoftware/source-sdk-2013/blob/0759e2e8e179d5352d81d0d4aaded72c1704b7a9/src/public/steam/steamnetworkingtypes.h)

接口从游戏已初始化的上下文取得，控制线程不初始化 Steam、不重新请求 LobbyList。旧与现代接口分别在缓存可用时安装；尚未齐全时 GUI 明示等待。原游戏和旧 P2P 的 MinHook 函数目标继续要求可执行 MEM_IMAGE 内存。candidate.2 的现代 Messages002 改用原表槽位指针替换：slot 0 Send、slot 2 Accept、slot 1 Receive，先保存全部原函数，再按这个顺序安装。验证既有 Messages002 六个公开方法为已提交可执行内存，现代目标可为 MEM_PRIVATE/MEM_MAPPED 的运行时 thunk；不在其入口写代码。未修改的方法、表指针、RTTI 和隐藏方法保留，既有表若被同接口其他实例共享，相应三个方法同样经过过滤。

每个槽位以 VirtualProtect 临时取得写权限（原页可执行时保留执行权限），InterlockedCompareExchangePointer 只在值仍等于预期原函数时改写，并恢复原页权限；首个错误保留，已持有槽位按反序回退，其他插件的指针不覆盖。完整组成功后才置现代位2；任何失败令所有拦截放行，不回退到猜测的新 API 或不完整握手保护。安装结束及每100毫秒核对缓存 interface、表与三槽位归属；变化就锁存错误、停用。原 DLL 与保存的函数指针保留至进程退出。

IPC ABI 升至2（Local\UNI2Blacklist-v2-PID），不能与旧 GUI/DLL 混用；升级需重启游戏。挂钩失败后映射仍有效时，重新打开同版本 GUI 可以只连接诊断状态，不能把“已连接”当成过滤成功。分组诊断的 native index0–4=search/join/request/chat/members，legacy index0–2=Send/Read/Accept（slot0/2/3），messages002 index即slot，未知值 UINT32_MAX。报告 memory_* 对应目标函数页，slot_memory_* 对应取得写权限前的表页；两者不能混读。MH 阶段的 minhook_status 是原 MinHook 返回值；验证拒绝使用合成 Win32 INVALID_ADDRESS/INVALID_DATA/RETRY，权限操作错误为 GetLastError 原值。cleanup_error 按该组 method 为 MinHook 状态或 Win32 状态，restore_error 总为页权限恢复的 Win32 状态。

## 场景与未完成证据

全局 `0x9a4764` 是原外层场景；既有原生分析中 1=Battle，12=Result，15=VS entry。读取不到场景时保守暂停拦截。此版没有改 battle 函数、GGPO 参数或录像计算。

仍需真实 Windows 实测确认：

1. 排位待机、玩家房间、重赛等模式是否全部经过已查 join/成员/聊天路径；有无另一条确认转换入口。
2. 忽略 entered 事件后房间计数、界面与自动搜索恢复时序；Steam 后端已加入玩家不能由本插件强踢，是否必须额外调用**经证实**的原游戏取消/重建房间函数。
3. 两套接口在不同菜单的初始化时间；GUI 待机期间是否都可达到安装状态3，是否需要在原接口初始化返回处同步安装来缩短控制线程的最多100毫秒窗口。
4. 场景1是否覆盖所有实际进行中的对战，后台消息线程是否有跨场景延后事件。
5. 原 room/search worker 生命周期与索引读者的同步，在快速取消、刷新、退出菜单时的表现。
6. 与现有 uniGGPO 注入器的入口和通信 hook 是否重叠。当前签名不匹配会拒绝加载，未做多插件共存验证。

以上未知不以自建进程检查代替真实游戏验收；不发布全量反编译文本或原二进制。

## candidate.3 生命周期补充

首次空名单只安装搜索入口（native index0）；native index1–4及两套通信组仅在启用、心跳有效、非空名单、非战斗后安装。仍用ABI2，但GUI/DLL必须同候选版本并重启游戏升级。network位0在观察模式正常，不代表错误；有效保护还要求非空名单。关闭/清空名单不卸载既有入口，直接委派原调用。

搜索捕获不再额外查询Steam姓名/Ping，当前元数据为未知；堆缓存与原函数调用分帧。GUI历史最多2048行、120秒，不影响原eligible索引生命周期。退出报告保留最后状态并显式 disconnected；新增独立本地64字节异常记录合同见 [快速匹配调查](QUICK_MATCH_CRASH.zh-CN.md)。
