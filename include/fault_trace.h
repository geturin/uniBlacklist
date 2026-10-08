#pragma once
#include <windows.h>
#include <stdint.h>
#include <string>

constexpr uint32_t UB_FAULT_MAGIC=0x31464255;
constexpr uint32_t UB_FAULT_LIMIT=8;
enum UbTraceStage : uint32_t {UB_TRACE_OUTSIDE,UB_TRACE_SEARCH_ORIGINAL,
    UB_TRACE_SEARCH_CAPTURE,UB_TRACE_JOIN,UB_TRACE_CALLBACK,UB_TRACE_NETWORK,UB_TRACE_INSTALL};
inline const char* UbTraceStageName(uint32_t stage) {
    constexpr const char* names[]={"outside_hook","original_search","search_capture","join_owner_and_original",
        "room_callback_and_original","network_filter_and_original","hook_installation"};
    return stage<sizeof(names)/sizeof(names[0])?names[stage]:"unknown";
}
struct UbFaultRecord {
    uint32_t magic,bytes,version,pid,tid,sequence,tick,code,flags,eip,
        operation,fault_address,allocation_base,stage,search_count,reserved;
};
static_assert(sizeof(UbFaultRecord)==64,"fixed local fault trace layout");
inline std::wstring UbDiagnosticDirectory(bool create=false) {
    wchar_t path[32768]{};DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",path,32768);
    if (!n || n>=32768) return {};
    std::wstring directory=std::wstring(path)+L"\\UNI2Blacklist";
    if (create) CreateDirectoryW(directory.c_str(),nullptr);
    directory+=L"\\diagnostics";
    if (create) CreateDirectoryW(directory.c_str(),nullptr);
    return directory;
}
inline std::wstring UbFaultPath(DWORD pid) {
    auto directory=UbDiagnosticDirectory();
    return directory.empty()?std::wstring():directory+L"\\uni2-"+std::to_wstring(pid)+L"-fault.bin";
}

// All resources are prepared before game callbacks. The handler is a bounded,
// best-effort first-chance recorder, never an exception recovery mechanism.
namespace UbFaultRuntime {
inline HANDLE file=INVALID_HANDLE_VALUE;
inline DWORD tls=TLS_OUT_OF_INDEXES;
inline volatile LONG guard=0,sequence=0,search_count=0;
inline LONG CALLBACK handler(EXCEPTION_POINTERS* fault) {
    DWORD saved=GetLastError();
    if (!fault || !fault->ExceptionRecord || !fault->ContextRecord || file==INVALID_HANDLE_VALUE) return EXCEPTION_CONTINUE_SEARCH;
    DWORD code=fault->ExceptionRecord->ExceptionCode;
    if (code!=EXCEPTION_ACCESS_VIOLATION && code!=EXCEPTION_IN_PAGE_ERROR &&
        code!=EXCEPTION_ILLEGAL_INSTRUCTION && code!=EXCEPTION_PRIV_INSTRUCTION &&
        code!=EXCEPTION_INT_DIVIDE_BY_ZERO && code!=EXCEPTION_STACK_OVERFLOW &&
        code!=0xc0000374 && code!=0xc0000409) return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedCompareExchange(&guard,1,0)) {SetLastError(saved);return EXCEPTION_CONTINUE_SEARCH;}
    uint32_t number=static_cast<uint32_t>(InterlockedIncrement(&sequence));
    {
        const auto& e=*fault->ExceptionRecord;
        UbFaultRecord record{};
        record.magic=UB_FAULT_MAGIC;record.bytes=sizeof(record);record.version=1;
        record.pid=GetCurrentProcessId();record.tid=GetCurrentThreadId();record.sequence=number;
        record.tick=GetTickCount();record.code=code;record.flags=e.ExceptionFlags;
        record.eip=fault->ContextRecord->Eip;
        record.operation=UINT32_MAX;
        if ((code==EXCEPTION_ACCESS_VIOLATION || code==EXCEPTION_IN_PAGE_ERROR) && e.NumberParameters>=2) {
            record.operation=e.ExceptionInformation[0];record.fault_address=e.ExceptionInformation[1];
        }
        MEMORY_BASIC_INFORMATION memory{};
        if (VirtualQuery(reinterpret_cast<void*>(uintptr_t(record.eip)),&memory,sizeof(memory)))
            record.allocation_base=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(memory.AllocationBase));
        if (tls!=TLS_OUT_OF_INDEXES) record.stage=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(TlsGetValue(tls)));
        record.search_count=static_cast<uint32_t>(InterlockedCompareExchange(&search_count,0,0));
        DWORD done=0;
        LARGE_INTEGER position{};position.QuadPart=((number-1)%UB_FAULT_LIMIT)*sizeof(record);
        if (SetFilePointerEx(file,position,nullptr,FILE_BEGIN)) {
            WriteFile(file,&record,sizeof(record),&done,nullptr);
            FlushFileBuffers(file);
        }
    }
    InterlockedExchange(&guard,0);SetLastError(saved);
    return EXCEPTION_CONTINUE_SEARCH;
}
}
inline bool UbInstallFaultTrace() {
    DWORD saved=GetLastError();
    bool installed=false;
    if (!UbDiagnosticDirectory(true).empty()) {
        auto path=UbFaultPath(GetCurrentProcessId());
        UbFaultRuntime::file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,
            CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if (UbFaultRuntime::file!=INVALID_HANDLE_VALUE) {
            UbFaultRuntime::tls=TlsAlloc();
            installed=UbFaultRuntime::tls!=TLS_OUT_OF_INDEXES && AddVectoredExceptionHandler(1,UbFaultRuntime::handler)!=nullptr;
            if (!installed) {
                if (UbFaultRuntime::tls!=TLS_OUT_OF_INDEXES) TlsFree(UbFaultRuntime::tls);
                UbFaultRuntime::tls=TLS_OUT_OF_INDEXES;
                CloseHandle(UbFaultRuntime::file);UbFaultRuntime::file=INVALID_HANDLE_VALUE;
            }
        }
    }
    SetLastError(saved);return installed;
}
struct UbTraceScope {
    void* previous=nullptr;
    explicit UbTraceScope(UbTraceStage stage) {
        DWORD saved=GetLastError();
        if (UbFaultRuntime::tls!=TLS_OUT_OF_INDEXES) {
            previous=TlsGetValue(UbFaultRuntime::tls);
            TlsSetValue(UbFaultRuntime::tls,reinterpret_cast<void*>(uintptr_t(stage)));
        }
        SetLastError(saved);
    }
    ~UbTraceScope() {
        DWORD saved=GetLastError();
        if (UbFaultRuntime::tls!=TLS_OUT_OF_INDEXES) TlsSetValue(UbFaultRuntime::tls,previous);
        SetLastError(saved);
    }
    UbTraceScope(const UbTraceScope&)=delete;
    UbTraceScope& operator=(const UbTraceScope&)=delete;
};
