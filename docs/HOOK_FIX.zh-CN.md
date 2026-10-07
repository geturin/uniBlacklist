# candidate.2 通信挂钩修复

用户的 candidate.1 原生 Windows 报告为 state3/status4/hooks_ready1、search_count6/candidate_count1。这证明旧通信组安装成功、原游戏搜索结果已被读取；现代通信组失败，ready 已清零，enabled1 只是 GUI 请求状态。报告没有精确失败阶段，不能由此断言缺 DLL、权限不足、Steam 版本不对或 MinHook 的某个具体错误。云端已有相同 EXE 与 steam_api.dll 指纹，无需重复提供这两份文件。

## 代码变化

- Messages002 不再交给 MinHook 改写动态方法入口，改为替换既有表的三个指针。它仍调用原接口和原函数，拒绝策略、thiscall ABI 与 Release 路径不改。
- 保留完整原表地址、RTTI、其他公开及内部方法。没有只复制六个公开槽位的缩短表，也没有猜测具体类大小。可执行运行时 thunk 可来自非 MEM_IMAGE 内存。
- 原函数先保存，CAS 校验槽位值，检查取得写权限和恢复权限；部分失败回退已安装槽位，保留其他插件的改动。恢复仍失败会记录 restore_error/cleanup_error，不声称恢复成功。
- 安装后核对指针，随后周期核对缓存和槽位归属。失败锁存、全部过滤放行，不把后续接口安装成功覆盖成 READY。模块与原函数保留至游戏退出。
- 原游戏五入口及旧通信仍用既有 MinHook，新增具体 preflight/create/queue/apply 错误及清理错误。
- 开关请求与过滤/完整保护生效分开；心跳过期、策略未确认、对战暂停、接口未齐全、失败均不能显示完整保护有效。挂钩失败后的同版本 GUI 重连仍可打开有效映射读取诊断。
- ABI2 与候选版版本标记同步更新。旧 DLL 留在游戏进程时明确要求重启；磁盘游戏文件不修改。

## 新报告的判断入口

`messages002.method` 应为 vtable_slot；成功时 stage=ready、hooks_ready=3。开启且不在对战、策略已确认及心跳有效时 effective_enabled=1；空名单依然不会产生拒绝。失败时查看 stage、slot、target、module/module_rva、memory_type/protect（原函数代码）与 slot_memory_type/protect（被改写的表页）、win32_error 和 restore_error。原生/旧组还要查看 minhook_status_name。报告不输出玩家账号、名单内容或 IP，不自动上传。

candidate.2 是否解决该用户 Steam 运行时故障必须用新包实际连接确认。本轮没有启动原游戏，没有在真实 Steam 客户端中实测。双通信齐全也不证明所有模式均在确认界面前拒绝，原房间后端占位、场景覆盖和多插件兼容仍需独立验证；参见验证范围。
