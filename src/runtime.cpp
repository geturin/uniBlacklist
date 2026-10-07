#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#include <stdint.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <vector>
#include "protocol.h"
#include "profile.h"
#include "memory.h"
#include "MinHook.h"

static_assert(sizeof(void*)==4,"only x86 ABI supported");
namespace {
uintptr_t image=0;
HMODULE steam=nullptr;
UbShared* shared=nullptr;
HANDLE ipc_mutex=nullptr, mapping=nullptr;
volatile LONG started=0, ready=0;
volatile LONG network_mask=0;
SRWLOCK policy_lock=SRWLOCK_INIT;
uint64_t ids[UB_MAX_BLOCKED]{};
uint32_t id_count=0;
bool enabled=false;
DWORD client_tick=0;
volatile LONG skips=0,rejects=0,sends=0,drops=0,errors=0;
SRWLOCK capture_lock=SRWLOCK_INIT;
UbCandidate capture[UB_MAX_CANDIDATES]{};
uint32_t capture_count=0, omitted=0, search_count=0, search_tick=0;
bool captured=false;

bool lock_ipc(DWORD ms=10) {
    DWORD r=WaitForSingleObject(ipc_mutex,ms);
    return r==WAIT_OBJECT_0 || r==WAIT_ABANDONED;
}
void message(const char* s, UbState state, UbStatus status=UB_OK) {
    if (!shared || !lock_ipc()) return;
    shared->state=state;shared->status=status;
    strncpy(shared->message_utf8,s,sizeof(shared->message_utf8)-1);
    shared->message_utf8[sizeof(shared->message_utf8)-1]=0;
    ReleaseMutex(ipc_mutex);
}
bool battle() {
    uint32_t scene=0;
    // An unreadable scene is conservatively treated as active battle.
    return !UbGet(image+0x5a4764,scene) || scene==1;
}
bool blocked(uint64_t id) {
    if (!UbPlayerId(id) || InterlockedCompareExchange(&ready,0,0)!=1 || battle()) return false;
    AcquireSRWLockShared(&policy_lock);
    bool found=enabled && DWORD(GetTickCount()-client_tick)<10000 &&
        std::binary_search(ids,ids+id_count,id);
    ReleaseSRWLockShared(&policy_lock);
    return found;
}
void* context(uint32_t rva) {
    void* p=nullptr;
    UbGet(image+rva+8,p);
    return p;
}
template<class F> F api(const char* name) {
    return reinterpret_cast<F>(reinterpret_cast<uintptr_t>(GetProcAddress(steam,name)));
}
uint64_t identity(const void* p) {
    struct Prefix { int32_t type,size; uint64_t id; };
    Prefix q{};
    return UbGet(reinterpret_cast<uintptr_t>(p),q) && q.type==16 && q.size==8 && UbPlayerId(q.id) ? q.id : 0;
}
bool bounded_string(const char* p, char* out, size_t capacity) {
    if (!p || !capacity) return false;
    for (size_t i=0;i<capacity;i++) {
        if (!UbGet(reinterpret_cast<uintptr_t>(p)+i,out[i])) return false;
        if (!out[i]) return true;
    }
    out[0]=0;return false;
}
uint64_t decimal(const char* s) {
    uint64_t value=0;
    if (!s || !*s) return 0;
    for (size_t i=0;s[i];i++) {
        if (i>=20 || s[i]<'0' || s[i]>'9' || value>(UINT64_MAX-uint64_t(s[i]-'0'))/10) return 0;
        value=value*10+uint64_t(s[i]-'0');
    }
    return value;
}
using LobbyData=const char* (__cdecl*)(void*,uint64_t,const char*);
using LobbyOwner=uint64_t (__cdecl*)(void*,uint64_t);
using Persona=const char* (__cdecl*)(void*,uint64_t);
using ParsePing=bool (__cdecl*)(void*,const char*,void*);
using EstimatePing=int (__cdecl*)(void*,const void*);
using ReleaseMessage=void (__cdecl*)(void*);
LobbyData lobby_data=nullptr;
LobbyOwner lobby_owner=nullptr;
Persona persona=nullptr;
ParsePing parse_ping=nullptr;
EstimatePing estimate_ping=nullptr;
ReleaseMessage release_message=nullptr;
uint64_t owner(uint64_t lobby) {
    void* mm=context(0x5f9774);
    if (!mm || !lobby) return 0;
    char text[32]{};
    if (bounded_string(lobby_data(mm,lobby,"RoomPropertyKey_OwnerId"),text,sizeof(text))) {
        uint64_t id=decimal(text);
        if (UbPlayerId(id)) return id;
    }
    uint64_t id=lobby_owner(mm,lobby);
    return UbPlayerId(id)?id:0;
}
void metadata(UbCandidate& c) {
    c.estimated_ping_ms=-1;c.observed_tick=GetTickCount();
    void* friends=context(0x5f9780);
    if (friends && bounded_string(persona(friends,c.steam_id),c.name_utf8,sizeof(c.name_utf8))) {
        if (c.name_utf8[0] && strcmp(c.name_utf8,"[unknown]") &&
            MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,c.name_utf8,-1,nullptr,0)) c.flags|=UB_NAME_KNOWN;
        else c.name_utf8[0]=0;
    }
    void* mm=context(0x5f9774);void* utils=context(0x5f97a4);
    char text[1024]{};
    if (mm && utils && bounded_string(lobby_data(mm,c.lobby_id,"RoomPropertyKey_PingLocation"),text,sizeof(text)) && text[0]) {
        alignas(8) unsigned char location[512]{};
        if (parse_ping(utils,text,location)) {
            int ping=estimate_ping(utils,location);
            if (ping>=0) {c.estimated_ping_ms=ping;c.flags|=UB_PING_KNOWN;}
        }
    }
    if (blocked(c.steam_id)) c.flags|=UB_BLOCKED;
}

using Search=uint32_t (__thiscall*)(void*,void*);
using Join=uint32_t (__thiscall*)(void*,uint64_t);
using Callback=void (__thiscall*)(void*,void*);
Search original_search=nullptr;
Join original_join=nullptr;
Callback original_request=nullptr,original_chat=nullptr,original_members=nullptr;
uint32_t __fastcall hook_search(void* self,void*,void* list) {
    uint32_t result=original_search(self,list);
    DWORD saved=GetLastError();
    if (!(result&255)) {SetLastError(saved);return result;}
    const uintptr_t root=reinterpret_cast<uintptr_t>(self);
    uint32_t allocated=0,count=0,rows=0,index=0;
    if (!UbGet(root+0x2c,allocated) || !UbGet(root+0x30,rows) ||
        !UbGet(root+0x34,count) || !UbGet(root+0x38,index) || allocated>4096 ||
        count>allocated || (count && (!rows || !index))) {
        InterlockedIncrement(&errors);SetLastError(saved);return result;
    }
    std::vector<uint32_t> before(count),after;
    if (count && !UbRead(index,before.data(),count*4)) {
        InterlockedIncrement(&errors);SetLastError(saved);return result;
    }
    std::array<UbCandidate,UB_MAX_CANDIDATES> found{};
    uint32_t n=0,extra=0;
    bool valid=true;
    for (uint32_t slot:before) {
        if (slot>=allocated) {valid=false;break;}
        const uintptr_t row=uintptr_t(rows)+slot*0x5c;
        uint32_t player=0;uint64_t id=0;uint8_t good=0;char lobby[32]{};
        if (!UbGet(row+0x58,good) || !good || !UbGet(row+0x2c,player) ||
            !player || !UbGet(uintptr_t(player)+8,id) || !UbPlayerId(id) ||
            !bounded_string(reinterpret_cast<char*>(row),lobby,sizeof(lobby))) {valid=false;break;}
        UbCandidate c{};c.steam_id=id;c.lobby_id=decimal(lobby);
        if (!c.lobby_id) {valid=false;break;}
        metadata(c);
        if (n<UB_MAX_CANDIDATES) found[n++]=c;else ++extra;
        if (!blocked(id)) after.push_back(slot);
    }
    if (!valid) {InterlockedIncrement(&errors);SetLastError(saved);return result;}
    // Only the game's own search producer thread writes its cached index list,
    // before its original completion callback can choose a candidate.
    if (after.size()!=before.size()) {
        uint32_t new_count=static_cast<uint32_t>(after.size());
        if (UbWritable(root+0x34,4) && UbWritable(index,before.size()*4) &&
            (after.empty() || UbPut(index,after.data(),after.size()*4)) && UbSet(root+0x34,new_count)) {
            InterlockedExchangeAdd(&skips,static_cast<LONG>(before.size()-after.size()));
        } else {
            if (!before.empty()) UbPut(index,before.data(),before.size()*4);
            UbSet(root+0x34,count);InterlockedIncrement(&errors);
        }
    }
    AcquireSRWLockExclusive(&capture_lock);
    memcpy(capture,found.data(),sizeof(capture));capture_count=n;omitted=extra;
    ++search_count;search_tick=GetTickCount();captured=true;
    ReleaseSRWLockExclusive(&capture_lock);
    SetLastError(saved);return result;
}
uint32_t __fastcall hook_join(void* self,void*,uint64_t lobby) {
    DWORD saved=GetLastError();uint64_t id=owner(lobby);
    if (blocked(id)) {
        uint8_t zero=0;
        uintptr_t p=reinterpret_cast<uintptr_t>(self);
        // Same two flag writes as the original JoinLobby failure path.
        if (UbWritable(p+0x139,1) && UbWritable(p+0x13b,1) && UbSet(p+0x139,zero) && UbSet(p+0x13b,zero)) {
            InterlockedIncrement(&rejects);SetLastError(saved);return 0;
        }
        InterlockedIncrement(&errors);
    }
    SetLastError(saved);return original_join(self,lobby);
}
void __fastcall hook_request(void* self,void*,void* event) {
    DWORD saved=GetLastError();uint64_t id=0;
    if (UbGet(reinterpret_cast<uintptr_t>(event),id) && blocked(id)) {
        InterlockedIncrement(&rejects);SetLastError(saved);return;
    }
    SetLastError(saved);original_request(self,event);
}
void __fastcall hook_chat(void* self,void*,void* event) {
    DWORD saved=GetLastError();uint64_t id=0;
    if (UbGet(reinterpret_cast<uintptr_t>(event)+8,id) && blocked(id)) {
        InterlockedIncrement(&drops);SetLastError(saved);return;
    }
    SetLastError(saved);original_chat(self,event);
}
void __fastcall hook_members(void* self,void*,void* event) {
    DWORD saved=GetLastError();uint64_t id=0;uint32_t change=0;
    const uintptr_t p=reinterpret_cast<uintptr_t>(event);
    // Suppress the game-handler dispatch for a blocked new lobby member only.
    // Leave/disconnect/kick events must still reach native cleanup.
    if (UbGet(p+8,id) && UbGet(p+24,change) && change==1 && blocked(id)) {
        InterlockedIncrement(&rejects);SetLastError(saved);return;
    }
    SetLastError(saved);original_members(self,event);
}

using LegacySend=bool (__thiscall*)(void*,uint64_t,const void*,uint32_t,int,int);
using LegacyRead=bool (__thiscall*)(void*,void*,uint32_t,uint32_t*,uint64_t*,int);
using LegacyAccept=bool (__thiscall*)(void*,uint64_t);
using ModernSend=int (__thiscall*)(void*,const void*,const void*,uint32_t,int,int);
using ModernRead=int (__thiscall*)(void*,int,void**,int);
using ModernAccept=bool (__thiscall*)(void*,const void*);
LegacySend original_ls=nullptr;LegacyRead original_lr=nullptr;LegacyAccept original_la=nullptr;
ModernSend original_ms=nullptr;ModernRead original_mr=nullptr;ModernAccept original_ma=nullptr;
bool __fastcall hook_ls(void* self,void*,uint64_t id,const void* data,uint32_t len,int kind,int channel) {
    DWORD saved=GetLastError();
    if (blocked(id)) {InterlockedIncrement(&sends);SetLastError(saved);return false;}
    SetLastError(saved);return original_ls(self,id,data,len,kind,channel);
}
bool __fastcall hook_la(void* self,void*,uint64_t id) {
    DWORD saved=GetLastError();
    if (blocked(id)) {InterlockedIncrement(&rejects);SetLastError(saved);return false;}
    SetLastError(saved);return original_la(self,id);
}
bool __fastcall hook_lr(void* self,void*,void* data,uint32_t cap,uint32_t* size,uint64_t* peer,int channel) {
    for (int i=0;i<32;i++) {
        bool ok=original_lr(self,data,cap,size,peer,channel);DWORD saved=GetLastError();uint64_t id=0;
        if (!ok || !UbGet(reinterpret_cast<uintptr_t>(peer),id) || !blocked(id)) {SetLastError(saved);return ok;}
        InterlockedIncrement(&drops);
        uint32_t zero=0;uint64_t none=0;
        if (!UbSet(reinterpret_cast<uintptr_t>(size),zero) || !UbSet(reinterpret_cast<uintptr_t>(peer),none)) {
            InterlockedIncrement(&errors);SetLastError(saved);return false;
        }
        SetLastError(saved);
    }
    return false;
}
int __fastcall hook_ms(void* self,void*,const void* peer,const void* data,uint32_t len,int flags,int channel) {
    DWORD saved=GetLastError();
    if (blocked(identity(peer))) {InterlockedIncrement(&sends);SetLastError(saved);return 15;}// k_EResultAccessDenied
    SetLastError(saved);return original_ms(self,peer,data,len,flags,channel);
}
bool __fastcall hook_ma(void* self,void*,const void* peer) {
    DWORD saved=GetLastError();
    if (blocked(identity(peer))) {InterlockedIncrement(&rejects);SetLastError(saved);return false;}
    SetLastError(saved);return original_ma(self,peer);
}
int __fastcall hook_mr(void* self,void*,int channel,void** messages,int max) {
    int count=original_mr(self,channel,messages,max);DWORD saved=GetLastError();
    if (count<=0 || count>max || max>4096 || !UbWritable(reinterpret_cast<uintptr_t>(messages),count*4)) {
        SetLastError(saved);return count;
    }
    int out=0;
    for (int i=0;i<count;i++) {
        void* p=nullptr;
        UbGet(reinterpret_cast<uintptr_t>(messages+i),p);
        // SDK x86 message prefix: pData+0, cbSize+4, conn+8, identity+12.
        if (p && blocked(identity(reinterpret_cast<char*>(p)+12))) {
            release_message(p);InterlockedIncrement(&drops);
        } else messages[out++]=p;
    }
    for (int i=out;i<count;i++) messages[i]=nullptr;
    SetLastError(saved);return out;
}

std::string file_hash(HMODULE module) {
    wchar_t path[32768]{};
    DWORD n=GetModuleFileNameW(module,path,32768);
    if (!n || n>=32768) return {};
    HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (f==INVALID_HANDLE_VALUE) return {};
    HCRYPTPROV provider=0;HCRYPTHASH hash=0;
    bool ok=CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT) &&
        CryptCreateHash(provider,CALG_SHA_256,0,0,&hash);
    unsigned char buf[65536];DWORD done=0;
    while (ok) {
        ok=ReadFile(f,buf,sizeof(buf),&done,nullptr)!=FALSE;
        if (!ok || !done) break;
        ok=CryptHashData(hash,buf,done,0)!=FALSE;
    }
    unsigned char digest[32]{};DWORD length=32;
    ok=ok && CryptGetHashParam(hash,HP_HASHVAL,digest,&length,0) && length==32;
    if (hash) CryptDestroyHash(hash);
    if (provider) CryptReleaseContext(provider,0);
    CloseHandle(f);
    if (!ok) return {};
    std::string out;const char* hex="0123456789abcdef";
    for (auto b:digest) {out+=hex[b>>4];out+=hex[b&15];}return out;
}
bool signature(const UbSignature& s) {
    unsigned char bytes[32]{};
    if (!UbRead(image+s.rva,bytes,32)) return false;
    for (unsigned i=0;i<32;i++) if (s.mask[i] && bytes[i]!=s.bytes[i]) return false;
    return true;
}
struct Hook {void* target;void* detour;void** original;};
std::vector<void*> own_hooks;
bool install(std::vector<Hook>& hooks) {
    std::vector<void*> created;
    for (auto& h:hooks) {
        if (!UbExecutable(h.target) || MH_CreateHook(h.target,h.detour,h.original)!=MH_OK) goto fail;
        created.push_back(h.target);
    }
    for (void* p:created) if (MH_QueueEnableHook(p)!=MH_OK) goto fail;
    if (MH_ApplyQueued()!=MH_OK) goto fail;
    own_hooks.insert(own_hooks.end(),created.begin(),created.end());return true;
fail:
    for (void* p:created) {MH_DisableHook(p);MH_RemoveHook(p);}
    return false;
}
void* vfunction(void* iface,unsigned slot) {
    uintptr_t vt=0;void* fn=nullptr;
    if (UbGet(reinterpret_cast<uintptr_t>(iface),vt)) UbGet(vt+slot*4,fn);
    return fn;
}
bool install_network(bool modern) {
    void* iface=context(modern?0x5f9798:0x5a3c08);
    if (!iface) return false;
    std::vector<Hook> h={
        {vfunction(iface,0),modern?reinterpret_cast<void*>(hook_ms):reinterpret_cast<void*>(hook_ls),
            modern?reinterpret_cast<void**>(&original_ms):reinterpret_cast<void**>(&original_ls)},
        {vfunction(iface,modern?1:2),modern?reinterpret_cast<void*>(hook_mr):reinterpret_cast<void*>(hook_lr),
            modern?reinterpret_cast<void**>(&original_mr):reinterpret_cast<void**>(&original_lr)},
        {vfunction(iface,modern?2:3),modern?reinterpret_cast<void*>(hook_ma):reinterpret_cast<void*>(hook_la),
            modern?reinterpret_cast<void**>(&original_ma):reinterpret_cast<void**>(&original_la)}};
    if (!install(h)) {
        InterlockedExchange(&ready,0);
        message("通信挂钩安装失败；黑名单已停用，请保存诊断状态。",UB_ERROR,UB_HOOK_FAILURE);return false;
    }
    InterlockedOr(&network_mask,modern?2:1);
    if (network_mask==3) message("已接入：候选过滤、握手和双通信接口；请在本机验证匹配。",UB_READY);
    return true;
}
DWORD WINAPI worker(void*) {
    bool legacy_attempted=false,modern_attempted=false;
    for (;;) {
        if (!legacy_attempted && context(0x5a3c08)) {legacy_attempted=true;install_network(false);}
        if (!modern_attempted && context(0x5f9798)) {modern_attempted=true;install_network(true);}
        if (lock_ipc()) {
            AcquireSRWLockExclusive(&policy_lock);
            enabled=shared->enable!=0;client_tick=shared->client_tick;
            id_count=std::min(shared->blocked_count,UB_MAX_BLOCKED);
            memcpy(ids,shared->blocked,id_count*8);std::sort(ids,ids+id_count);
            ReleaseSRWLockExclusive(&policy_lock);
            shared->policy_ack=shared->policy_revision;
            shared->heartbeat=GetTickCount();UbGet(image+0x5a4764,shared->host_scene);
            shared->battle_suspended=battle();shared->network_hooks_ready=static_cast<uint32_t>(network_mask);
            shared->candidate_skips=skips;shared->request_rejects=rejects;
            shared->send_rejects=sends;shared->receive_drops=drops;shared->metadata_errors=errors;
            AcquireSRWLockExclusive(&capture_lock);
            if (captured) {
                memcpy(shared->candidates,capture,sizeof(capture));shared->candidate_count=capture_count;
                shared->omitted_count=omitted;shared->search_count=search_count;shared->search_tick=search_tick;
                ++shared->capture_sequence;captured=false;
            }
            ReleaseSRWLockExclusive(&capture_lock);
            ReleaseMutex(ipc_mutex);
        }
        Sleep(100);
    }
}
}

extern "C" __declspec(dllexport) DWORD WINAPI BlacklistStart(void* argument) {
    UbStartRequest request{};
    if (!UbGet(reinterpret_cast<uintptr_t>(argument),request) || request.bytes!=sizeof(request) || request.abi!=UB_ABI)
        return UB_BAD_REQUEST;
    auto finish=[&](UbStatus status) {request.status=status;UbSet(reinterpret_cast<uintptr_t>(argument),request);return DWORD(status);};
    if (InterlockedCompareExchange(&started,1,0)) return finish(shared?static_cast<UbStatus>(shared->status):UB_BAD_REQUEST);
    image=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));steam=GetModuleHandleW(L"steam_api.dll");
    if (!steam || file_hash(nullptr)!=UB_GAME_SHA || file_hash(steam)!=UB_STEAM_SHA) return finish(UB_WRONG_IMAGE);
    for (const auto* s:{&SIG_search_results,&SIG_join_lobby,&SIG_p2p_request,&SIG_lobby_chat,&SIG_lobby_members})
        if (!signature(*s)) return finish(UB_SIGNATURE);
    lobby_data=api<LobbyData>("SteamAPI_ISteamMatchmaking_GetLobbyData");
    lobby_owner=api<LobbyOwner>("SteamAPI_ISteamMatchmaking_GetLobbyOwner");
    persona=api<Persona>("SteamAPI_ISteamFriends_GetFriendPersonaName");
    parse_ping=api<ParsePing>("SteamAPI_ISteamNetworkingUtils_ParsePingLocationString");
    estimate_ping=api<EstimatePing>("SteamAPI_ISteamNetworkingUtils_EstimatePingTimeFromLocalHost");
    release_message=api<ReleaseMessage>("SteamAPI_SteamNetworkingMessage_t_Release");
    if (!lobby_data || !lobby_owner || !persona || !parse_ping || !estimate_ping || !release_message) return finish(UB_WRONG_IMAGE);
    wchar_t map_name[128],mutex_name[128];UbNames(GetCurrentProcessId(),map_name,mutex_name);
    ipc_mutex=CreateMutexW(nullptr,FALSE,mutex_name);
    mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(UbShared),map_name);
    if (!ipc_mutex || !mapping) return finish(UB_IPC_FAILURE);
    shared=static_cast<UbShared*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(UbShared)));
    if (!shared || !lock_ipc(1000)) return finish(UB_IPC_FAILURE);
    memset(shared,0,sizeof(*shared));shared->magic=UB_MAGIC;shared->abi=UB_ABI;
    shared->bytes=sizeof(*shared);shared->pid=GetCurrentProcessId();ReleaseMutex(ipc_mutex);
    message("候选和房间过滤启动中；等待游戏初始化通信接口，握手保护尚未齐全。",UB_STARTING);
    HMODULE retained=nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&BlacklistStart),&retained);
    if (MH_Initialize()!=MH_OK) {message("无法初始化挂钩。",UB_ERROR,UB_HOOK_FAILURE);return finish(UB_HOOK_FAILURE);}
    std::vector<Hook> h={
        {reinterpret_cast<void*>(image+SIG_search_results.rva),reinterpret_cast<void*>(hook_search),reinterpret_cast<void**>(&original_search)},
        {reinterpret_cast<void*>(image+SIG_join_lobby.rva),reinterpret_cast<void*>(hook_join),reinterpret_cast<void**>(&original_join)},
        {reinterpret_cast<void*>(image+SIG_p2p_request.rva),reinterpret_cast<void*>(hook_request),reinterpret_cast<void**>(&original_request)},
        {reinterpret_cast<void*>(image+SIG_lobby_chat.rva),reinterpret_cast<void*>(hook_chat),reinterpret_cast<void**>(&original_chat)},
        {reinterpret_cast<void*>(image+SIG_lobby_members.rva),reinterpret_cast<void*>(hook_members),reinterpret_cast<void**>(&original_members)}};
    if (!install(h)) {message("原生入口挂钩失败；黑名单未启用。",UB_ERROR,UB_HOOK_FAILURE);return finish(UB_HOOK_FAILURE);}
    InterlockedExchange(&ready,1);
    HANDLE thread=CreateThread(nullptr,0,worker,nullptr,0,nullptr);
    if (!thread) {message("无法启动黑名单控制线程。",UB_ERROR,UB_IPC_FAILURE);return finish(UB_IPC_FAILURE);}
    CloseHandle(thread);return finish(UB_OK);
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) {
    if (reason==DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(module);
    return TRUE;
}
