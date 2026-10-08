# UNI2Blacklist

Native Windows GUI and injected DLL for SteamID64 matchmaking filtering in UNI2. [中文使用说明](README.zh-CN.md).

**0.1.0-candidate.4 targets the uploaded updated EXE (SHA-256 4ebed985…9b7ed9), with scene RVA 0x5A4A84 instead of the old 0x5A4764. This is not a confirmed native Windows quick-match crash fix.** Fresh empty-blacklist observation installs only the search hook; room/network hooks are deferred until an enabled, live, nonempty policy. Search snapshots use heap storage outside the original-call frame and make no additional Steam metadata queries; names and latency temporarily remain unknown. Candidate rows retain their last observation for two minutes with stable selection. Exit counters and code survive game exit, with a local bounded first-chance fault trace and automatic status export.

Download the portable win32 ZIP from [Releases](https://github.com/geturin/uniBlacklist/releases). Close the old GUI and restart the game before upgrading. The original EXE/DLL files remain untouched. Cloud verification uses self-owned controls under Wine/QEMU; actual game quick-match stability and blacklist rejection still require the user's native Windows test. See [crash investigation](docs/QUICK_MATCH_CRASH.zh-CN.md) and [validation](docs/VALIDATION.zh-CN.md). No original game files, personal logs, credentials or test suite are published.

The five matchmaking entry points retain their addresses; the updated raw signatures and 325 scene references were checked statically. The user confirms steam_api.dll is unchanged, so its existing fingerprint remains enforced. See [EXE update evidence](docs/EXE_UPDATE.zh-CN.md). Earlier releases remain available.
