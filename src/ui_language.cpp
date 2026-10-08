#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iterator>
#include <stdexcept>
#include <string>
#include "settings.h"
#include "ui_language.h"

namespace {
std::wstring language_path() {
    const auto path=UbSettingsPath();
    return path.substr(0,path.find_last_of(L'\\')+1)+L"language.ini";
}

struct Translation {const wchar_t* original; const wchar_t* english;};
constexpr Translation translations[] = {
    {L"无效 UTF-8 内容。",L"The saved text is not valid UTF-8."},
    {L"无效 Unicode 内容。",L"The text contains invalid Unicode characters."},
    {L"无法取得本机配置目录。",L"Cannot find your local settings folder."},
    {L"无法创建黑名单配置目录。",L"Cannot create the settings folder. Check its permissions."},
    {L"无法读取黑名单文件。",L"Cannot read the blocklist. Check its permissions."},
    {L"黑名单文件超过大小限制。",L"The blocklist file is too large."},
    {L"黑名单文件读取不完整。",L"Cannot read the complete blocklist. Try reopening the app."},
    {L"黑名单文件包含无效或重复的 SteamID64；原文件保留。",L"The blocklist contains invalid or duplicate player IDs. The original file was kept."},
    {L"黑名单条目超出限制；原文件保留。",L"The blocklist contains too many entries or an invalid name. The original file was kept."},
    {L"黑名单最多保存 256 人。",L"The blocklist can hold up to 256 players."},
    {L"SteamID64 无效或重复。",L"The player ID is invalid or already on the blocklist."},
    {L"无法写入黑名单临时文件。",L"Cannot save the blocklist. Check the settings folder permissions."},
    {L"黑名单保存失败；旧文件保留。",L"Cannot save the blocklist. The previous file was kept."},
    {L"无法读取 Wi-Fi 筛选选项；原文件保留。",L"Cannot read the Wi-Fi setting. The original file was kept."},
    {L"Wi-Fi 筛选选项损坏；原文件保留，请修复 options.ini 后重开程序。",L"The Wi-Fi setting is damaged. Fix options.ini and reopen the app. The original file was kept."},
    {L"无法写入 Wi-Fi 筛选选项。",L"Cannot save the Wi-Fi setting. Check the settings folder permissions."},
    {L"Wi-Fi 筛选选项保存失败；旧文件保留。",L"Cannot save the Wi-Fi setting. The previous file was kept."},
    {L"无法读取语言选项；原文件保留。",L"Cannot read the language setting. The original file was kept."},
    {L"语言选项损坏；原文件保留，请修复 language.ini 后重开程序。",L"The language setting is damaged. Fix language.ini and reopen the app. The original file was kept."},
    {L"语言选项无效。",L"The selected language is not supported."},
    {L"无法写入语言选项。",L"Cannot save the language setting. Check the settings folder permissions."},
    {L"语言选项保存失败；旧文件保留。",L"Cannot save the language setting. The previous file was kept."},
    {L"未知 Windows 错误",L"Unknown Windows error"},
    {L"无法分配黑名单插件初始化内存。",L"Cannot initialize the game plugin. Try restarting the game."},
    {L"无法解析文件路径。",L"Cannot resolve the file path."},
    {L"文件路径过长或已变化。",L"The file path is too long or has changed."},
    {L"无法定位黑名单插件程序。",L"Cannot locate this app's executable."},
    {L"无法读取目标进程的 EXE 路径。",L"Cannot read the game's executable path."},
    {L"EXE 文件定位失败。",L"Cannot seek within the game executable."},
    {L"EXE 文件读取不完整。",L"Cannot read the complete game executable."},
    {L"目标不是有效的 PE32 映像。禁止注入。",L"The selected executable is invalid. Connection was refused."},
    {L"只支持本黑名单插件对应的 32 位 UNI2 映像。禁止注入。",L"This app supports only its specified 32-bit UNI2 version. Connection was refused."},
    {L"Windows SHA-256 提供程序不可用。",L"Windows cannot verify the game executable."},
    {L"无法创建 SHA-256 校验。",L"Cannot start game executable verification."},
    {L"EXE 的 SHA-256 校验失败。",L"Cannot verify the game executable."},
    {L"无法列举进程。",L"Cannot list running apps."},
    {L"无法读取进程列表。",L"Cannot read the running app list."},
    {L"没有找到 uni2.exe。请先正常启动游戏，再开始记录。",L"Start UNI2 first, then connect."},
    {L"发现多个 uni2.exe；请使用 --pid 明确选择一个进程。",L"More than one UNI2 process is running. Select one game process."},
    {L"无法读取 32 位目标模块列表。",L"Cannot read the game's loaded modules."},
    {L"目标进程的模块列表为空。",L"The game has no readable loaded modules. Try restarting it."},
    {L"模块列表读取不完整。",L"Cannot read all loaded game modules."},
    {L"目标存在同名模块；无法安全选择调用地址。",L"The game has duplicate modules. Restart it before connecting."},
    {L"无法核对正在运行的目标映像。",L"Cannot verify the running game."},
    {L"运行中的目标映像头不合法。禁止注入。",L"The running game image is invalid. Connection was refused."},
    {L"无法读取运行中的 PE32 头。",L"Cannot read the running game image."},
    {L"运行中的目标不是预期的 PE32 映像。禁止注入。",L"The running game does not match the supported image. Connection was refused."},
    {L"无法找到 Windows 装载函数。",L"Cannot locate the Windows plugin loader."},
    {L"无法确定装载函数的真实所属模块。",L"Cannot identify the Windows plugin loader module."},
    {L"无法读取装载函数所属模块路径。",L"Cannot read the Windows plugin loader path."},
    {L"目标缺少装载函数的真实模块或函数地址越界。",L"The game's Windows plugin loader could not be verified."},
    {L"无法核对本地装载函数的真实模块。",L"Cannot verify the local Windows plugin loader."},
    {L"装载函数所属模块的 DOS 头无效。",L"The Windows plugin loader image is invalid."},
    {L"装载函数所属模块的 PE32 头无效。",L"The Windows plugin loader is not a valid 32-bit image."},
    {L"装载函数所属模块的函数范围无效。",L"The Windows plugin loader address is outside its module."},
    {L"目标装载模块的 DOS 头与本进程不一致。",L"The game's Windows plugin loader does not match this app's loader."},
    {L"无法读取目标装载模块的 PE32 头。",L"Cannot read the game's Windows plugin loader."},
    {L"目标装载函数所属模块与本进程不是同一 PE32 映像；拒绝借用 RVA。",L"The game's Windows plugin loader does not match this app's loader. Connection was refused."},
    {L"目标装载函数地址不可读。",L"The game's Windows plugin loader cannot be read."},
    {L"无法写入黑名单插件初始化参数。",L"Cannot initialize the game plugin. Run this app with the same permissions as the game."},
    {L"无法创建黑名单插件初始化线程。",L"Cannot start the game plugin. Run this app with the same permissions as the game."},
    {L"等待初始化超过 30 秒。线程仍可能继续；保留参数内存，请勿重复点击。停止或退出游戏后重新开始。",L"Connection timed out after 30 seconds. Restart the game before trying again."},
    {L"等待初始化失败；已保留可能仍被线程使用的参数内存。",L"Connection failed while initializing. Restart the game before trying again."},
    {L"无法读取初始化线程结果。",L"Cannot read the plugin initialization result."},
    {L"无法只读解析随附 DLL；请保持程序和 DLL 位于同一文件夹。",L"Cannot read the plugin DLL. Keep the EXE and DLL in the same folder."},
    {L"随附 DLL 不是 32 位黑名单模块。",L"The included DLL is not the required 32-bit plugin."},
    {L"随附 DLL 缺少所需的黑名单导出。",L"The included DLL is incompatible. Extract the complete app package again."},
    {L"黑名单 DLL 的导出被转发，无法安全调用。",L"The included DLL could not be verified. Extract the complete app package again."},
    {L"无法核对已装载黑名单 DLL 的版本。",L"Cannot verify the loaded plugin version."},
    {L"游戏保留的是旧版本黑名单 DLL。请关闭旧黑名单窗口并重新启动游戏，再使用这个程序包。",L"The game still has an older plugin loaded. Close the old app and restart the game before using this package."},
    {L"无法枚举游戏进程。",L"Cannot list running UNI2 processes."},
    {L"无法连接游戏，请以与游戏相同的用户和权限运行。",L"Cannot connect. Run this app as the same user and with the same permissions as the game."},
    {L"所选进程不是 uni2.exe。",L"The selected process is not UNI2."},
    {L"无法锁定并核验游戏文件。",L"Cannot open the game executable for verification."},
    {L"游戏版本不支持：EXE长度不匹配。",L"This game version is not supported."},
    {L"运行映像与核验文件不一致。",L"The running game does not match its executable file. Restart the game."},
    {L"无法定位插件目录。",L"Cannot locate the plugin folder."},
    {L"游戏已加载另一目录的插件，请重启游戏后再连接。",L"The game has a plugin loaded from another folder. Restart the game before connecting."},
    {L"游戏无法加载随附插件 DLL。",L"The game cannot load the plugin DLL. Extract the complete package and restart the game."},
    {L"加载后的插件身份不匹配。",L"The loaded plugin does not match this package. Restart the game."},
    {L"插件初始化应答无效。",L"The plugin returned an invalid response. Restart the game."},
    {L"原配置读取失败，请先修复配置文件后重开程序，防止覆盖旧黑名单。",L"Fix the saved settings and reopen the app before changing the blocklist."},
    {L"该 SteamID64 已在黑名单中。",L"This player is already on the blocklist."},
    {L"连接失败：无法读取目标进程信息。",L"Cannot read game process information. Try reconnecting."},
    {L"请先选择一个游戏进程。",L"Select a running game first."},
    {L"无法打开插件控制通道。请重启游戏后重试。",L"Cannot communicate with the plugin. Restart the game and try again."},
    {L"插件控制通道的身份不匹配。",L"The plugin connection does not match this app. Restart the game."},
    {L"无法观察游戏退出状态。",L"Cannot monitor the game process."},
    {L"状态文件未完整保存。",L"Cannot save the complete diagnostics file."},
    {L"请在搜索结果中选择一位玩家。",L"Select a player in the search results."},
    {L"操作失败，原有配置保持不变。",L"The operation failed. Your previous settings were kept."},
    {L"黑名单窗口已运行。",L"UNI2 Blacklist is already running."},
};

std::wstring translate_line(std::wstring line) {
    while (!line.empty() && (line.back()==L' ' || line.back()==L'\r')) line.pop_back();
    for (const auto& item:translations) if (line==item.original) return item.english;
    constexpr wchar_t hash_prefix[]=L"游戏版本不支持，SHA256：";
    constexpr wchar_t status_prefix[]=L"插件拒绝初始化，状态码：";
    if (line.rfind(hash_prefix,0)==0) return L"This game version is not supported. SHA256: "+line.substr(std::size(hash_prefix)-1);
    if (line.rfind(status_prefix,0)==0) return L"The plugin refused initialization. Status: "+line.substr(std::size(status_prefix)-1);
    // Injector errors append a localized Windows description and its numeric
    // code. Ask Windows for English text while retaining that exact code.
    const auto open=line.find_last_of(L'(');
    if (open!=std::wstring::npos && line.back()==L')') {
        DWORD code=0;bool numeric=open+2<line.size();
        for (size_t i=open+1; numeric && i+1<line.size(); ++i) {
            const wchar_t c=line[i];
            if (c<L'0' || c>L'9' || code>(MAXDWORD-DWORD(c-L'0'))/10) numeric=false;
            else code=code*10+DWORD(c-L'0');
        }
        if (numeric) {
            wchar_t* buffer=nullptr;
            DWORD count=FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr,code,MAKELANGID(LANG_ENGLISH,SUBLANG_ENGLISH_US),reinterpret_cast<wchar_t*>(&buffer),0,nullptr);
            std::wstring description=count && buffer?std::wstring(buffer,count):L"Windows error";
            if (buffer) LocalFree(buffer);
            while (!description.empty() && (description.back()==L'\r' || description.back()==L'\n')) description.pop_back();
            return description+L" ("+std::to_wstring(code)+L")";
        }
    }
    // Unknown diagnostics remain intact rather than hiding a useful detail.
    return line;
}
} // namespace

UbLanguage UbLoadLanguage() {
    const auto path=language_path();
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file==INVALID_HANDLE_VALUE) {
        if (GetLastError()==ERROR_FILE_NOT_FOUND) return UbLanguage::Chinese;
        throw std::runtime_error("无法读取语言选项；原文件保留。");
    }
    char bytes[64]{};DWORD read=0;LARGE_INTEGER size{};
    const bool ok=GetFileSizeEx(file,&size) && size.QuadPart>0 && size.QuadPart<64 &&
        ReadFile(file,bytes,sizeof(bytes),&read,nullptr) && read==size.QuadPart;
    CloseHandle(file);
    if (ok && std::string(bytes,read)=="language=zh-CN\n") return UbLanguage::Chinese;
    if (ok && std::string(bytes,read)=="language=en\n") return UbLanguage::English;
    throw std::runtime_error("语言选项损坏；原文件保留，请修复 language.ini 后重开程序。");
}

void UbSaveLanguage(UbLanguage language) {
    const char* bytes=nullptr;DWORD length=0;
    if (language==UbLanguage::Chinese) {bytes="language=zh-CN\n";length=15;}
    else if (language==UbLanguage::English) {bytes="language=en\n";length=12;}
    else throw std::runtime_error("语言选项无效。");
    const auto target=language_path();
    static volatile LONG sequence=0;
    const auto temp=target+L".tmp."+std::to_wstring(GetCurrentProcessId())+L"."+
        std::to_wstring(static_cast<unsigned long>(InterlockedIncrement(&sequence)));
    HANDLE file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file==INVALID_HANDLE_VALUE) throw std::runtime_error("无法写入语言选项。");
    DWORD written=0;
    const bool ok=WriteFile(file,bytes,length,&written,nullptr) && written==length && FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok || !MoveFileExW(temp.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp.c_str());
        throw std::runtime_error("语言选项保存失败；旧文件保留。");
    }
}

std::wstring UbTranslateError(const std::wstring& message, UbLanguage language) {
    if (language!=UbLanguage::English) return message;
    std::wstring result;size_t start=0;
    for (;;) {
        const auto end=message.find(L'\n',start);
        result+=translate_line(message.substr(start,end==std::wstring::npos?end:end-start));
        if (end==std::wstring::npos) break;
        result+=L'\n';start=end+1;
    }
    return result;
}
