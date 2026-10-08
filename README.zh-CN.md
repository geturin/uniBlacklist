# UNI2 黑名单 / UNI2 Blacklist

Windows 黑名单插件，当前版本 `0.1.0-candidate.7`。显示游戏搜索到的玩家、估计延迟和连接类型，支持选中拉黑及排除 Wi-Fi 玩家。中文／English 可即时切换。

## 使用

从仓库 [Releases](https://github.com/geturin/uniBlacklist/releases) 下载对应的 `win32.zip`，完整解压后运行 `UNI2Blacklist.exe`，无需 Python。**升级时关闭旧窗口并重启游戏，EXE 和 DLL 使用同一包中的文件。**

1. 通过 Steam 正常启动游戏。在插件中点“刷新”，选择 `uni2.exe` 的 PID，再点“连接”。与游戏使用相同用户和权限。
2. “启用拦截”默认勾选。需要时勾选“排除 Wi-Fi”；没有名单且未排除 Wi-Fi 时只观察。未知类型放行，Wi-Fi 排除不加入永久名单。
3. 游戏搜索结果会显示在左侧。选中玩家后点“拉黑玩家”；在右侧黑名单中选中玩家点“解除拉黑”即可移除。最近出现的玩家保留 **120 秒**，方便选择。
4. 点“请求记录”查看跳过、拒绝、拦截、丢弃的累计次数及观察到的增量。关闭记录窗口继续过滤；“清空记录”只清显示。
5. 从语言选择框切换“中文”或“English”，窗口标题和界面立即切换，下次启动恢复选择。没有手动填写 SteamID64 的入口。

名单和选项保存在 `%LOCALAPPDATA%\UNI2Blacklist\`。已有名单沿用，Wi-Fi 开关与语言分别保存到 `options.ini`、`language.ini`；语言默认中文，Wi-Fi 排除默认关闭。界面设计和记录边界见 [GUI 说明](docs/GUI_REDESIGN.zh-CN.md)。

## 支持与边界

只接受以下游戏文件；其他版本拒绝连接。

| 文件 | SHA-256 |
| --- | --- |
| `uni2.exe`（6,921,216 字节，PE32） | `4ebed985ecbf330ab8e495573361e49df20bb555263289d1aff5425fac9b7ed9` |
| `steam_api.dll` | `67ae11d71ae6ec404090094df1e47b614d27400dc53fa450023e6fbcf347902c` |

插件在游戏进程中注入 DLL 并挂钩匹配及 Steam 通信入口，不改写游戏 EXE／Steam DLL 的磁盘文件，不修改防火墙、GGPO 或战斗计算。已有战斗暂停拦截，已发生的确认不会撤回。关闭主窗口停止过滤，已加载 DLL 保留到游戏退出；升级需要重启游戏。

玩家列表仍受游戏的搜索范围限制，历史行不表示玩家仍在待机。Wi-Fi 是游戏通告的网卡类型，仅排除观察过的账号；未观察到的被动来访账号和未知类型放行。估计延迟是 Steam 路径估计，**不是实测 RTT**。请求记录没有玩家／IP 归属，也不是网络丢包率。

数据只存本地，没有自动上传。游戏退出后自动诊断仍保留在配置目录的 `diagnostics` 下。真实联机、不同匹配模式、多插件共存及最终显示效果仍以本机使用为准。[验证范围](docs/VALIDATION.zh-CN.md) · [连接图标分类](docs/CONNECTION_STATES.zh-CN.md) · [Wi-Fi 修正证据](docs/WIFI_IDENTITY_FIX.zh-CN.md) · [原生接口合同](docs/NATIVE_CONTRACT.zh-CN.md)

## 构建

安装 Python 3.9+、32 位 MinGW-w64 GCC/G++ 和 windres：

```text
python build.py --cc i686-w64-mingw32-gcc --cxx i686-w64-mingw32-g++ --windres i686-w64-mingw32-windres
```

产物：`build/UNI2Blacklist-0.1.0-candidate.7-win32/`。界面语言切换不改变原生过滤协议。仓库和发布包不包含原游戏文件、资源、个人日志、名单、凭据或测试集。代码采用 MIT，MinHook 保留许可证与来源指纹。
