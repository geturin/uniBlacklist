# UNI2Blacklist

Native Windows GUI and injected blacklist plugin for UNI2, keyed by SteamID64.

See [中文使用说明](README.zh-CN.md), [native contracts](docs/NATIVE_CONTRACT.zh-CN.md) and [validation scope](docs/VALIDATION.zh-CN.md).

Candidate.2 replaces Messages002 inline hooks with atomic swaps of the three existing vtable slots and adds exact installation failure diagnostics and effective protection status. Candidate.1 produced real search results on the user’s Windows machine but its modern networking hook failed. The old report cannot identify the exact failure cause; the new Steam-client path still requires the user’s native Windows verification. Download the portable win32 ZIP from [Releases](https://github.com/geturin/uniBlacklist/releases). It does not modify the game executable on disk.
