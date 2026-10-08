#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <wincrypt.h>
#include <shlobj.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <stdexcept>
#include <vector>
#include "protocol.h"
#include "injector.h"

namespace {
constexpr wchar_t kDllName[] = L"uni2-blacklist.dll";
constexpr char kExpectedSha[] = "cf64ec26e9646b68b46ed22c838b8f0778ab65abbeda0ebf164e81b5c08c9098";
constexpr DWORD kWaitMs = 30000;
constexpr uint64_t kExpectedBytes = 6921216;

struct Failure { std::wstring text; DWORD error; };
[[noreturn]] void fail(const std::wstring& s, DWORD error = 0) {
    throw Failure{s, error};
}
struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE h = nullptr) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
struct RemoteMemory {
    HANDLE process;
    void* value;
    RemoteMemory(HANDLE p, size_t bytes) : process(p),
        value(VirtualAllocEx(p, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)) {
        if (!value) fail(L"无法分配黑名单插件初始化内存。", GetLastError());
    }
    ~RemoteMemory() { if (value) VirtualFreeEx(process, value, 0, MEM_RELEASE); }
    void leave_allocated() { value = nullptr; }
};
struct LocalModule {
    HMODULE value;
    explicit LocalModule(HMODULE m) : value(m) {}
    ~LocalModule() { if (value) FreeLibrary(value); }
};

[[maybe_unused]] void output(const std::wstring& value, bool error = false) {
    HANDLE handle = GetStdHandle(error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    DWORD mode = 0, done = 0;
    if (GetConsoleMode(handle, &mode)) {
        WriteConsoleW(handle, value.data(), static_cast<DWORD>(value.size()), &done, nullptr);
    } else {
        int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                       nullptr, 0, nullptr, nullptr);
        std::string utf8(static_cast<size_t>(count), '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                            utf8.data(), count, nullptr, nullptr);
        WriteFile(handle, utf8.data(), static_cast<DWORD>(utf8.size()), &done, nullptr);
    }
}
std::wstring error_text(DWORD error) {
    wchar_t* buffer = nullptr;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                            FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0,
                            reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring result = n && buffer ? std::wstring(buffer, n) : L"未知 Windows 错误";
    if (buffer) LocalFree(buffer);
    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n')) result.pop_back();
    return result + L" (" + std::to_wstring(error) + L")";
}
std::wstring basename(const std::wstring& path) {
    auto last = path.find_last_of(L"/\\");
    return last == std::wstring::npos ? path : path.substr(last + 1);
}
[[maybe_unused]] std::wstring full_path(const std::wstring& path) {
    DWORD n = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (!n) fail(L"无法解析文件路径。", GetLastError());
    std::vector<wchar_t> buffer(static_cast<size_t>(n) + 1);
    DWORD written = GetFullPathNameW(path.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (!written || written >= buffer.size()) fail(L"文件路径过长或已变化。", GetLastError());
    return std::wstring(buffer.data(), written);
}
std::wstring exe_path() {
    std::vector<wchar_t> path(32768);
    DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!n || n >= path.size()) fail(L"无法定位黑名单插件程序。", GetLastError());
    return std::wstring(path.data(), n);
}
std::wstring process_path(HANDLE process) {
    std::vector<wchar_t> path(32768);
    DWORD n = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process, 0, path.data(), &n))
        fail(L"无法读取目标进程的 EXE 路径。", GetLastError());
    return std::wstring(path.data(), n);
}
void read_exact(HANDLE file, uint64_t offset, void* data, DWORD bytes) {
    LARGE_INTEGER position; position.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(file, position, nullptr, FILE_BEGIN)) fail(L"EXE 文件定位失败。", GetLastError());
    DWORD read = 0;
    if (!ReadFile(file, data, bytes, &read, nullptr) || read != bytes)
        fail(L"EXE 文件读取不完整。", GetLastError());
}
void verify_pe32_file(HANDLE file, uint64_t length) {
    IMAGE_DOS_HEADER dos{};
    read_exact(file, 0, &dos, sizeof(dos));
    if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < static_cast<LONG>(sizeof(dos)) ||
        static_cast<uint64_t>(dos.e_lfanew) > length ||
        sizeof(IMAGE_NT_HEADERS32) > length - static_cast<uint64_t>(dos.e_lfanew))
        fail(L"目标不是有效的 PE32 映像。禁止注入。 ");
    IMAGE_NT_HEADERS32 nt{};
    read_exact(file, static_cast<uint64_t>(dos.e_lfanew), &nt, sizeof(nt));
    if (nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt.FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER32) ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
        fail(L"只支持本黑名单插件对应的 32 位 UNI2 映像。禁止注入。 ");
}
std::string sha256_file(HANDLE file) {
    HCRYPTPROV provider = 0;
    if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        fail(L"Windows SHA-256 提供程序不可用。", GetLastError());
    HCRYPTHASH hash = 0;
    if (!CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash)) {
        DWORD error = GetLastError(); CryptReleaseContext(provider, 0);
        fail(L"无法创建 SHA-256 校验。", error);
    }
    LARGE_INTEGER zero{};
    bool ok = SetFilePointerEx(file, zero, nullptr, FILE_BEGIN) != FALSE;
    DWORD saved_error = ok ? 0 : GetLastError();
    unsigned char buffer[65536];
    while (ok) {
        DWORD count = 0;
        if (!ReadFile(file, buffer, sizeof(buffer), &count, nullptr)) {
            saved_error = GetLastError(); ok = false; break;
        }
        if (!count) break;
        if (!CryptHashData(hash, buffer, count, 0)) { saved_error = GetLastError(); ok = false; }
    }
    unsigned char digest[32]{}; DWORD digest_bytes = sizeof(digest);
    if (ok && !CryptGetHashParam(hash, HP_HASHVAL, digest, &digest_bytes, 0)) {
        saved_error = GetLastError(); ok = false;
    }
    CryptDestroyHash(hash); CryptReleaseContext(provider, 0);
    if (!ok || digest_bytes != sizeof(digest)) fail(L"EXE 的 SHA-256 校验失败。", saved_error);
    const char hex[] = "0123456789abcdef";
    std::string result; result.reserve(64);
    for (unsigned char b : digest) { result.push_back(hex[b >> 4]); result.push_back(hex[b & 15]); }
    return result;
}
[[maybe_unused]] DWORD find_pid() {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snapshot.value == INVALID_HANDLE_VALUE) fail(L"无法列举进程。", GetLastError());
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    std::vector<DWORD> matches;
    if (Process32FirstW(snapshot.value, &entry)) {
        do { if (_wcsicmp(entry.szExeFile, L"uni2.exe") == 0) matches.push_back(entry.th32ProcessID); }
        while (Process32NextW(snapshot.value, &entry));
    } else if (GetLastError() != ERROR_NO_MORE_FILES) fail(L"无法读取进程列表。", GetLastError());
    if (matches.empty()) fail(L"没有找到 uni2.exe。请先正常启动游戏，再开始记录。 ");
    if (matches.size() != 1) fail(L"发现多个 uni2.exe；请使用 --pid 明确选择一个进程。 ");
    return matches.front();
}
std::vector<MODULEENTRY32W> modules(DWORD pid) {
    HANDLE snapshot = INVALID_HANDLE_VALUE;
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (snapshot != INVALID_HANDLE_VALUE) break;
        if (GetLastError() != ERROR_BAD_LENGTH) break;
        Sleep(10);
    }
    Handle owner(snapshot);
    if (snapshot == INVALID_HANDLE_VALUE) fail(L"无法读取 32 位目标模块列表。", GetLastError());
    MODULEENTRY32W entry{}; entry.dwSize = sizeof(entry);
    std::vector<MODULEENTRY32W> result;
    if (!Module32FirstW(snapshot, &entry)) fail(L"目标进程的模块列表为空。", GetLastError());
    do { result.push_back(entry); } while (Module32NextW(snapshot, &entry));
    if (GetLastError() != ERROR_NO_MORE_FILES) fail(L"模块列表读取不完整。", GetLastError());
    return result;
}
const MODULEENTRY32W* find_module(const std::vector<MODULEENTRY32W>& items, const std::wstring& name) {
    const MODULEENTRY32W* result = nullptr;
    for (const auto& item : items) {
        if (_wcsicmp(item.szModule, name.c_str()) == 0) {
            if (result) fail(L"目标存在同名模块；无法安全选择调用地址。 ");
            result = &item;
        }
    }
    return result;
}
void verify_remote_pe(HANDLE process, const MODULEENTRY32W& module) {
    IMAGE_DOS_HEADER dos{}; SIZE_T read = 0;
    if (!ReadProcessMemory(process, module.modBaseAddr, &dos, sizeof(dos), &read) || read != sizeof(dos))
        fail(L"无法核对正在运行的目标映像。", GetLastError());
    if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < static_cast<LONG>(sizeof(dos)) ||
        static_cast<uint64_t>(dos.e_lfanew) + sizeof(IMAGE_NT_HEADERS32) > module.modBaseSize)
        fail(L"运行中的目标映像头不合法。禁止注入。 ");
    IMAGE_NT_HEADERS32 nt{};
    if (!ReadProcessMemory(process, module.modBaseAddr + dos.e_lfanew, &nt, sizeof(nt), &read) || read != sizeof(nt))
        fail(L"无法读取运行中的 PE32 头。", GetLastError());
    if (nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        nt.OptionalHeader.SizeOfImage != module.modBaseSize)
        fail(L"运行中的目标不是预期的 PE32 映像。禁止注入。 ");
}
uintptr_t remote_system_function(DWORD pid, const char* name) {
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    FARPROC function = kernel32 ? GetProcAddress(kernel32, name) : nullptr;
    if (!function) fail(L"无法找到 Windows 装载函数。", GetLastError());
    HMODULE owner = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(function), &owner))
        fail(L"无法确定装载函数的真实所属模块。", GetLastError());
    std::vector<wchar_t> owner_path(32768);
    DWORD n = GetModuleFileNameW(owner, owner_path.data(), static_cast<DWORD>(owner_path.size()));
    if (!n || n >= owner_path.size()) fail(L"无法读取装载函数所属模块路径。", GetLastError());
    uintptr_t rva = reinterpret_cast<uintptr_t>(function) - reinterpret_cast<uintptr_t>(owner);
    auto items = modules(pid);
    const auto* remote_owner = find_module(items, basename(std::wstring(owner_path.data(), n)));
    if (!remote_owner || rva >= remote_owner->modBaseSize)
        fail(L"目标缺少装载函数的真实模块或函数地址越界。 ");
    // No kernel32 base-address equality is assumed. The owner image is
    // independently checked before the remote call.
    return reinterpret_cast<uintptr_t>(remote_owner->modBaseAddr) + rva;
}
void verify_system_function(HANDLE process, uintptr_t remote) {
    FARPROC local = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HMODULE owner = nullptr;
    if (!local || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                    reinterpret_cast<LPCWSTR>(local), &owner))
        fail(L"无法核对本地装载函数的真实模块。", GetLastError());
    const auto* base = reinterpret_cast<const unsigned char*>(owner);
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS32 local_nt{}; SIZE_T read = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), base, &dos, sizeof(dos), &read) || read != sizeof(dos) ||
        dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < static_cast<LONG>(sizeof(dos)) || dos.e_lfanew > 1024 * 1024)
        fail(L"装载函数所属模块的 DOS 头无效。 ");
    if (!ReadProcessMemory(GetCurrentProcess(), base + dos.e_lfanew, &local_nt, sizeof(local_nt), &read) || read != sizeof(local_nt) ||
        local_nt.Signature != IMAGE_NT_SIGNATURE || local_nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        local_nt.FileHeader.Machine != IMAGE_FILE_MACHINE_I386)
        fail(L"装载函数所属模块的 PE32 头无效。 ");
    uintptr_t rva = reinterpret_cast<uintptr_t>(local) - reinterpret_cast<uintptr_t>(owner);
    if (rva >= local_nt.OptionalHeader.SizeOfImage || remote < rva)
        fail(L"装载函数所属模块的函数范围无效。 ");
    const auto* remote_base = reinterpret_cast<const unsigned char*>(remote - rva);
    IMAGE_DOS_HEADER remote_dos{}; IMAGE_NT_HEADERS32 remote_nt{};
    if (!ReadProcessMemory(process, remote_base, &remote_dos, sizeof(remote_dos), &read) || read != sizeof(remote_dos) ||
        remote_dos.e_magic != IMAGE_DOS_SIGNATURE || remote_dos.e_lfanew != dos.e_lfanew)
        fail(L"目标装载模块的 DOS 头与本进程不一致。 ");
    if (!ReadProcessMemory(process, remote_base + remote_dos.e_lfanew, &remote_nt, sizeof(remote_nt), &read) || read != sizeof(remote_nt))
        fail(L"无法读取目标装载模块的 PE32 头。", GetLastError());
    // Headers retain the preferred ImageBase after normal relocation. This
    // comparison permits different runtime bases, and avoids touching discarded
    // relocation pages or comparing instructions containing absolute addresses.
    if (memcmp(&local_nt, &remote_nt, sizeof(local_nt)) != 0)
        fail(L"目标装载函数所属模块与本进程不是同一 PE32 映像；拒绝借用 RVA。 ");
    unsigned char readable = 0;
    if (!ReadProcessMemory(process, reinterpret_cast<const void*>(remote), &readable, 1, &read) || read != 1)
        fail(L"目标装载函数地址不可读。", GetLastError());
}
void write_remote(HANDLE process, void* target, const void* data, SIZE_T bytes) {
    SIZE_T written = 0;
    if (!WriteProcessMemory(process, target, data, bytes, &written) || written != bytes)
        fail(L"无法写入黑名单插件初始化参数。", GetLastError());
}
DWORD call_remote(HANDLE process, uintptr_t function, RemoteMemory& argument) {
    Handle thread(CreateRemoteThread(process, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(function), argument.value, 0, nullptr));
    if (!thread.value) fail(L"无法创建黑名单插件初始化线程。", GetLastError());
    DWORD wait = WaitForSingleObject(thread.value, kWaitMs);
    if (wait != WAIT_OBJECT_0) {
        // A timed-out thread may still access the parameter. Deliberately retain
        // its tiny allocation, rather than causing a use-after-free in the game.
        argument.leave_allocated();
        if (wait == WAIT_TIMEOUT)
            fail(L"等待初始化超过 30 秒。线程仍可能继续；保留参数内存，请勿重复点击。停止或退出游戏后重新开始。 ");
        fail(L"等待初始化失败；已保留可能仍被线程使用的参数内存。", GetLastError());
    }
    DWORD result = 0;
    if (!GetExitCodeThread(thread.value, &result)) fail(L"无法读取初始化线程结果。", GetLastError());
    return result;
}
uintptr_t export_rva(const std::wstring& dll, const char* name, IMAGE_NT_HEADERS32& identity) {
    LocalModule local(LoadLibraryExW(dll.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES));
    if (!local.value) fail(L"无法只读解析随附 DLL；请保持程序和 DLL 位于同一文件夹。", GetLastError());
    auto* base = reinterpret_cast<unsigned char*>(local.value);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386)
        fail(L"随附 DLL 不是 32 位黑名单模块。 ");
    FARPROC function = GetProcAddress(local.value, name);
    if (!function) fail(L"随附 DLL 缺少所需的黑名单导出。", GetLastError());
    uintptr_t ptr = reinterpret_cast<uintptr_t>(function), start = reinterpret_cast<uintptr_t>(base);
    if (ptr < start || ptr - start >= nt->OptionalHeader.SizeOfImage)
        fail(L"黑名单 DLL 的导出被转发，无法安全调用。 ");
    identity = *nt;
    return ptr - start;
}
void verify_loaded_dll(HANDLE process, const MODULEENTRY32W& module, const IMAGE_NT_HEADERS32& identity) {
    verify_remote_pe(process, module);
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS32 loaded{}; SIZE_T read = 0;
    if (!ReadProcessMemory(process, module.modBaseAddr, &dos, sizeof(dos), &read) || read != sizeof(dos) ||
        !ReadProcessMemory(process, module.modBaseAddr + dos.e_lfanew, &loaded, sizeof(loaded), &read) || read != sizeof(loaded))
        fail(L"无法核对已装载黑名单 DLL 的版本。", GetLastError());
    if (memcmp(&loaded, &identity, sizeof(identity)) != 0)
        fail(L"游戏保留的是旧版本黑名单 DLL。请关闭旧黑名单窗口并重新启动游戏，再使用这个程序包。 ");
}
} // namespace
std::wstring UbErrorText(DWORD code) { return error_text(code); }
std::vector<UbProcess> UbFindGames() {
    std::vector<UbProcess> out;
    Handle snap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));
    if(snap.value==INVALID_HANDLE_VALUE) fail(L"无法枚举游戏进程。",GetLastError());
    PROCESSENTRY32W e{};e.dwSize=sizeof(e);
    if(Process32FirstW(snap.value,&e)) do {
        if(_wcsicmp(e.szExeFile,L"uni2.exe")==0) {
            Handle p(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,e.th32ProcessID));
            if(p.value) out.push_back({e.th32ProcessID,process_path(p.value)});
        }
    } while(Process32NextW(snap.value,&e));
    return out;
}
DWORD UbAttach(DWORD pid) {
    static_assert(sizeof(void*)==4,"PE32 only");
    try {
        if(!pid) pid=find_pid();
        Handle process(OpenProcess(PROCESS_CREATE_THREAD|PROCESS_QUERY_INFORMATION|PROCESS_VM_OPERATION|PROCESS_VM_WRITE|PROCESS_VM_READ|SYNCHRONIZE,FALSE,pid));
        if(!process.value)fail(L"无法连接游戏，请以与游戏相同的用户和权限运行。",GetLastError());
        auto path=process_path(process.value);
        if(_wcsicmp(basename(path).c_str(),L"uni2.exe")!=0)fail(L"所选进程不是 uni2.exe。");
        Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
        if(file.value==INVALID_HANDLE_VALUE)fail(L"无法锁定并核验游戏文件。",GetLastError());
        LARGE_INTEGER size{};
        if(!GetFileSizeEx(file.value,&size)||size.QuadPart!=static_cast<LONGLONG>(kExpectedBytes))fail(L"游戏版本不支持：EXE长度不匹配。");
        verify_pe32_file(file.value,kExpectedBytes);
        auto sha=sha256_file(file.value);
        if(sha!=kExpectedSha)fail(L"游戏版本不支持，SHA256："+std::wstring(sha.begin(),sha.end()));
        auto initial=modules(pid);auto game=find_module(initial,L"uni2.exe");
        if(!game||_wcsicmp(game->szExePath,path.c_str())!=0)fail(L"运行映像与核验文件不一致。");
        verify_remote_pe(process.value,*game);
        auto own=exe_path();auto slash=own.find_last_of(L"/\\");
        if(slash==std::wstring::npos)fail(L"无法定位插件目录。");
        auto dll=own.substr(0,slash+1)+kDllName;
        IMAGE_NT_HEADERS32 identity{};auto rva=export_rva(dll,"BlacklistStart",identity);
        auto prior=find_module(initial,kDllName);
        if(prior&&_wcsicmp(prior->szExePath,dll.c_str())!=0)fail(L"游戏已加载另一目录的插件，请重启游戏后再连接。");
        if(!prior) {
            auto load=remote_system_function(pid,"LoadLibraryW");verify_system_function(process.value,load);
            RemoteMemory remote(process.value,(dll.size()+1)*sizeof(wchar_t));
            write_remote(process.value,remote.value,dll.c_str(),(dll.size()+1)*sizeof(wchar_t));
            if(!call_remote(process.value,load,remote))fail(L"游戏无法加载随附插件 DLL。");
        }
        auto loaded=modules(pid);auto mod=find_module(loaded,kDllName);
        if(!mod||_wcsicmp(mod->szExePath,dll.c_str())!=0||rva>=mod->modBaseSize)fail(L"加载后的插件身份不匹配。");
        verify_loaded_dll(process.value,*mod,identity);
        UbStartRequest req{sizeof(req),UB_ABI,UINT32_MAX,0};
        RemoteMemory param(process.value,sizeof(req));write_remote(process.value,param.value,&req,sizeof(req));
        auto ret=call_remote(process.value,reinterpret_cast<uintptr_t>(mod->modBaseAddr)+rva,param);
        SIZE_T read=0;
        if(!ReadProcessMemory(process.value,param.value,&req,sizeof(req),&read)||read!=sizeof(req)||req.abi!=UB_ABI||req.bytes!=sizeof(req))fail(L"插件初始化应答无效。");
        // A retained plugin with a hook failure still exposes its diagnostic
        // mapping. Let the GUI validate and open that mapping, without claiming
        // that filters are active. Version/image/request errors remain fatal.
        if(req.status!=ret || (ret!=UB_OK && ret!=UB_HOOK_FAILURE))fail(L"插件拒绝初始化，状态码："+std::to_wstring(req.status));
        return pid;
    } catch(const Failure& e) {
        auto msg=e.text;if(e.error)msg+=L"\n"+error_text(e.error);
        int n=WideCharToMultiByte(CP_UTF8,0,msg.c_str(),-1,nullptr,0,nullptr,nullptr);
        std::string text(static_cast<size_t>(n),'\0');WideCharToMultiByte(CP_UTF8,0,msg.c_str(),-1,text.data(),n,nullptr,nullptr);
        throw std::runtime_error(text.c_str());
    }
}

std::vector<UbModule> UbListModules(DWORD pid) {
    std::vector<UbModule> result;
    try {
        for (const auto& m:modules(pid)) result.push_back({static_cast<uint32_t>(reinterpret_cast<uintptr_t>(m.modBaseAddr)),m.modBaseSize,m.szModule});
    } catch(const Failure&) {return {};}
    return result;
}
