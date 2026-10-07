#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <algorithm>
#include <stdexcept>
#include "settings.h"
#include "protocol.h"

std::wstring UbWide(const std::string& s) {
    if (s.empty()) return {};
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);
    if (!n) throw std::runtime_error("无效 UTF-8 内容。");
    std::wstring out(n,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),out.data(),n);
    return out;
}
std::string UbUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);
    if (!n) throw std::runtime_error("无效 Unicode 内容。");
    std::string out(n,'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr);
    return out;
}
uint64_t UbParseId(const std::wstring& text) {
    uint64_t id=0;
    if (text.empty() || text.size()>20) return 0;
    for (wchar_t c:text) {
        if (c<L'0' || c>L'9' || id>(UINT64_MAX-uint64_t(c-L'0'))/10) return 0;
        id=id*10+uint64_t(c-L'0');
    }
    return UbPlayerId(id)?id:0;
}
std::wstring UbSettingsPath() {
    wchar_t folder[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA|CSIDL_FLAG_CREATE,nullptr,SHGFP_TYPE_CURRENT,folder)))
        throw std::runtime_error("无法取得本机配置目录。");
    std::wstring dir=std::wstring(folder)+L"\\UNI2Blacklist";
    if (!CreateDirectoryW(dir.c_str(),nullptr) && GetLastError()!=ERROR_ALREADY_EXISTS)
        throw std::runtime_error("无法创建黑名单配置目录。");
    return dir+L"\\blacklist.tsv";
}
std::vector<UbEntry> UbLoadSettings() {
    auto path=UbSettingsPath();
    HANDLE f=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (f==INVALID_HANDLE_VALUE) {
        if (GetLastError()==ERROR_FILE_NOT_FOUND) return {};
        throw std::runtime_error("无法读取黑名单文件。");
    }
    LARGE_INTEGER size{};DWORD done=0;
    if (!GetFileSizeEx(f,&size) || size.QuadPart<0 || size.QuadPart>262144) {
        CloseHandle(f);throw std::runtime_error("黑名单文件超过大小限制。");
    }
    std::string bytes(static_cast<size_t>(size.QuadPart),'\0');
    bool ok=ReadFile(f,bytes.data(),static_cast<DWORD>(bytes.size()),&done,nullptr) && done==bytes.size();CloseHandle(f);
    if (!ok) throw std::runtime_error("黑名单文件读取不完整。");
    auto text=UbWide(bytes);
    std::vector<UbEntry> out;
    size_t start=0;
    while (start<text.size()) {
        size_t end=text.find(L'\n',start);if (end==std::wstring::npos) end=text.size();
        auto line=text.substr(start,end-start);start=end+1;
        if (!line.empty() && line.back()==L'\r') line.pop_back();
        if (line.empty() || line[0]==L'#') continue;
        auto tab=line.find(L'\t');
        auto id=UbParseId(line.substr(0,tab));
        if (!id || std::any_of(out.begin(),out.end(),[&](const UbEntry& e){return e.id==id;}))
            throw std::runtime_error("黑名单文件包含无效或重复的 SteamID64；原文件保留。");
        auto alias=tab==std::wstring::npos?L"":line.substr(tab+1);
        if (alias.size()>128 || alias.find_first_of(L"\r\n\t")!=std::wstring::npos || out.size()>=UB_MAX_BLOCKED)
            throw std::runtime_error("黑名单条目超出限制；原文件保留。");
        out.push_back({id,alias});
    }
    return out;
}
void UbSaveSettings(const std::vector<UbEntry>& entries) {
    if (entries.size()>UB_MAX_BLOCKED) throw std::runtime_error("黑名单最多保存 256 人。");
    std::wstring text=L"# UNI2Blacklist v1; SteamID64<TAB>alias; UTF-8\n";
    std::vector<uint64_t> seen;
    for (const auto& entry:entries) {
        if (!UbPlayerId(entry.id) || std::find(seen.begin(),seen.end(),entry.id)!=seen.end())
            throw std::runtime_error("SteamID64 无效或重复。");
        seen.push_back(entry.id);
        auto alias=entry.alias.substr(0,128);
        for (auto& c:alias) if (c<L' ' || c==L'\t') c=L' ';
        text+=std::to_wstring(entry.id)+L"\t"+alias+L"\n";
    }
    auto bytes=UbUtf8(text),path=UbUtf8(UbSettingsPath());
    auto target=UbWide(path),temp=target+L".tmp";
    HANDLE f=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (f==INVALID_HANDLE_VALUE) throw std::runtime_error("无法写入黑名单临时文件。");
    DWORD done=0;
    bool ok=WriteFile(f,bytes.data(),static_cast<DWORD>(bytes.size()),&done,nullptr) && done==bytes.size() && FlushFileBuffers(f);
    CloseHandle(f);
    if (!ok || !MoveFileExW(temp.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp.c_str());throw std::runtime_error("黑名单保存失败；旧文件保留。");
    }
}
