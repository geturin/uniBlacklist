#pragma once
#include <windows.h>
#include <stdint.h>

constexpr uint32_t UB_ABI = 1;
constexpr uint32_t UB_MAGIC = 0x31424e55;
constexpr uint32_t UB_MAX_BLOCKED = 256;
constexpr uint32_t UB_MAX_CANDIDATES = 128;
constexpr char UB_GAME_SHA[] = "cf64ec26e9646b68b46ed22c838b8f0778ab65abbeda0ebf164e81b5c08c9098";
constexpr char UB_STEAM_SHA[] = "67ae11d71ae6ec404090094df1e47b614d27400dc53fa450023e6fbcf347902c";

enum UbState : uint32_t { UB_STARTING, UB_READY, UB_UNSUPPORTED, UB_ERROR };
enum UbStatus : uint32_t { UB_OK, UB_BAD_REQUEST, UB_WRONG_IMAGE, UB_SIGNATURE,
    UB_HOOK_FAILURE, UB_IPC_FAILURE };
enum UbCandidateFlags : uint32_t { UB_NAME_KNOWN=1, UB_PING_KNOWN=2,
    UB_BLOCKED=4, UB_ENDPOINT_KNOWN=8, UB_QUERY_TRUE=16 };

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
    uint8_t reserved[3];
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
    char message_utf8[256];
    UbCandidate candidates[UB_MAX_CANDIDATES];
};
struct UbStartRequest { uint32_t bytes, abi, status, reserved; };
#pragma pack(pop)
static_assert(sizeof(UbCandidate)==168, "stable PE32 IPC candidate layout");
static_assert(alignof(UbShared)>=8, "64-bit identity alignment");

inline void UbNames(DWORD pid, wchar_t* mapping, wchar_t* mutex) {
    wsprintfW(mapping, L"Local\\UNI2Blacklist-v1-%lu", static_cast<unsigned long>(pid));
    wsprintfW(mutex, L"Local\\UNI2Blacklist-v1-%lu-mutex", static_cast<unsigned long>(pid));
}
inline bool UbPlayerId(uint64_t id) {
    return (id & 0xffffffffu)!=0 && ((id>>52)&15)==1 && ((id>>56)&255)!=0;
}
