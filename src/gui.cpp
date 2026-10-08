#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "protocol.h"
#include "status_report.h"
#include "candidate_history.h"
#include "fault_trace.h"
#include "injector.h"
#include "settings.h"

namespace {
enum {ID_PROCESS=100,ID_SCAN,ID_ATTACH,ID_ENABLE,ID_CANDIDATES,ID_BLOCK,ID_ID,ID_ALIAS,ID_ADD,ID_BLACKLIST,ID_REMOVE,ID_COPY,ID_REPORT};
constexpr UINT WM_ATTACHED=WM_APP+1;
HWND window,process_box,attach_button,enable_box,status_label,summary_label,candidates,blacklist,id_edit,alias_edit;
HFONT font;
std::vector<UbProcess> processes;
std::vector<UbEntry> entries;
std::vector<UbCandidate> rows;
UbCandidateHistory candidate_history;
bool rows_dirty=true;
DWORD rendered_second=UINT32_MAX;
HANDLE map_handle=nullptr,ipc_mutex=nullptr,game_handle=nullptr;
UbShared* shared=nullptr;
UbShared snapshot{};
bool connecting=false,settings_failed=false;
uint32_t sequence=UINT32_MAX;
DWORD pid=0;
std::wstring attachment_error;
std::vector<UbModule> game_modules;
bool game_exited=false,exit_code_known=false;
DWORD game_exit_code=0;
std::wstring last_report_path;
struct Reply {DWORD pid=0;std::wstring error;};
bool lock_ipc() {
    if (!ipc_mutex) return false;
    DWORD r=WaitForSingleObject(ipc_mutex,30);return r==WAIT_OBJECT_0||r==WAIT_ABANDONED;
}
void error(const std::wstring& text) {MessageBoxW(window,text.c_str(),L"UNI2 黑名单",MB_ICONERROR);}
void set(HWND h,const std::wstring& s) {SetWindowTextW(h,s.c_str());}
std::wstring text(HWND h) {
    int n=GetWindowTextLengthW(h);std::wstring s(n+1,L'\0');GetWindowTextW(h,s.data(),n+1);s.resize(n);return s;
}
HWND control(const wchar_t* cls,const wchar_t* label,DWORD style,int id) {
    HWND h=CreateWindowExW(wcscmp(cls,WC_LISTVIEWW)==0?WS_EX_CLIENTEDGE:0,cls,label,WS_CHILD|WS_VISIBLE|style,
        0,0,10,10,window,reinterpret_cast<HMENU>(intptr_t(id)),GetModuleHandleW(nullptr),nullptr);
    SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return h;
}
void column(HWND h,int index,const wchar_t* title,int width) {
    LVCOLUMNW c{};c.mask=LVCF_TEXT|LVCF_WIDTH;c.pszText=const_cast<wchar_t*>(title);c.cx=width;
    ListView_InsertColumn(h,index,&c);
}
void cell(HWND h,int row,int col,const std::wstring& value) {
    if (!col) {LVITEMW i{};i.mask=LVIF_TEXT;i.iItem=row;i.pszText=const_cast<wchar_t*>(value.c_str());ListView_InsertItem(h,&i);}
    else ListView_SetItemText(h,row,col,const_cast<wchar_t*>(value.c_str()));
}
void layout() {
    RECT r{};GetClientRect(window,&r);int w=r.right,h=r.bottom;
    auto move=[](int id,int x,int y,int ww,int hh) {MoveWindow(GetDlgItem(window,id),x,y,ww,hh,TRUE);};
    move(ID_PROCESS,18,18,w-345,220);move(ID_SCAN,w-312,18,112,30);move(ID_ATTACH,w-188,18,170,30);
    move(ID_ENABLE,18,57,w-36,26);MoveWindow(status_label,18,91,w-36,44,TRUE);
    MoveWindow(summary_label,18,139,w-36,26,TRUE);
    int table_height=std::max(130,(h-430)*3/5);
    move(ID_CANDIDATES,18,176,w-36,table_height);
    move(ID_BLOCK,18,183+table_height,190,30);move(ID_COPY,220,183+table_height,170,30);
    move(ID_REPORT,w-188,183+table_height,170,30);
    int y=225+table_height;
    move(200,18,y,150,24);move(ID_ID,18,y+25,210,28);move(ID_ALIAS,240,y+25,w-440,28);move(ID_ADD,w-188,y+25,170,28);
    move(201,240,y,w-440,24);move(202,18,y+64,w-36,22);
    move(ID_BLACKLIST,18,y+90,w-216,std::max(60,h-y-128));move(ID_REMOVE,w-188,y+90,170,30);
    move(203,18,h-29,w-36,22);
}
void policy() {
    if (!shared || !lock_ipc()) return;
    shared->enable=SendMessageW(enable_box,BM_GETCHECK,0,0)==BST_CHECKED && !settings_failed;
    shared->blocked_count=static_cast<uint32_t>(entries.size());
    memset(shared->blocked,0,sizeof(shared->blocked));
    for (size_t i=0;i<entries.size();i++) shared->blocked[i]=entries[i].id;
    ++shared->policy_revision;shared->client_tick=GetTickCount();ReleaseMutex(ipc_mutex);
}
void rebuild_blacklist() {
    ListView_DeleteAllItems(blacklist);
    for (size_t i=0;i<entries.size();i++) {cell(blacklist,int(i),0,entries[i].alias);cell(blacklist,int(i),1,std::to_wstring(entries[i].id));}
}
bool contains(uint64_t id) {return std::any_of(entries.begin(),entries.end(),[&](const UbEntry& e){return e.id==id;});}
void add(uint64_t id,std::wstring alias) {
    if (settings_failed) {error(L"原配置读取失败，请先修复配置文件后重开程序，防止覆盖旧黑名单。");return;}
    if (!UbPlayerId(id)) {error(L"请输入完整的十进制个人账号 SteamID64，不能使用 IP 或短 account ID。");return;}
    if (contains(id)) {error(L"该 SteamID64 已在黑名单中。");return;}
    if (entries.size()>=UB_MAX_BLOCKED) {error(L"黑名单最多保存 256 人。");return;}
    auto changed=entries;changed.push_back({id,alias});UbSaveSettings(changed);entries=std::move(changed);
    rebuild_blacklist();policy();rows_dirty=true;
}
void scan() {
    processes=UbFindGames();SendMessageW(process_box,CB_RESETCONTENT,0,0);
    for (const auto& p:processes) {
        auto label=L"PID "+std::to_wstring(p.pid)+L"  "+p.path;
        SendMessageW(process_box,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
    }
    if (!processes.empty()) SendMessageW(process_box,CB_SETCURSEL,0,0);
    EnableWindow(attach_button,!connecting && !processes.empty());
    if (!shared && !game_exited) set(status_label,processes.empty()?L"未连接：先用 Steam 启动游戏，再点击“查找游戏”。":L"选择游戏进程并连接。支持的 EXE 和 Steam DLL 指纹见随包说明。");
}
void disconnect(bool preserve=false) {
    if (shared) UnmapViewOfFile(shared);
    shared=nullptr;
    if (map_handle) CloseHandle(map_handle);
    map_handle=nullptr;
    if (ipc_mutex) CloseHandle(ipc_mutex);
    ipc_mutex=nullptr;
    if (game_handle) CloseHandle(game_handle);
    game_handle=nullptr;
    if (!preserve) {
        pid=0;snapshot={};sequence=UINT32_MAX;rows.clear();candidate_history.clear();game_modules.clear();
        game_exited=false;exit_code_known=false;game_exit_code=0;last_report_path.clear();
        rows_dirty=true;rendered_second=UINT32_MAX;ListView_DeleteAllItems(candidates);
    }
}
DWORD WINAPI attach_worker(void* arg) {
    DWORD target=static_cast<DWORD>(reinterpret_cast<uintptr_t>(arg));
    auto reply=new Reply;
    try {reply->pid=UbAttach(target);} catch(const std::exception& e) {reply->error=UbWide(e.what());}
    catch(...) {reply->error=L"连接失败：无法读取目标进程信息。";}
    if (!PostMessageW(window,WM_ATTACHED,0,reinterpret_cast<LPARAM>(reply))) delete reply;
    return 0;
}
void connect() {
    if (connecting || shared) return;
    LRESULT n=SendMessageW(process_box,CB_GETCURSEL,0,0);
    if (n<0 || size_t(n)>=processes.size()) {error(L"请先选择一个游戏进程。");return;}
    connecting=true;EnableWindow(attach_button,FALSE);set(status_label,L"正在核验文件指纹并连接……");
    HANDLE t=CreateThread(nullptr,0,attach_worker,reinterpret_cast<void*>(uintptr_t(processes[size_t(n)].pid)),0,nullptr);
    if (!t) {connecting=false;error(UbErrorText(GetLastError()));EnableWindow(attach_button,TRUE);} else CloseHandle(t);
}
void attached(Reply* raw) {
    std::unique_ptr<Reply> reply(raw);connecting=false;
    if (!reply->error.empty()) {attachment_error=reply->error;set(status_label,reply->error);EnableWindow(attach_button,TRUE);error(reply->error);return;}
    wchar_t mn[128],xn[128];UbNames(reply->pid,mn,xn);
    map_handle=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,mn);ipc_mutex=OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,xn);
    if (map_handle) shared=static_cast<UbShared*>(MapViewOfFile(map_handle,FILE_MAP_ALL_ACCESS,0,0,sizeof(UbShared)));
    if (!shared || !lock_ipc()) {disconnect();error(L"无法打开插件控制通道。请重启游戏后重试。");EnableWindow(attach_button,TRUE);return;}
    bool valid=shared->magic==UB_MAGIC && shared->abi==UB_ABI && shared->bytes==sizeof(UbShared) && shared->pid==reply->pid;
    ReleaseMutex(ipc_mutex);
    if (!valid) {disconnect();error(L"插件控制通道的身份不匹配。");EnableWindow(attach_button,TRUE);return;}
    pid=reply->pid;game_handle=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if (!game_handle) {disconnect();error(L"无法观察游戏退出状态。");EnableWindow(attach_button,TRUE);return;}
    game_exited=false;exit_code_known=false;game_exit_code=0;last_report_path.clear();
    rows.clear();candidate_history.clear();sequence=UINT32_MAX;rows_dirty=true;rendered_second=UINT32_MAX;
    game_modules=UbListModules(pid);
    if (lock_ipc()) {snapshot=*shared;ReleaseMutex(ipc_mutex);}
    attachment_error.clear();EnableWindow(attach_button,FALSE);set(attach_button,L"已连接");policy();
}
void refresh_candidates(DWORD now) {
    if (sequence!=snapshot.capture_sequence) {
        sequence=snapshot.capture_sequence;
        candidate_history.observe(snapshot.candidates,snapshot.candidate_count,now);
        rows_dirty=true;
    }
    if (candidate_history.expire(now)) rows_dirty=true;
    if (!rows_dirty && rendered_second==now/1000) return;
    const auto& recent=candidate_history.rows();
    bool structure_changed=rows.size()!=recent.size();
    if (!structure_changed) for (size_t i=0;i<rows.size();i++) {
        if (rows[i].steam_id!=recent[i].candidate.steam_id) {structure_changed=true;break;}
    }
    uint64_t selected=0;int current=ListView_GetNextItem(candidates,-1,LVNI_SELECTED);
    if (current>=0 && size_t(current)<rows.size()) selected=rows[size_t(current)].steam_id;
    SendMessageW(candidates,WM_SETREDRAW,FALSE,0);
    if (structure_changed) ListView_DeleteAllItems(candidates);
    rows.clear();rows.reserve(recent.size());
    for (size_t i=0;i<recent.size();i++) {
        const auto& row=recent[i];const auto& c=row.candidate;rows.push_back(c);
        std::wstring name=L"未知";
        if ((c.flags&UB_NAME_KNOWN) && memchr(c.name_utf8,0,sizeof(c.name_utf8))) name=UbWide(c.name_utf8);
        if (structure_changed) cell(candidates,int(i),0,name);
        else ListView_SetItemText(candidates,int(i),0,const_cast<wchar_t*>(name.c_str()));
        cell(candidates,int(i),1,std::to_wstring(c.steam_id));
        cell(candidates,int(i),2,(c.flags&UB_PING_KNOWN)?std::to_wstring(c.estimated_ping_ms)+L" ms":L"未知");
        cell(candidates,int(i),3,L"未知 / 未测");cell(candidates,int(i),4,std::to_wstring(c.lobby_id));
        std::wstring state=contains(c.steam_id)?L"已拉黑 · ":L"未拉黑 · ";
        state+=row.in_latest_result?L"本次返回":L"最近出现";
        cell(candidates,int(i),5,state);
        cell(candidates,int(i),6,std::to_wstring(DWORD(now-row.last_seen)/1000)+L" 秒前");
        if (structure_changed && selected==c.steam_id)
            ListView_SetItemState(candidates,int(i),LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    }
    SendMessageW(candidates,WM_SETREDRAW,TRUE,0);InvalidateRect(candidates,nullptr,TRUE);
    rows_dirty=false;rendered_second=now/1000;
}
std::string diagnostic_report();
bool save_report_file(const std::wstring& path,const std::string& contents);
void tick() {
    if (!shared) return;
    if (game_handle && WaitForSingleObject(game_handle,0)==WAIT_OBJECT_0) {
        if (lock_ipc()) {snapshot=*shared;ReleaseMutex(ipc_mutex);}
        exit_code_known=GetExitCodeProcess(game_handle,&game_exit_code)!=FALSE;game_exited=true;
        disconnect(true);
        auto directory=UbDiagnosticDirectory(true);
        if (!directory.empty()) {
            auto path=directory+L"\\uni2-"+std::to_wstring(pid)+L"-status.txt";
            if (save_report_file(path,diagnostic_report())) last_report_path=path;
        }
        set(attach_button,L"连接并启用");EnableWindow(attach_button,TRUE);
        set(status_label,L"游戏已退出，保护已停止；已保留最后状态，可点击“保存诊断状态”。"+
            (last_report_path.empty()?std::wstring(L" 自动保存失败。"):std::wstring(L" 诊断已自动保存在本地。")));
        return;
    }
    if (!lock_ipc()) {set(status_label,L"插件控制通道忙；尚未取得新状态。");return;}
    shared->client_tick=GetTickCount();snapshot=*shared;ReleaseMutex(ipc_mutex);
    DWORD now=GetTickCount();refresh_candidates(now);
    std::wstring state=UbWide(std::string(snapshot.message_utf8,strnlen(snapshot.message_utf8,256)));
    if (DWORD(GetTickCount()-snapshot.heartbeat)>3000) state=L"插件心跳过期，不能确认保护有效。";
    else if (snapshot.battle_suspended) state+=L"  当前对战中，拦截暂停。";
    else if (!snapshot.blocked_count) state+=L"  黑名单为空：持续观察排位搜索，通信不拦截。";
    else if (!snapshot.enable) state+=L"  保护开关已关闭。";
    else if (snapshot.policy_ack!=snapshot.policy_revision) state+=L"  等待名单生效。";
    else if (!UbEffective(snapshot,GetTickCount())) state+=L"  完整保护未生效。";
    set(status_label,state);
    auto detail=L"搜索 "+std::to_wstring(snapshot.search_count)+L" 次 | 本次捕获 "+std::to_wstring(snapshot.candidate_count)+
        L" 人 | 最近2分钟 "+std::to_wstring(rows.size())+L" 人 | 已拉黑 "+std::to_wstring(entries.size())+
        L" 人 | 跳过 "+std::to_wstring(snapshot.candidate_skips)+L" | 拒绝 "+std::to_wstring(snapshot.request_rejects)+
        L" | 拦截发送 "+std::to_wstring(snapshot.send_rejects)+L" | 丢弃 "+std::to_wstring(snapshot.receive_drops);
    if (candidate_history.omitted()) detail+=L" | 历史已满，省略 "+std::to_wstring(candidate_history.omitted());
    set(summary_label,detail);
}
void copy_id() {
    int n=ListView_GetNextItem(candidates,-1,LVNI_SELECTED);
    if (n<0 || size_t(n)>=rows.size()) return;
    auto s=std::to_wstring(rows[size_t(n)].steam_id);size_t bytes=(s.size()+1)*sizeof(wchar_t);
    HGLOBAL mem=GlobalAlloc(GMEM_MOVEABLE,bytes);
    if (!mem) return;
    void* p=GlobalLock(mem);if (!p) {GlobalFree(mem);return;}memcpy(p,s.c_str(),bytes);GlobalUnlock(mem);
    if (OpenClipboard(window)) {EmptyClipboard();if (!SetClipboardData(CF_UNICODETEXT,mem)) GlobalFree(mem);CloseClipboard();}
    else GlobalFree(mem);
}
std::string hex32(uint32_t value) {char buf[16]{};wsprintfA(buf,"0x%08lx",static_cast<unsigned long>(value));return buf;}
std::string fault_report() {
    std::string result="fault_trace_kind: bounded_first_chance_not_proof_of_fatal_crash\r\n";
    auto path=UbFaultPath(pid);
    HANDLE in=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (in==INVALID_HANDLE_VALUE) return result+"fault_trace_available: 0\r\nfault_trace_read_error: "+std::to_string(GetLastError())+"\r\n";
    result+="fault_trace_available: 1\r\n";
    std::vector<UbFaultRecord> records;
    for (uint32_t i=0;i<UB_FAULT_LIMIT;i++) {
        UbFaultRecord record{};DWORD done=0;
        if (!ReadFile(in,&record,sizeof(record),&done,nullptr)) {result+="fault_trace_read_error: "+std::to_string(GetLastError())+"\r\n";break;}
        if (!done) break;
        if (done!=sizeof(record) || record.magic!=UB_FAULT_MAGIC || record.bytes!=sizeof(record) || record.version!=1 ||
            record.pid!=pid || !record.sequence) {result+="fault_trace_invalid_record: 1\r\n";break;}
        records.push_back(record);
    }
    CloseHandle(in);
    std::sort(records.begin(),records.end(),[](const UbFaultRecord& a,const UbFaultRecord& b){return a.sequence<b.sequence;});
    for (size_t i=0;i<records.size();i++) {
        const auto& record=records[i];std::string prefix="fault."+std::to_string(i)+".";
        auto field=[&](const char* key,const std::string& value){result+=prefix+key+": "+value+"\r\n";};
        field("code",hex32(record.code));field("flags",hex32(record.flags));field("eip",hex32(record.eip));
        field("operation",record.operation==UINT32_MAX?"unknown":std::to_string(record.operation));
        field("fault_address",hex32(record.fault_address));field("allocation_base",hex32(record.allocation_base));
        field("sequence",std::to_string(record.sequence));field("thread",std::to_string(record.tid));field("tick",std::to_string(record.tick));
        field("stage",UbTraceStageName(record.stage));field("search_count",std::to_string(record.search_count));
        bool found=false;
        for (const auto& module:game_modules) if (record.allocation_base==module.base && record.eip>=module.base &&
            uint64_t(record.eip)-module.base<module.bytes) {
            field("module",UbUtf8(module.name));field("module_rva",hex32(record.eip-module.base));found=true;break;
        }
        if (!found) field("module","unknown_or_loaded_after_attachment");
    }
    return result+"fault_trace_records: "+std::to_string(records.size())+"\r\n";
}
std::string diagnostic_report() {
    std::string s=UbStatusReport(snapshot,pid,UbUtf8(attachment_error),GetTickCount(),shared!=nullptr);
    s+="snapshot_source: "+std::string(game_exited?"last_before_exit":shared?"connected":"unconnected")+"\r\n";
    s+="game_exited: "+std::to_string(game_exited)+"\r\nexit_code_known: "+std::to_string(exit_code_known)+"\r\n";
    if (exit_code_known) s+="game_exit_code: "+hex32(game_exit_code)+"\r\n";
    s+="candidate_retention_ms: "+std::to_string(UB_CANDIDATE_RETENTION_MS)+"\r\nrecent_candidate_count: "+
        std::to_string(candidate_history.rows().size())+"\r\nhistory_omitted_rows: "+std::to_string(candidate_history.omitted())+"\r\n";
    if (pid) s+=fault_report();
    return s;
}
bool save_report_file(const std::wstring& path,const std::string& contents) {
    HANDLE out=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);DWORD done=0;
    if (out==INVALID_HANDLE_VALUE) return false;
    bool ok=WriteFile(out,contents.data(),static_cast<DWORD>(contents.size()),&done,nullptr) && done==contents.size() && FlushFileBuffers(out);
    CloseHandle(out);return ok;
}
void report() {
    wchar_t path[32768]=L"uni2-blacklist-status.txt";
    OPENFILENAMEW f{};f.lStructSize=sizeof(f);f.hwndOwner=window;f.lpstrFile=path;f.nMaxFile=32768;
    f.lpstrFilter=L"文本文件\0*.txt\0\0";f.lpstrDefExt=L"txt";f.Flags=OFN_OVERWRITEPROMPT|OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&f)) return;
    if (shared && lock_ipc()) {snapshot=*shared;ReleaseMutex(ipc_mutex);}
    if (!save_report_file(path,diagnostic_report())) error(L"状态文件未完整保存。");
}
LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM w,LPARAM l) {
    try {
        switch (msg) {
        case WM_CREATE: {
            window=h;font=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
            process_box=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,ID_PROCESS);
            control(L"BUTTON",L"查找游戏",BS_PUSHBUTTON|WS_TABSTOP,ID_SCAN);
            attach_button=control(L"BUTTON",L"连接并启用",BS_PUSHBUTTON|WS_TABSTOP,ID_ATTACH);
            enable_box=control(L"BUTTON",L"启用黑名单（已有对战暂停拦截；关闭此窗口将停止保护）",BS_AUTOCHECKBOX|WS_TABSTOP,ID_ENABLE);
            SendMessageW(enable_box,BM_SETCHECK,BST_CHECKED,0);
            status_label=control(L"STATIC",L"",SS_LEFT,204);summary_label=control(L"STATIC",L"最近2分钟出现的玩家：等待游戏自然刷新列表",SS_LEFT,205);
            candidates=control(WC_LISTVIEWW,L"",LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|WS_TABSTOP,ID_CANDIDATES);
            ListView_SetExtendedListViewStyle(candidates,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
            column(candidates,0,L"玩家名（暂未知）",180);column(candidates,1,L"SteamID64",165);column(candidates,2,L"估计延迟（暂未知）",110);
            column(candidates,3,L"实际 RTT / 丢包率",140);column(candidates,4,L"最近 LobbyID",155);column(candidates,5,L"名单 / 搜索结果",170);
            column(candidates,6,L"最近出现",105);
            control(L"BUTTON",L"拉黑选中玩家",BS_PUSHBUTTON|WS_TABSTOP,ID_BLOCK);control(L"BUTTON",L"复制 SteamID64",BS_PUSHBUTTON|WS_TABSTOP,ID_COPY);
            control(L"BUTTON",L"保存诊断状态",BS_PUSHBUTTON|WS_TABSTOP,ID_REPORT);
            control(L"STATIC",L"手动 SteamID64",SS_LEFT,200);id_edit=control(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,ID_ID);
            SendMessageW(id_edit,EM_SETLIMITTEXT,20,0);control(L"STATIC",L"备注（可选）",SS_LEFT,201);
            alias_edit=control(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,ID_ALIAS);SendMessageW(alias_edit,EM_SETLIMITTEXT,128,0);
            control(L"BUTTON",L"添加到黑名单",BS_PUSHBUTTON|WS_TABSTOP,ID_ADD);control(L"STATIC",L"本地黑名单（最多 256 人，按账号拦截）",SS_LEFT,202);
            blacklist=control(WC_LISTVIEWW,L"",LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|WS_TABSTOP,ID_BLACKLIST);
            ListView_SetExtendedListViewStyle(blacklist,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
            column(blacklist,0,L"备注",280);column(blacklist,1,L"SteamID64",230);control(L"BUTTON",L"移除选中玩家",BS_PUSHBUTTON|WS_TABSTOP,ID_REMOVE);
            control(L"STATIC",L"玩家最后出现后保留2分钟；本轮停用额外 Steam 元数据查询，姓名和延迟暂显示未知。最近出现不代表仍待机。",SS_LEFT,203);
            try {entries=UbLoadSettings();} catch(const std::exception& e) {settings_failed=true;SendMessageW(enable_box,BM_SETCHECK,BST_UNCHECKED,0);error(UbWide(e.what()));}
            rebuild_blacklist();scan();layout();SetTimer(h,1,500,nullptr);return 0;
        }
        case WM_SIZE:layout();return 0;
        case WM_GETMINMAXINFO:reinterpret_cast<MINMAXINFO*>(l)->ptMinTrackSize={1000,700};return 0;
        case WM_ATTACHED:attached(reinterpret_cast<Reply*>(l));return 0;
        case WM_TIMER:tick();return 0;
        case WM_COMMAND:
            switch (LOWORD(w)) {
            case ID_SCAN:scan();break;
            case ID_ATTACH:connect();break;
            case ID_ENABLE:policy();break;
            case ID_BLOCK: {int n=ListView_GetNextItem(candidates,-1,LVNI_SELECTED);
                if (n>=0 && size_t(n)<rows.size()) add(rows[size_t(n)].steam_id,UbWide(rows[size_t(n)].name_utf8));
                else error(L"请在搜索结果中选择一位玩家。");
                break;}
            case ID_ADD:add(UbParseId(text(id_edit)),text(alias_edit));break;
            case ID_REMOVE: {int n=ListView_GetNextItem(blacklist,-1,LVNI_SELECTED);
                if (n>=0 && size_t(n)<entries.size() && !settings_failed) {auto changed=entries;changed.erase(changed.begin()+n);
                    UbSaveSettings(changed);entries=std::move(changed);rebuild_blacklist();policy();rows_dirty=true;}break;}
            case ID_COPY:copy_id();break;
            case ID_REPORT:report();break;
            }return 0;
        case WM_CLOSE:
            if (connecting) {set(status_label,L"请等待当前连接操作结束，最长约 30 秒，然后关闭窗口。");return 0;}
            SendMessageW(enable_box,BM_SETCHECK,BST_UNCHECKED,0);policy();disconnect();DestroyWindow(h);return 0;
        case WM_DESTROY:KillTimer(h,1);DeleteObject(font);PostQuitMessage(0);return 0;
        }
    } catch(const std::exception& e) {error(UbWide(e.what()));}
    catch(...) {error(L"操作失败，原有配置保持不变。");}
    return DefWindowProcW(h,msg,w,l);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show) {
    SetProcessDPIAware();
    HANDLE singleton=CreateMutexW(nullptr,TRUE,L"Local\\UNI2Blacklist-GUI-v1");
    if (!singleton || GetLastError()==ERROR_ALREADY_EXISTS) {MessageBoxW(nullptr,L"黑名单窗口已运行。",L"UNI2 黑名单",MB_ICONINFORMATION);if(singleton)CloseHandle(singleton);return 1;}
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&common);
    WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.hInstance=instance;cls.lpszClassName=L"UNI2BlacklistWindow";
    cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hIcon=LoadIconW(nullptr,IDI_APPLICATION);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
    RegisterClassW(&cls);
    window=CreateWindowExW(WS_EX_CONTROLPARENT,cls.lpszClassName,(L"UNI2 黑名单 "+UbWide(UB_VERSION)).c_str(),WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,CW_USEDEFAULT,1120,840,nullptr,nullptr,instance,nullptr);
    if (!window) {CloseHandle(singleton);return 2;}
    ShowWindow(window,show);UpdateWindow(window);
    MSG msg{};while (GetMessageW(&msg,nullptr,0,0)>0) {if (!IsDialogMessageW(window,&msg)) {TranslateMessage(&msg);DispatchMessageW(&msg);}}
    CloseHandle(singleton);return int(msg.wParam);
}
