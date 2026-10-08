#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#include <stdint.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include "protocol.h"
#include "profile.h"
#include "memory.h"
#include "fault_trace.h"
#include "MinHook.h"

static_assert(sizeof(void*)==4,"only x86 ABI supported");
namespace {
uintptr_t image=0;
HMODULE steam=nullptr;
UbShared* shared=nullptr;
HANDLE ipc_mutex=nullptr, mapping=nullptr;
volatile LONG started=0, ready=0;
volatile LONG network_mask=0;
bool network_failed=false;
bool native_policy_ready=false;
UbHookDiagnostic hook_diagnostics[3]{};
UbState runtime_state=UB_STARTING;
UbStatus runtime_status=UB_OK;
char runtime_message[256]{};
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
    runtime_state=state;runtime_status=status;
    strncpy(runtime_message,s,sizeof(runtime_message)-1);runtime_message[sizeof(runtime_message)-1]=0;
    if (!shared || !lock_ipc()) return;
    shared->state=state;shared->status=status;
    if (state==UB_ERROR) {shared->filter_active=0;shared->effective_enabled=0;}
    memcpy(shared->hook_diagnostics,hook_diagnostics,sizeof(hook_diagnostics));
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
LobbyData lobby_data=nullptr;
LobbyOwner lobby_owner=nullptr;
using ReleaseMessage=void (__cdecl*)(void*);
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
// Search observations deliberately make no additional Steam SDK calls.
// Names and ping remain unknown until their safe source is established.
void metadata(UbCandidate& c) {
    c.estimated_ping_ms=-1;c.observed_tick=GetTickCount();
    if (blocked(c.steam_id)) c.flags|=UB_BLOCKED;
}

bool policy_requested() {
    DWORD saved=GetLastError();
    AcquireSRWLockShared(&policy_lock);
    bool active=enabled && id_count!=0 && DWORD(GetTickCount()-client_tick)<10000;
    ReleaseSRWLockShared(&policy_lock);
    SetLastError(saved);return active;
}
bool filtering() {
    DWORD saved=GetLastError();
    bool active=policy_requested() && InterlockedCompareExchange(&ready,0,0)==1 && !battle();
    SetLastError(saved);return active;
}

using Search=uint32_t (__thiscall*)(void*,void*);
using Join=uint32_t (__thiscall*)(void*,uint64_t);
using Callback=void (__thiscall*)(void*,void*);
Search original_search=nullptr;
Join original_join=nullptr;
Callback original_request=nullptr,original_chat=nullptr,original_members=nullptr;
// Keep this allocation-heavy path out of the frame that calls the game.
// The previous hook reserved over 23 KiB before entering original_search.
__attribute__((noinline)) void capture_search(void* self) {
    UbTraceScope trace(UB_TRACE_SEARCH_CAPTURE);
    const uintptr_t root=reinterpret_cast<uintptr_t>(self);
    uint32_t allocated=0,count=0,rows=0,index=0;
    if (!UbGet(root+0x2c,allocated) || !UbGet(root+0x30,rows) ||
        !UbGet(root+0x34,count) || !UbGet(root+0x38,index) || allocated>4096 ||
        count>allocated || (count && (!rows || !index))) {
        InterlockedIncrement(&errors);return;
    }
    std::vector<uint32_t> before(count),after;
    if (count && !UbRead(index,before.data(),count*4)) {
        InterlockedIncrement(&errors);return;
    }
    std::vector<UbCandidate> found(UB_MAX_CANDIDATES);
    after.reserve(count);
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
    if (!valid) {InterlockedIncrement(&errors);return;}
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
    ++search_count;InterlockedExchange(&UbFaultRuntime::search_count,search_count);search_tick=GetTickCount();captured=true;
    ReleaseSRWLockExclusive(&capture_lock);
}
uint32_t __fastcall hook_search(void* self,void*,void* list) {
    uint32_t result;
    {UbTraceScope trace(UB_TRACE_SEARCH_ORIGINAL);result=original_search(self,list);}
    DWORD saved=GetLastError();
    if (result&255) {
        // Catch only our C++ allocation failures, never swallow native SEH.
        try {capture_search(self);} catch(...) {InterlockedIncrement(&errors);}
    }
    SetLastError(saved);return result;
}
uint32_t __fastcall hook_join(void* self,void*,uint64_t lobby) {
    if (!filtering()) return original_join(self,lobby);
    UbTraceScope trace(UB_TRACE_JOIN);
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
    if (!filtering()) {original_request(self,event);return;}
    UbTraceScope trace(UB_TRACE_CALLBACK);
    DWORD saved=GetLastError();uint64_t id=0;
    if (UbGet(reinterpret_cast<uintptr_t>(event),id) && blocked(id)) {
        InterlockedIncrement(&rejects);SetLastError(saved);return;
    }
    SetLastError(saved);original_request(self,event);
}
void __fastcall hook_chat(void* self,void*,void* event) {
    if (!filtering()) {original_chat(self,event);return;}
    UbTraceScope trace(UB_TRACE_CALLBACK);
    DWORD saved=GetLastError();uint64_t id=0;
    if (UbGet(reinterpret_cast<uintptr_t>(event)+8,id) && blocked(id)) {
        InterlockedIncrement(&drops);SetLastError(saved);return;
    }
    SetLastError(saved);original_chat(self,event);
}
void __fastcall hook_members(void* self,void*,void* event) {
    if (!filtering()) {original_members(self,event);return;}
    UbTraceScope trace(UB_TRACE_CALLBACK);
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
    if (!filtering()) return original_ls(self,id,data,len,kind,channel);
    UbTraceScope trace(UB_TRACE_NETWORK);
    DWORD saved=GetLastError();
    if (blocked(id)) {InterlockedIncrement(&sends);SetLastError(saved);return false;}
    SetLastError(saved);return original_ls(self,id,data,len,kind,channel);
}
bool __fastcall hook_la(void* self,void*,uint64_t id) {
    if (!filtering()) return original_la(self,id);
    UbTraceScope trace(UB_TRACE_NETWORK);
    DWORD saved=GetLastError();
    if (blocked(id)) {InterlockedIncrement(&rejects);SetLastError(saved);return false;}
    SetLastError(saved);return original_la(self,id);
}
bool __fastcall hook_lr(void* self,void*,void* data,uint32_t cap,uint32_t* size,uint64_t* peer,int channel) {
    if (!filtering()) return original_lr(self,data,cap,size,peer,channel);
    UbTraceScope trace(UB_TRACE_NETWORK);
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
    if (!filtering()) return original_ms(self,peer,data,len,flags,channel);
    UbTraceScope trace(UB_TRACE_NETWORK);
    DWORD saved=GetLastError();
    if (blocked(identity(peer))) {InterlockedIncrement(&sends);SetLastError(saved);return 15;}// k_EResultAccessDenied
    SetLastError(saved);return original_ms(self,peer,data,len,flags,channel);
}
bool __fastcall hook_ma(void* self,void*,const void* peer) {
    if (!filtering()) return original_ma(self,peer);
    UbTraceScope trace(UB_TRACE_NETWORK);
    DWORD saved=GetLastError();
    if (blocked(identity(peer))) {InterlockedIncrement(&rejects);SetLastError(saved);return false;}
    SetLastError(saved);return original_ma(self,peer);
}
int __fastcall hook_mr(void* self,void*,int channel,void** messages,int max) {
    if (!filtering()) return original_mr(self,channel,messages,max);
    UbTraceScope trace(UB_TRACE_NETWORK);
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
void target_info(UbHookDiagnostic& d,void* target) {
    DWORD saved=GetLastError();
    d.target_address=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target));
    d.memory_state=d.memory_type=d.memory_protect=d.module_rva=d.code_bytes=0;
    memset(d.code_prefix,0,sizeof(d.code_prefix));memset(d.module_utf8,0,sizeof(d.module_utf8));
    MEMORY_BASIC_INFORMATION m{};
    if (target && VirtualQuery(target,&m,sizeof(m))) {
        d.memory_state=m.State;d.memory_type=m.Type;d.memory_protect=m.Protect;
        uintptr_t end=reinterpret_cast<uintptr_t>(m.BaseAddress)+m.RegionSize;
        if (end>reinterpret_cast<uintptr_t>(target)) {
            size_t bytes=std::min<size_t>(sizeof(d.code_prefix),end-reinterpret_cast<uintptr_t>(target));
            if (UbRead(reinterpret_cast<uintptr_t>(target),d.code_prefix,bytes)) d.code_bytes=static_cast<uint32_t>(bytes);
        }
        HMODULE module=nullptr;
        if (m.Type==MEM_IMAGE && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(target),&module)) {
            wchar_t path[32768]{};DWORD n=GetModuleFileNameW(module,path,32768);
            if (n && n<32768) {
                const wchar_t* name=wcsrchr(path,L'\\');name=name?name+1:path;
                WideCharToMultiByte(CP_UTF8,0,name,-1,d.module_utf8,sizeof(d.module_utf8),nullptr,nullptr);
                d.module_utf8[sizeof(d.module_utf8)-1]=0;
                d.module_rva=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target)-reinterpret_cast<uintptr_t>(module));
            }
        }
    }
    SetLastError(saved);
}
void reset_diagnostic(UbHookDiagnostic& d,UbHookMethod method) {
    d={};d.method=method;d.slot=UINT32_MAX;d.index=UINT32_MAX;
}
bool install(std::vector<Hook>& hooks,UbHookDiagnostic& d,size_t base=0) {
    UbTraceScope trace(UB_TRACE_INSTALL);
    std::vector<void*> created;
    for (size_t i=0;i<hooks.size();i++) {
        auto& h=hooks[i];d.index=static_cast<uint32_t>(i+base);
        if (i+base<5) d.targets[i+base]=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(h.target));
        if (d.interface_address) {
            constexpr unsigned slots[]={0,2,3};d.slot=slots[i];
            d.slot_address=d.vtable_address+d.slot*4;
        }
        target_info(d,h.target);d.stage=UB_STAGE_TARGET;
        if (!UbExecutable(h.target)) {d.win32_error=ERROR_INVALID_ADDRESS;goto fail;}
        d.stage=UB_STAGE_MH_CREATE;
        d.minhook_status=MH_CreateHook(h.target,h.detour,h.original);
        if (d.minhook_status!=MH_OK) goto fail;
        created.push_back(h.target);
    }
    for (size_t i=0;i<created.size();i++) {
        d.index=static_cast<uint32_t>(i+base);target_info(d,created[i]);
        if (d.interface_address) {constexpr unsigned slots[]={0,2,3};d.slot=slots[i];d.slot_address=d.vtable_address+d.slot*4;}
        d.stage=UB_STAGE_MH_QUEUE;d.minhook_status=MH_QueueEnableHook(created[i]);
        if (d.minhook_status!=MH_OK) goto fail;
    }
    d.stage=UB_STAGE_MH_APPLY;d.minhook_status=MH_ApplyQueued();
    if (d.minhook_status!=MH_OK) goto fail;
    d.stage=UB_STAGE_READY;
    own_hooks.insert(own_hooks.end(),created.begin(),created.end());return true;
fail:
    for (void* p:created) {
        MH_STATUS s=MH_DisableHook(p);
        if (s!=MH_OK && s!=MH_ERROR_DISABLED && !d.cleanup_error) d.cleanup_error=static_cast<uint32_t>(s);
        s=MH_RemoveHook(p);
        if (s!=MH_OK && !d.cleanup_error) d.cleanup_error=static_cast<uint32_t>(s);
    }
    return false;
}
void* vfunction(void* iface,unsigned slot) {
    uintptr_t vt=0;void* fn=nullptr;
    if (UbGet(reinterpret_cast<uintptr_t>(iface),vt)) UbGet(vt+slot*4,fn);
    return fn;
}
struct VtableSlot {
    uintptr_t address=0;
    void* original=nullptr;
    void* detour=nullptr;
    DWORD original_protect=0;
    bool owned=false;
};
std::array<VtableSlot,3> modern_slots{};
uintptr_t modern_interface=0,modern_vtable=0;

// Only replace existing slots. Do not clone/truncate the SDK's concrete vtable:
// RTTI, private methods and every unmodified slot must keep their original layout.
bool exchange_slot(VtableSlot& s,bool installing,UbHookDiagnostic& d) {
    d.slot_address=static_cast<uint32_t>(s.address);target_info(d,s.original);
    MEMORY_BASIC_INFORMATION m{};
    d.slot_memory_state=d.slot_memory_type=d.slot_memory_protect=0;
    d.stage=UB_STAGE_PROTECT;
    if (!s.address || (s.address&3) || !VirtualQuery(reinterpret_cast<void*>(s.address),&m,sizeof(m))) {
        d.win32_error=ERROR_INVALID_ADDRESS;return false;
    }
    d.slot_memory_state=m.State;d.slot_memory_type=m.Type;d.slot_memory_protect=m.Protect;
    if (m.State!=MEM_COMMIT || (m.Protect&(PAGE_GUARD|PAGE_NOACCESS))) {d.win32_error=ERROR_INVALID_ADDRESS;return false;}
    bool executable=(m.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))!=0;
    DWORD before=0;
    if (!VirtualProtect(reinterpret_cast<void*>(s.address),sizeof(void*),
            executable?PAGE_EXECUTE_READWRITE:PAGE_READWRITE,&before)) {
        d.win32_error=GetLastError();return false;
    }
    if (installing) s.original_protect=before;
    void* expected=installing?s.original:s.detour;
    void* desired=installing?s.detour:s.original;
    d.stage=UB_STAGE_EXCHANGE;
    void* observed=InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(s.address),desired,expected);
    d.observed_address=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(observed));
    bool swapped=observed==expected || (!installing && observed==desired);
    if (observed==expected) s.owned=installing;
    else if (!installing) s.owned=false; // Another plugin's value is never overwritten.
    if (!swapped) d.win32_error=ERROR_RETRY;
    DWORD ignored=0;
    DWORD restore_to=installing?before:s.original_protect;
    if (!VirtualProtect(reinterpret_cast<void*>(s.address),sizeof(void*),restore_to,&ignored)) {
        d.restore_error=GetLastError();
        if (swapped) {d.stage=UB_STAGE_RESTORE;d.win32_error=d.restore_error;}
        return false;
    }
    return swapped;
}
void rollback_modern(UbHookDiagnostic& d) {
    for (auto it=modern_slots.rbegin();it!=modern_slots.rend();++it) {
        if (!it->owned) continue;
        UbHookDiagnostic cleanup{};
        if (!exchange_slot(*it,false,cleanup) && !d.cleanup_error) d.cleanup_error=cleanup.win32_error;
        if (cleanup.restore_error && !d.restore_error) d.restore_error=cleanup.restore_error;
    }
}
bool install_modern(void* iface,UbHookDiagnostic& d) {
    d.interface_address=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(iface));
    uintptr_t vt=0;d.stage=UB_STAGE_CONTEXT;
    if (!UbGet(reinterpret_cast<uintptr_t>(iface),vt) || !vt || (vt&3) || vt>UINTPTR_MAX-24) {
        d.win32_error=ERROR_INVALID_ADDRESS;return false;
    }
    d.vtable_address=static_cast<uint32_t>(vt);
    void* functions[6]{};
    if (!UbRead(vt,functions,sizeof(functions))) {d.win32_error=ERROR_INVALID_ADDRESS;return false;}
    // The fixed Messages002 contract has six public methods. The concrete table
    // may have additional entries; they are neither read nor modified.
    for (unsigned i=0;i<6;i++) {
        d.slot=d.index=i;d.slot_address=static_cast<uint32_t>(vt+i*4);
        target_info(d,functions[i]);d.stage=UB_STAGE_TARGET;
        if (i<3) d.targets[i]=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(functions[i]));
        // Steam can return executable runtime thunks outside MEM_IMAGE. Pointer
        // hooks don't disassemble or patch that code and can preserve those thunks.
        if (!UbExecutable(functions[i],false)) {d.win32_error=ERROR_INVALID_ADDRESS;return false;}
    }
    original_ms=reinterpret_cast<ModernSend>(functions[0]);
    original_mr=reinterpret_cast<ModernRead>(functions[1]);
    original_ma=reinterpret_cast<ModernAccept>(functions[2]);
    void* detours[]={reinterpret_cast<void*>(hook_ms),reinterpret_cast<void*>(hook_mr),reinterpret_cast<void*>(hook_ma)};
    for (unsigned i=0;i<3;i++) modern_slots[i]={vt+i*4,functions[i],detours[i],0,false};
    modern_interface=reinterpret_cast<uintptr_t>(iface);modern_vtable=vt;
    // Originals are published before any detour. Atomic CAS validates ownership.
    for (unsigned i:{0u,2u,1u}) {
        d.slot=d.index=i;
        if (!exchange_slot(modern_slots[i],true,d)) {rollback_modern(d);return false;}
    }
    uintptr_t current_vtable=0;
    if (context(0x5f9798)!=iface || !UbGet(reinterpret_cast<uintptr_t>(iface),current_vtable) || current_vtable!=vt) {
        d.stage=UB_STAGE_OWNERSHIP;d.index=d.slot=UINT32_MAX;
        d.observed_address=static_cast<uint32_t>(current_vtable);d.win32_error=ERROR_INVALID_ADDRESS;
        rollback_modern(d);return false;
    }
    for (unsigned i=0;i<3;i++) {
        void* value=nullptr;
        if (!UbGet(modern_slots[i].address,value) || value!=modern_slots[i].detour) {
            d.stage=UB_STAGE_OWNERSHIP;d.index=d.slot=i;d.slot_address=static_cast<uint32_t>(modern_slots[i].address);
            target_info(d,modern_slots[i].original);
            d.observed_address=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(value));d.win32_error=ERROR_RETRY;
            rollback_modern(d);return false;
        }
    }
    d.stage=UB_STAGE_READY;return true;
}
void network_failure(bool modern) {
    InterlockedExchange(&ready,0);network_failed=true;
    const auto& d=hook_diagnostics[modern?2:1];
    char text[256]{};
    std::snprintf(text,sizeof(text),"%s 挂钩失败（%s，MH=%ld，Win32=%lu）；拦截已停用，请保存诊断状态。",
        modern?"Messages002":"旧 P2P",UbStageName(d.stage),static_cast<long>(d.minhook_status),
        static_cast<unsigned long>(d.win32_error));
    message(text,UB_ERROR,UB_HOOK_FAILURE);
}
bool install_network(bool modern) {
    if (network_failed || !policy_requested() || battle()) return false;
    void* iface=context(modern?0x5f9798:0x5a3c08);
    if (!iface) return false;
    auto& d=hook_diagnostics[modern?2:1];reset_diagnostic(d,modern?UB_HOOK_VTABLE:UB_HOOK_MINHOOK);
    UbTraceScope trace(UB_TRACE_INSTALL);
    bool ok=false;
    if (modern) {
        LONG previous=InterlockedExchange(&ready,0);
        ok=install_modern(iface,d);
        if (ok) InterlockedExchange(&ready,previous);
    } else {
        d.interface_address=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(iface));
        d.stage=UB_STAGE_CONTEXT;
        uintptr_t vt=0;
        if (UbGet(reinterpret_cast<uintptr_t>(iface),vt) && vt && !(vt&3) && vt<=UINTPTR_MAX-16) {
            d.vtable_address=static_cast<uint32_t>(vt);
            std::vector<Hook> h={
                {vfunction(iface,0),reinterpret_cast<void*>(hook_ls),reinterpret_cast<void**>(&original_ls)},
                {vfunction(iface,2),reinterpret_cast<void*>(hook_lr),reinterpret_cast<void**>(&original_lr)},
                {vfunction(iface,3),reinterpret_cast<void*>(hook_la),reinterpret_cast<void**>(&original_la)}};
            ok=install(h,d);
        } else d.win32_error=ERROR_INVALID_ADDRESS;
    }
    if (!ok) {network_failure(modern);return false;}
    InterlockedOr(&network_mask,modern?2:1);
    if (network_mask==3 && !network_failed) message("已接入：候选过滤、握手和双通信接口；请在本机验证匹配。",UB_READY);
    return true;
}
void check_modern_ownership() {
    if (network_failed || !(network_mask&2)) return;
    uintptr_t vt=0;
    if (context(0x5f9798)!=reinterpret_cast<void*>(modern_interface) || !UbGet(modern_interface,vt) || vt!=modern_vtable) {
        auto& d=hook_diagnostics[2];d.stage=UB_STAGE_OWNERSHIP;d.index=d.slot=UINT32_MAX;
        d.observed_address=static_cast<uint32_t>(vt);d.win32_error=ERROR_INVALID_ADDRESS;
        network_failure(true);return;
    }
    for (unsigned i=0;i<3;i++) {
        void* value=nullptr;auto& s=modern_slots[i];
        if (!UbGet(s.address,value) || value!=s.detour) {
            auto& d=hook_diagnostics[2];d.stage=UB_STAGE_OWNERSHIP;d.slot=d.index=i;
            d.slot_address=static_cast<uint32_t>(s.address);target_info(d,s.original);
            d.observed_address=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(value));d.win32_error=ERROR_RETRY;
            network_failure(true);return;
        }
    }
}
bool install_native_policy() {
    if (network_failed || !policy_requested() || battle()) return false;
    auto& d=hook_diagnostics[0];
    std::vector<Hook> h={
        {reinterpret_cast<void*>(image+SIG_join_lobby.rva),reinterpret_cast<void*>(hook_join),reinterpret_cast<void**>(&original_join)},
        {reinterpret_cast<void*>(image+SIG_p2p_request.rva),reinterpret_cast<void*>(hook_request),reinterpret_cast<void**>(&original_request)},
        {reinterpret_cast<void*>(image+SIG_lobby_chat.rva),reinterpret_cast<void*>(hook_chat),reinterpret_cast<void**>(&original_chat)},
        {reinterpret_cast<void*>(image+SIG_lobby_members.rva),reinterpret_cast<void*>(hook_members),reinterpret_cast<void**>(&original_members)}};
    if (!install(h,d,1)) {
        InterlockedExchange(&ready,0);network_failed=true;
        message("房间入口挂钩失败；拦截停用，请保存诊断状态。",UB_ERROR,UB_HOOK_FAILURE);return false;
    }
    native_policy_ready=true;return true;
}
DWORD WINAPI worker(void*) {
    for (;;) {
        if (lock_ipc()) {
            AcquireSRWLockExclusive(&policy_lock);
            enabled=shared->enable!=0;client_tick=shared->client_tick;
            id_count=std::min(shared->blocked_count,UB_MAX_BLOCKED);
            memcpy(ids,shared->blocked,id_count*8);std::sort(ids,ids+id_count);
            ReleaseSRWLockExclusive(&policy_lock);
            shared->policy_ack=shared->policy_revision;
            ReleaseMutex(ipc_mutex);
        }
        // Observing an empty/disabled list installs only the search hook.
        if (policy_requested() && !battle() && !network_failed) {
            if (!native_policy_ready) install_native_policy();
            if (native_policy_ready && !network_failed && !(network_mask&1) && context(0x5a3c08)) install_network(false);
            if (native_policy_ready && !network_failed && !(network_mask&2) && context(0x5f9798)) install_network(true);
        }
        check_modern_ownership();
        if (lock_ipc()) {
            shared->state=runtime_state;shared->status=runtime_status;
            if (!network_failed && !policy_requested())
                strncpy(shared->message_utf8,"观察模式：黑名单为空、开关关闭或心跳过期；不拦截通信。",sizeof(shared->message_utf8)-1);
            else if (!network_failed && network_mask!=3)
                strncpy(shared->message_utf8,"候选过滤已接入；等待通信接口，完整保护未生效。",sizeof(shared->message_utf8)-1);
            else memcpy(shared->message_utf8,runtime_message,sizeof(runtime_message));
            shared->message_utf8[sizeof(shared->message_utf8)-1]=0;
            shared->heartbeat=GetTickCount();UbGet(image+0x5a4764,shared->host_scene);
            shared->battle_suspended=battle();shared->network_hooks_ready=static_cast<uint32_t>(network_mask);
            shared->filter_active=filtering();
            shared->effective_enabled=shared->filter_active && native_policy_ready && network_mask==3 && !network_failed;
            memcpy(shared->hook_diagnostics,hook_diagnostics,sizeof(hook_diagnostics));
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
    release_message=api<ReleaseMessage>("SteamAPI_SteamNetworkingMessage_t_Release");
    if (!lobby_data || !lobby_owner || !release_message) return finish(UB_WRONG_IMAGE);
    wchar_t map_name[128],mutex_name[128];UbNames(GetCurrentProcessId(),map_name,mutex_name);
    ipc_mutex=CreateMutexW(nullptr,FALSE,mutex_name);
    mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(UbShared),map_name);
    if (!ipc_mutex || !mapping) return finish(UB_IPC_FAILURE);
    shared=static_cast<UbShared*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(UbShared)));
    if (!shared || !lock_ipc(1000)) return finish(UB_IPC_FAILURE);
    memset(shared,0,sizeof(*shared));shared->magic=UB_MAGIC;shared->abi=UB_ABI;
    shared->bytes=sizeof(*shared);shared->pid=GetCurrentProcessId();ReleaseMutex(ipc_mutex);
    message("正在接入搜索观察入口；黑名单为空时不安装通信挂钩。",UB_STARTING);
    HMODULE retained=nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&BlacklistStart),&retained);
    UbInstallFaultTrace();
    reset_diagnostic(hook_diagnostics[0],UB_HOOK_MINHOOK);
    hook_diagnostics[0].stage=UB_STAGE_MH_INIT;hook_diagnostics[0].minhook_status=MH_Initialize();
    if (hook_diagnostics[0].minhook_status!=MH_OK) {message("无法初始化挂钩。",UB_ERROR,UB_HOOK_FAILURE);return finish(UB_HOOK_FAILURE);}
    std::vector<Hook> h={{reinterpret_cast<void*>(image+SIG_search_results.rva),reinterpret_cast<void*>(hook_search),reinterpret_cast<void**>(&original_search)}};
    if (!install(h,hook_diagnostics[0])) {message("原生入口挂钩失败；黑名单未启用。",UB_ERROR,UB_HOOK_FAILURE);return finish(UB_HOOK_FAILURE);}
    InterlockedExchange(&ready,1);
    message("搜索观察已接入；黑名单为空时不安装通信挂钩。",UB_READY);
    HANDLE thread=CreateThread(nullptr,0,worker,nullptr,0,nullptr);
    if (!thread) {InterlockedExchange(&ready,0);message("无法启动黑名单控制线程。",UB_ERROR,UB_IPC_FAILURE);return finish(UB_IPC_FAILURE);}
    CloseHandle(thread);return finish(UB_OK);
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) {
    if (reason==DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(module);
    return TRUE;
}
