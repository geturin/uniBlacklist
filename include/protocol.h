#pragma once
#include <windows.h>
#include <stdint.h>

constexpr uint32_t UB_ABI = 3;
constexpr char UB_VERSION[] = "0.1.0-candidate.6";
constexpr uint32_t UB_MAGIC = 0x31424e55;
constexpr uint32_t UB_MAX_BLOCKED = 256;
constexpr uint32_t UB_MAX_CANDIDATES = 128;
constexpr char UB_GAME_SHA[] = "4ebed985ecbf330ab8e495573361e49df20bb555263289d1aff5425fac9b7ed9";
constexpr char UB_STEAM_SHA[] = "67ae11d71ae6ec404090094df1e47b614d27400dc53fa450023e6fbcf347902c";
constexpr uint32_t UB_SCENE_RVA = 0x5a4a84;

enum UbState : uint32_t { UB_STARTING, UB_READY, UB_UNSUPPORTED, UB_ERROR };
enum UbStatus : uint32_t { UB_OK, UB_BAD_REQUEST, UB_WRONG_IMAGE, UB_SIGNATURE,
    UB_HOOK_FAILURE, UB_IPC_FAILURE };
enum UbCandidateFlags : uint32_t { UB_NAME_KNOWN=1, UB_PING_KNOWN=2,
    UB_BLOCKED=4, UB_ENDPOINT_KNOWN=8, UB_QUERY_TRUE=16, UB_WIFI_EXCLUDED=32 };
enum UbConnectionType : uint8_t { UB_CONNECTION_UNKNOWN, UB_CONNECTION_WIRED, UB_CONNECTION_WIFI };
enum UbHookMethod : uint32_t { UB_HOOK_NONE, UB_HOOK_MINHOOK, UB_HOOK_VTABLE };
enum UbHookStage : uint32_t { UB_STAGE_NONE, UB_STAGE_READY, UB_STAGE_CONTEXT,
    UB_STAGE_TARGET, UB_STAGE_MH_INIT, UB_STAGE_MH_CREATE, UB_STAGE_MH_QUEUE,
    UB_STAGE_MH_APPLY, UB_STAGE_PROTECT, UB_STAGE_EXCHANGE, UB_STAGE_RESTORE,
    UB_STAGE_OWNERSHIP };
inline const char* UbStageName(uint32_t stage) {
    constexpr const char* names[]={"not_attempted","ready","context","target",
        "minhook_initialize","minhook_create","minhook_queue","minhook_apply",
        "virtual_protect","compare_exchange","restore_protection","ownership"};
    return stage<sizeof(names)/sizeof(names[0])?names[stage]:"unknown";
}

#pragma pack(push, 8)
struct UbCandidate {
    uint64_t steam_id;
    uint64_t lobby_id;
    int32_t estimated_ping_ms;
    uint32_t flags;
    uint32_t observed_tick;
    uint32_t remote_ipv4_raw;
    uint16_t remote_port_raw;
    uint8_t relay;
    uint8_t active;
    char name_utf8[129];
    uint8_t connection_type;
    uint8_t reserved[2];
};
struct UbHookDiagnostic {
    uint32_t method, stage, index, slot;
    int32_t minhook_status;
    uint32_t win32_error, cleanup_error, restore_error;
    uint32_t interface_address, vtable_address, slot_address, target_address, observed_address;
    uint32_t memory_state, memory_type, memory_protect, module_rva;
    uint32_t slot_memory_state, slot_memory_type, slot_memory_protect;
    uint32_t code_bytes;
    uint8_t code_prefix[16];
    char module_utf8[96];
    uint32_t targets[5];
};
struct UbShared {
    uint32_t magic, abi, bytes, pid;
    uint32_t state, status, heartbeat, host_scene;
    uint32_t enable, policy_revision, policy_ack, blocked_count;
    uint64_t blocked[UB_MAX_BLOCKED];
    uint32_t refresh_revision, refresh_ack, capture_sequence, search_count;
    uint32_t candidate_count, omitted_count, metadata_errors, search_tick;
    uint32_t candidate_skips, request_rejects, send_rejects, receive_drops;
    uint32_t network_hooks_ready, battle_suspended, capture_pending, client_tick;
    uint32_t filter_active, effective_enabled;
    uint32_t exclude_wifi, wifi_cache_count, ping_queries, ping_known_count;
    UbHookDiagnostic hook_diagnostics[3]; // Native game, legacy P2P, Messages002.
    char message_utf8[256];
    UbCandidate candidates[UB_MAX_CANDIDATES];
};
struct UbStartRequest { uint32_t bytes, abi, status, reserved; };
#pragma pack(pop)
static_assert(sizeof(UbCandidate)==168, "stable PE32 IPC candidate layout");
static_assert(alignof(UbShared)>=8, "64-bit identity alignment");

inline void UbNames(DWORD pid, wchar_t* mapping, wchar_t* mutex) {
    wsprintfW(mapping, L"Local\\UNI2Blacklist-v3-%lu", static_cast<unsigned long>(pid));
    wsprintfW(mutex, L"Local\\UNI2Blacklist-v3-%lu-mutex", static_cast<unsigned long>(pid));
}
inline bool UbHasPolicy(const UbShared& s) { return s.blocked_count || s.exclude_wifi; }
inline bool UbEffective(const UbShared& s,DWORD now) {
    return s.effective_enabled && s.enable && UbHasPolicy(s) && s.state==UB_READY && s.status==UB_OK &&
        s.network_hooks_ready==3 && !s.battle_suspended &&
        s.policy_ack==s.policy_revision && DWORD(now-s.heartbeat)<=3000;
}
inline bool UbPlayerId(uint64_t id) {
    return (id & 0xffffffffu)!=0 && ((id>>52)&15)==1 && ((id>>56)&255)!=0;
}
