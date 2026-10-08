# 0.1.0-candidate.6 验证范围

2026-10-08，现有环境、uniBlacklist/main。用户已确认搜索拉黑及candidate.5的姓名／估计延迟正常，但Wi-Fi列全部未知；本轮纠正发布者身份绑定。云端没有启动原游戏，不改游戏磁盘文件，不把自建控制当成真实对局验收。历史记录见 [candidate.4](VALIDATION_CANDIDATE4.zh-CN.md)、[candidate.5及语义更正](VALIDATION_CANDIDATE5.zh-CN.md)。

## 原生证据与隔离执行

- 新EXE SHA256 `4ebed985ecbf330ab8e495573361e49df20bb555263289d1aff5425fac9b7ed9`，Steam DLL SHA256 `67ae11d71ae6ec404090094df1e47b614d27400dc53fa450023e6fbcf347902c`。五个匹配签名、场景RVA0x5a4a84沿用；没有取消版本检查。
- REA/Ghidra12.1.4和x86指令核对姓名缓存、CP_ACP来源、网卡检测、排位与房间两种序列化、Steam估计接口及原信号表。
- Unicorn隔离执行原0x5f9e40／0x5f8aa0生产，再调用原0x5f9ac0／0x5f8610解析，共8组：两个格式分别Wi-Fi=0/1，前一个ID为0或与发布者不同。原排位输出132字节，发布者在0x7c、Wi-Fi在0x78；原房间输出274字节，长度前缀272，Base64解码202字节，发布者在0xc2、Wi-Fi在0xc0。解析读回发布者+0xa0／+0x240、Wi-Fi+0x144／+0x234，消费长度正确。标准内存与名字助手有自建桩，检查字段合同，不宣称整个游戏运行。
- 使用candidate.5旧解析器复现8组有效数据全部拒绝，4组错误接受前一个账号；之前两账号相同的控制漏检语义错误。[具体修正](WIFI_IDENTITY_FIX.zh-CN.md)。没有原EXE进程启动、真实账号或Steam网络会话。
- 自建Wine宿主实际加载配套Steam DLL，调用原GetLobbyData／ParsePingLocationString／EstimatePingTimeFromLocalHost导出；各flat wrapper经自建thiscall槽位返回64ms和缺缓存未知，验证完整lobby64与512字节输出ABI。它不是活体Steam接口验收。

## 产品与控制检查

MinGW-w64 GCC14，PE32/x86，静态运行库；`-Wall -Wextra -Werror`；固定MinHook源码指纹校验。临时控制只保存在忽略目录，不提交GitHub或放入ZIP。

| 范围 | 结果 | 实际覆盖 |
| --- | --- | --- |
| 既有原生控制 | 89项，进程退出0 | 候选压缩、握手、发送／接收与Release、现代表槽位和MinHook ABI、权限／归属失败及放行、空／关闭／心跳／战斗边界、异常环形日志及LastError；含1000次直接与1000次实际自建trampoline调用 |
| 新元数据与Wi-Fi控制 | 77项，进程退出0 | 原机器码生成记录的实际产品解析；两个格式0/1、前一个ID为0／不同、邻字段排除、末尾发布者绑定及错误owner拒绝、截断／非法值／Base64拒绝；缓存TTL／回绕／容量／冲突；无持久名单时真实候选及各通信入口的Wi-Fi拒绝、关闭放行和独立名单；130候选含GUI省略的2位均过滤；ACP名字；估计失败、缺接口、限频、跨槽拒绝、原Steam DLL wrapper与复制快照更新；单独配置文件、三种分类人数诊断 |
| GUI历史／策略／退出控制 | 41项，进程退出0 | 编入生产GUI代码，实际Win32列表与按钮；Wi-Fi列取代RTT／丢包、名字与64ms／有线显示；实际复选框写入共享exclude_wifi，保留名单；取消后标签解除；两分钟TTL、选择稳定、自动退出证据 |
| 最终GUI EXE跨进程操作 | 18项，进程退出0 | 实际产品窗口，默认Wi-Fi关闭、勾选单独落盘、名单增加／Unicode保存／移除、重启恢复开关与名单、取消规则、正常退出 |
| 最终DLL | 通过，进程退出0 | 实际产品LoadLibrary和导出，null／ABI2／非游戏宿主拒绝 |
| 包格式和清单 | 通过 | PE32、GUI subsystem、系统DLL依赖、内置SHA256清单、ZIP CRC／大小／文件数及源码无测试集／游戏素材 |

上述控制在Wine11/QEMU x86中运行。最终89项原生控制和最终DLL检查stderr各有67字节未归属的 `qemu: uncaught target signal 11 (Segmentation fault) - core dumped`，自建检查进程本身退出0；最终77项元数据、41项GUI历史控制和18项GUI操作stderr为空。各控制最终stderr及哈希逐项保留在package-receipt.json，不以检查通过消除环境异常，也不将其归因于用户游戏闪退。Wine图形截屏此前为黑屏，**控件操作不证明字体、DPI、像素绘制或原生Windows稳定性**。

## Windows发布门槛与边界

发布流程在Windows Server 2022 GitHub runner中先执行实际最终产品：启动GUI、找到自身窗口与复选框、验证默认关闭、实际点击、单独options.ini写入而无名单、正常关闭／重开恢复／取消保存。32位Windows PowerShell实际加载PE32 DLL并调用导出，验证null／ABI2／非游戏指纹拒绝。只有这些操作通过，后续发布任务才上传Release并重新下载核验。实际workflow结论与提交见Release页面及delivery-receipt.json；本文件不提前声称某次尚未执行的workflow通过。

这个Windows门槛不启动原游戏、不加载真实Steam对象、不验证实际匹配拒绝、未注入游戏，也不验收玩家名字显示、路由估计、画面／字体或DPI。用户的实际联机和最终界面效果仍需要本机测试。

## 本轮边界与使用复验

- 同新版EXE和未更新Steam DLL，升级须关闭旧GUI并重启游戏；ABI3 GUI／DLL成套使用。candidate.4／5下载保留。
- 首次无名单、Wi-Fi未勾选时只有搜索观察，hooks_ready=0与有效过滤0正常。新延迟后台限频读取缓存；搜索回调仍没有新增Steam SDK查询。
- 勾选后观察过的Wi-Fi账号等同于临时拉黑：候选和网络入口共享同一策略，不写blacklist.tsv。未知／冲突放行，未曾观察的被动来访账号不会凭猜测排除。账号缓存两分钟／4096，GUI历史两分钟／2048，单次GUI快照128，实际过滤上限4096。
- 名字来自游戏已经转换并截断的缓存，不能补回原来被游戏丢弃的字符。估计列是Steam路径估计，不是RTT；背景接口未就绪、缺属性或估计失败可继续显示未知。接口线程与关闭生命周期仍须真实Steam实测。
- 勾选／取消通常约100ms控制周期，仍受控制线程调度和缓存接口调用耗时影响；已发生的确认或握手不撤回，原Battle场景暂停所有规则。
- candidate.2旧崩溃的确切根因仍未知，排位并发取消／搜索对象生命周期、全部模式确认前拒绝、多插件共存等边界未完成真实游戏验证。发生退出时仍保留既有自动状态与最多8条异常日志。
- 细微图标差异尚未闭合原生映射；[分类与未知点](CONNECTION_STATES.zh-CN.md)只供分析，没有新增信号、FPS或抖动过滤规则。

下载SHA、大小、CRC、文件数见 downloads/0.1.0-candidate.6/package-receipt.json 和Release delivery-receipt.json。源码和包不包含临时控制、实际个人日志、名单、原游戏文件或凭据。
