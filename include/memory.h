#pragma once
#include <windows.h>
#include <stdint.h>
#include <limits>
#include <cstring>

inline bool UbRead(uintptr_t address, void* out, size_t bytes) {
    if (!address || bytes > std::numeric_limits<uintptr_t>::max()-address) return false;
    SIZE_T done=0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
                             out, bytes, &done) && done==bytes;
}
template<class T> inline bool UbGet(uintptr_t address, T& out) {
    return UbRead(address, &out, sizeof(out));
}
inline bool UbWritable(uintptr_t address, size_t bytes) {
    if (!address || bytes > std::numeric_limits<uintptr_t>::max()-address) return false;
    const uintptr_t end=address+bytes;
    while (address<end) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi)) ||
            mbi.State!=MEM_COMMIT || (mbi.Protect&(PAGE_GUARD|PAGE_NOACCESS)) ||
            !(mbi.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))) return false;
        const uintptr_t next=reinterpret_cast<uintptr_t>(mbi.BaseAddress)+mbi.RegionSize;
        if (next<=address) return false;
        address=next;
    }
    return true;
}
inline bool UbPut(uintptr_t address, const void* in, size_t bytes) {
    if (!UbWritable(address,bytes)) return false;
    // This helper runs inside the injected process. Write the verified live
    // object directly; remote writes are only needed in the separate injector.
    std::memcpy(reinterpret_cast<void*>(address),in,bytes);
    return true;
}
template<class T> inline bool UbSet(uintptr_t address, const T& in) { return UbPut(address,&in,sizeof(in)); }
inline bool UbExecutable(void* p, bool image_only=true) {
    MEMORY_BASIC_INFORMATION m{};
    return p && VirtualQuery(p,&m,sizeof(m)) && (!image_only || m.Type==MEM_IMAGE) && m.State==MEM_COMMIT &&
        !(m.Protect&(PAGE_GUARD|PAGE_NOACCESS)) &&
        (m.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY));
}
