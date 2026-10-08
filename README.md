# UNI2Blacklist

Native Windows GUI and injected DLL for SteamID64 matchmaking filtering in UNI2. [中文使用说明](README.zh-CN.md).

**0.1.0-candidate.3 is a diagnostic candidate for the user's reproducible empty-blacklist quick-match crash, not a confirmed native Windows crash fix.** Fresh empty-blacklist observation installs only the search hook; room/network hooks are deferred until an enabled, live, nonempty policy. Search snapshots use heap storage outside the original-call frame and make no additional Steam metadata queries; names and latency temporarily remain unknown. Candidate rows retain their last observation for two minutes with stable selection. Exit counters and code survive game exit, with a local bounded first-chance fault trace and automatic status export.

Download the portable win32 ZIP from [Releases](https://github.com/geturin/uniBlacklist/releases). Close the old GUI and restart the game before upgrading. The original EXE/DLL files remain untouched. Cloud verification uses self-owned controls under Wine/QEMU; actual game quick-match stability and blacklist rejection still require the user's native Windows test. See [crash investigation](docs/QUICK_MATCH_CRASH.zh-CN.md) and [validation](docs/VALIDATION.zh-CN.md). No original game files, personal logs, credentials or test suite are published.
