#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <uxtheme.h>
#include <algorithm>
#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "protocol.h"
#include "status_report.h"
#include "candidate_history.h"
#include "activity_history.h"
#include "fault_trace.h"
#include "injector.h"
#include "settings.h"
#include "ui_language.h"

namespace {
enum {ID_PROCESS=100,ID_SCAN,ID_ATTACH,ID_ENABLE,ID_CANDIDATES,ID_BLOCK,
    ID_BLACKLIST=109,ID_REMOVE,ID_REPORT=112,ID_WIFI,ID_LANGUAGE,ID_ACTIVITY,ID_CLEAR,ID_ACTIVITY_LIST=118};
constexpr UINT WM_ATTACHED=WM_APP+1;
constexpr COLORREF background=RGB(244,246,250),ink=RGB(30,41,59),muted=RGB(100,116,139),accent=RGB(37,99,235),line=RGB(226,232,240);
HWND window,process_box,attach_button,enable_box,wifi_box,status_label,summary_label,blacklist_heading,candidates,blacklist,language_box;
HWND activity_window,activity_list;
std::array<HWND,4> activity_labels{},activity_counts{};
HFONT font,title_font,bold_font,count_font;
HBRUSH background_brush,white_brush;
HIMAGELIST list_spacing;
UINT dpi=96;
RECT player_card{},blacklist_card{};
UbLanguage language=UbLanguage::Chinese;
std::vector<UbProcess> processes;
std::vector<UbEntry> entries;
std::vector<UbCandidate> rows;
UbCandidateHistory candidate_history;
UbActivityHistory activity_history;
bool rows_dirty=true;
DWORD rendered_second=UINT32_MAX;
HANDLE map_handle=nullptr,ipc_mutex=nullptr,game_handle=nullptr;
UbShared* shared=nullptr;
UbShared snapshot{};
bool connecting=false,settings_failed=false,wifi_option=false;
uint32_t sequence=UINT32_MAX;
DWORD pid=0;
std::wstring attachment_error;
std::vector<UbModule> game_modules;
bool game_exited=false,exit_code_known=false;
DWORD game_exit_code=0;
std::wstring last_report_path;
struct Reply {DWORD pid=0;std::wstring error;};
const wchar_t* tr(const wchar_t* zh,const wchar_t* en) {return language==UbLanguage::Chinese?zh:en;}
const wchar_t* app_name() {return tr(L"UNI2 黑名单",L"UNI2 Blacklist");}
const wchar_t* activity_name(UbActivityKind kind) {
    switch (kind) {
    case UbActivityKind::Skipped:return tr(L"跳过",L"Skipped");
    case UbActivityKind::Rejected:return tr(L"拒绝",L"Rejected");
    case UbActivityKind::Intercepted:return tr(L"拦截",L"Intercepted");
    case UbActivityKind::Dropped:return tr(L"丢弃",L"Dropped");
    }
    return L"";
}
template<class Fn> Fn api(HMODULE module,const char* name) {
    FARPROC address=GetProcAddress(module,name);Fn result=nullptr;static_assert(sizeof(result)==sizeof(address));
    memcpy(&result,&address,sizeof(result));return result;
}
int px(int value) {return MulDiv(value,dpi,96);}
UINT window_dpi(HWND h) {
    using Fn=UINT(WINAPI*)(HWND);
    auto fn=api<Fn>(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow");
    if (fn) {UINT value=fn(h);if (value) return value;}
    HDC dc=GetDC(h);UINT value=UINT(GetDeviceCaps(dc,LOGPIXELSX));ReleaseDC(h,dc);return value?value:96;
}
void set(HWND h,const std::wstring& value) {if (h) SetWindowTextW(h,value.c_str());}
void error(const std::wstring& value) {MessageBoxW(window,UbTranslateError(value,language).c_str(),app_name(),MB_ICONERROR);}
bool lock_ipc() {
    if (!ipc_mutex) return false;
    DWORD r=WaitForSingleObject(ipc_mutex,30);return r==WAIT_OBJECT_0||r==WAIT_ABANDONED;
}
void rounded(HDC dc,RECT rect,COLORREF fill,COLORREF border,int radius=10) {
    HBRUSH brush=CreateSolidBrush(fill);HPEN pen=CreatePen(PS_SOLID,1,border);
    auto old_brush=SelectObject(dc,brush);auto old_pen=SelectObject(dc,pen);
    RoundRect(dc,rect.left,rect.top,rect.right,rect.bottom,px(radius),px(radius));
    SelectObject(dc,old_brush);SelectObject(dc,old_pen);DeleteObject(brush);DeleteObject(pen);
}
LRESULT CALLBACK button_proc(HWND h,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR) {
    if (message==WM_MOUSEMOVE && !GetPropW(h,L"hover")) {
        SetPropW(h,L"hover",reinterpret_cast<HANDLE>(1));TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,h,0};
        TrackMouseEvent(&track);InvalidateRect(h,nullptr,FALSE);
    } else if (message==WM_MOUSELEAVE) {RemovePropW(h,L"hover");InvalidateRect(h,nullptr,FALSE);}
    return DefSubclassProc(h,message,w,l);
}
LRESULT CALLBACK list_proc(HWND h,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR) {
    LRESULT result=DefSubclassProc(h,message,w,l);
    if ((message==WM_PAINT || message==WM_PRINTCLIENT) && !ListView_GetItemCount(h)) {
        HDC dc=message==WM_PRINTCLIENT?reinterpret_cast<HDC>(w):GetDC(h);RECT r{};GetClientRect(h,&r);r.top=px(64);r.bottom=r.top+px(48);
        SelectObject(dc,font);SetTextColor(dc,muted);SetBkMode(dc,TRANSPARENT);
        const wchar_t* label=h==candidates?tr(L"暂无玩家",L"No players yet"):
            h==blacklist?tr(L"黑名单为空",L"No blocked players"):tr(L"暂无记录",L"No requests yet");
        DrawTextW(dc,label,-1,&r,DT_CENTER|DT_SINGLELINE|DT_VCENTER);if (message==WM_PAINT) ReleaseDC(h,dc);
    }
    return result;
}
HWND control(const wchar_t* cls,const wchar_t* label,DWORD style,int id,HWND parent=nullptr) {
    // The GUI owns one spacing image list shared by all three tables.
    if (wcscmp(cls,WC_LISTVIEWW)==0) style|=LVS_SHAREIMAGELISTS;
    HWND h=CreateWindowExW(0,cls,label,WS_CHILD|WS_VISIBLE|style,0,0,10,10,parent?parent:window,
        reinterpret_cast<HMENU>(intptr_t(id)),GetModuleHandleW(nullptr),nullptr);
    SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
    if (wcscmp(cls,WC_LISTVIEWW)==0) {
        SetWindowTheme(h,L"Explorer",nullptr);ListView_SetExtendedListViewStyle(h,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
        ListView_SetBkColor(h,RGB(255,255,255));ListView_SetTextBkColor(h,RGB(255,255,255));ListView_SetTextColor(h,ink);
        ListView_SetImageList(h,list_spacing,LVSIL_SMALL);SetWindowSubclass(h,list_proc,1,0);
    } else if (wcscmp(cls,L"BUTTON")==0 && (style&BS_TYPEMASK)==BS_OWNERDRAW) SetWindowSubclass(h,button_proc,1,0);
    return h;
}
void column(HWND h,int index,const wchar_t* title,int width) {
    LVCOLUMNW c{};c.mask=LVCF_TEXT|LVCF_WIDTH;c.pszText=const_cast<wchar_t*>(title);c.cx=width;ListView_InsertColumn(h,index,&c);
}
void heading(HWND h,int index,const wchar_t* title) {
    LVCOLUMNW c{};c.mask=LVCF_TEXT;c.pszText=const_cast<wchar_t*>(title);ListView_SetColumn(h,index,&c);
}
void cell(HWND h,int row,int col,const std::wstring& value) {
    if (!col) {LVITEMW i{};i.mask=LVIF_TEXT;i.iItem=row;i.pszText=const_cast<wchar_t*>(value.c_str());ListView_InsertItem(h,&i);}
    else ListView_SetItemText(h,row,col,const_cast<wchar_t*>(value.c_str()));
}
void fonts() {
    for (auto value:{font,title_font,bold_font,count_font}) if (value) DeleteObject(value);
    const wchar_t* face=language==UbLanguage::Chinese?L"Microsoft YaHei UI":L"Segoe UI";
    auto make=[&](int size,int weight) {return CreateFontW(-px(size),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,face);};
    font=make(13,FW_NORMAL);title_font=make(21,FW_SEMIBOLD);bold_font=make(13,FW_SEMIBOLD);count_font=make(24,FW_SEMIBOLD);
    for (HWND parent:{window,activity_window}) if (parent) {
        for (HWND child=GetWindow(parent,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))
            SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
    }
    if (window) {
        SendMessageW(GetDlgItem(window,200),WM_SETFONT,reinterpret_cast<WPARAM>(title_font),TRUE);
        for (HWND h:{summary_label,blacklist_heading}) SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(bold_font),TRUE);
    }
    for (HWND h:activity_counts) if (h) SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(count_font),TRUE);
    if (list_spacing) ImageList_Destroy(list_spacing);
    list_spacing=ImageList_Create(px(1),px(30),ILC_COLOR32,1,1);
    for (HWND h:{candidates,blacklist,activity_list}) if (h) ListView_SetImageList(h,list_spacing,LVSIL_SMALL);
}
void selection_buttons() {
    EnableWindow(GetDlgItem(window,ID_BLOCK),!settings_failed && ListView_GetNextItem(candidates,-1,LVNI_SELECTED)>=0);
    EnableWindow(GetDlgItem(window,ID_REMOVE),!settings_failed && ListView_GetNextItem(blacklist,-1,LVNI_SELECTED)>=0);
}
void layout_activity() {
    if (!activity_window) return;
    RECT r{};GetClientRect(activity_window,&r);int w=r.right,h=r.bottom;
    int gap=px(12),margin=px(20),width=(w-2*margin-3*gap)/4;
    for (size_t i=0;i<4;i++) {
        int x=margin+int(i)*(width+gap);
        MoveWindow(activity_labels[i],x+px(12),px(26),width-px(24),px(20),TRUE);
        MoveWindow(activity_counts[i],x+px(12),px(51),width-px(24),px(32),TRUE);
    }
    MoveWindow(activity_list,margin+px(10),px(122),w-2*margin-px(20),h-px(188),TRUE);
    ListView_SetColumnWidth(activity_list,0,px(152));ListView_SetColumnWidth(activity_list,1,w-px(294));
    ListView_SetColumnWidth(activity_list,2,px(72));
    MoveWindow(GetDlgItem(activity_window,ID_CLEAR),w-margin-px(108),h-px(50),px(108),px(32),TRUE);
    InvalidateRect(activity_window,nullptr,TRUE);
}
void layout() {
    if (!window || !candidates) return;
    RECT r{};GetClientRect(window,&r);int w=r.right,h=r.bottom;
    auto move=[&](int id,int x,int y,int ww,int hh) {MoveWindow(GetDlgItem(window,id),x,y,ww,hh,TRUE);};
    move(200,px(68),px(23),w-px(250),px(34));
    move(ID_LANGUAGE,w-px(152),px(22),px(132),px(220));
    move(ID_PROCESS,px(20),px(80),w-px(280),px(220));
    move(ID_SCAN,w-px(244),px(78),px(88),px(34));move(ID_ATTACH,w-px(148),px(78),px(128),px(34));
    move(ID_ENABLE,px(20),px(128),px(185),px(26));move(ID_WIFI,px(218),px(128),px(200),px(26));
    MoveWindow(status_label,w-px(208),px(129),px(188),px(24),TRUE);
    int right=std::clamp(w/3,px(210),px(270));int left=w-px(56)-right;int bottom=h-px(66);
    player_card={px(20),px(176),px(20)+left,bottom};blacklist_card={player_card.right+px(16),px(176),w-px(20),bottom};
    MoveWindow(summary_label,player_card.left+px(14),px(190),left-px(28),px(23),TRUE);
    MoveWindow(blacklist_heading,blacklist_card.left+px(14),px(190),right-px(28),px(23),TRUE);
    move(ID_CANDIDATES,player_card.left+px(10),px(219),left-px(20),bottom-px(229));
    move(ID_BLACKLIST,blacklist_card.left+px(10),px(219),right-px(20),bottom-px(229));
    const int widths[]={left-px(20+78+82+92+66+19),px(78),px(82),px(92),px(66)};
    for (int i=0;i<5;i++) ListView_SetColumnWidth(candidates,i,widths[i]);
    ListView_SetColumnWidth(blacklist,0,right-px(40));
    move(ID_BLOCK,px(20),bottom+px(12),px(118),px(34));move(ID_ACTIVITY,px(148),bottom+px(12),px(126),px(34));
    move(ID_REPORT,px(284),bottom+px(12),px(100),px(34));
    move(ID_REMOVE,blacklist_card.left,bottom+px(12),right,px(34));
    InvalidateRect(window,nullptr,TRUE);layout_activity();
}
void policy() {
    if (!shared || !lock_ipc()) return;
    shared->enable=SendMessageW(enable_box,BM_GETCHECK,0,0)==BST_CHECKED && !settings_failed;
    shared->exclude_wifi=wifi_option && !settings_failed;
    shared->blocked_count=static_cast<uint32_t>(entries.size());memset(shared->blocked,0,sizeof(shared->blocked));
    for (size_t i=0;i<entries.size();i++) shared->blocked[i]=entries[i].id;
    ++shared->policy_revision;shared->client_tick=GetTickCount();ReleaseMutex(ipc_mutex);
}
void refresh_counts() {
    set(summary_label,std::wstring(tr(L"玩家  ",L"Players  "))+std::to_wstring(rows.size()));
    set(blacklist_heading,std::wstring(tr(L"黑名单  ",L"Blocklist  "))+std::to_wstring(entries.size()));
}
void rebuild_blacklist() {
    int selected=ListView_GetNextItem(blacklist,-1,LVNI_SELECTED);uint64_t id=0;
    if (selected>=0 && size_t(selected)<entries.size()) id=entries[size_t(selected)].id;
    ListView_DeleteAllItems(blacklist);
    for (size_t i=0;i<entries.size();i++) {
        auto name=entries[i].alias.empty()?std::wstring(tr(L"玩家 ",L"Player "))+std::to_wstring(entries[i].id&0xffffffffu):entries[i].alias;
        cell(blacklist,int(i),0,name);
        if (entries[i].id==id) ListView_SetItemState(blacklist,int(i),LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    }
    refresh_counts();selection_buttons();
}
bool contains(uint64_t id) {return std::any_of(entries.begin(),entries.end(),[&](const UbEntry& e){return e.id==id;});}
void add(uint64_t id,const std::wstring& alias) {
    if (settings_failed) {error(L"原配置读取失败，请先修复配置文件后重开程序，防止覆盖旧黑名单。");return;}
    if (!UbPlayerId(id)) {error(tr(L"无法识别这个玩家，请等待列表刷新。",L"Cannot identify this player. Wait for the list to refresh."));return;}
    if (contains(id)) {error(L"该 SteamID64 已在黑名单中。");return;}
    if (entries.size()>=UB_MAX_BLOCKED) {error(L"黑名单最多保存 256 人。");return;}
    auto changed=entries;changed.push_back({id,alias});UbSaveSettings(changed);entries=std::move(changed);
    rebuild_blacklist();policy();rows_dirty=true;
}
void display_status() {
    const wchar_t* value=tr(L"未连接",L"Not connected");
    if (settings_failed) value=tr(L"配置错误",L"Settings error");
    else if (connecting) value=tr(L"连接中…",L"Connecting…");
    else if (game_exited) value=tr(L"游戏已退出",L"Game closed");
    else if (!attachment_error.empty() && !shared) value=tr(L"连接失败",L"Connection failed");
    else if (shared) {
        if (snapshot.state==UB_ERROR || snapshot.state==UB_UNSUPPORTED || snapshot.status!=UB_OK) value=tr(L"连接异常",L"Connection error");
        else if (DWORD(GetTickCount()-snapshot.heartbeat)>3000) value=tr(L"连接中断",L"Disconnected");
        else if (snapshot.battle_suspended) value=tr(L"对战中",L"In battle");
        else if (!snapshot.enable) value=tr(L"已停用",L"Disabled");
        else if (!UbHasPolicy(snapshot)) value=tr(L"观察中",L"Observing");
        else if (snapshot.policy_ack!=snapshot.policy_revision) value=tr(L"应用中",L"Applying");
        else if (UbEffective(snapshot,GetTickCount())) value=tr(L"筛选生效",L"Filtering");
        else value=tr(L"等待就绪",L"Waiting");
    } else if (processes.empty()) value=tr(L"请启动游戏",L"Start the game");
    set(status_label,value);
}
void scan() {
    processes=UbFindGames();SendMessageW(process_box,CB_RESETCONTENT,0,0);
    for (const auto& p:processes) {
        auto label=L"uni2.exe  ·  PID "+std::to_wstring(p.pid);
        SendMessageW(process_box,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
    }
    if (!processes.empty()) SendMessageW(process_box,CB_SETCURSEL,0,0);
    EnableWindow(attach_button,!connecting && !shared && !processes.empty());display_status();
}
void refresh_activity() {
    if (!activity_window) return;
    for (size_t i=0;i<4;i++) set(activity_counts[i],std::to_wstring(activity_history.totals()[i]));
    int top=ListView_GetTopIndex(activity_list);bool at_latest=top<=0;
    SendMessageW(activity_list,WM_SETREDRAW,FALSE,0);ListView_DeleteAllItems(activity_list);
    const auto& history=activity_history.rows();
    for (size_t n=0;n<history.size();n++) {
        const auto& row=history[history.size()-1-n];wchar_t time[24]{};
        wsprintfW(time,L"%02u:%02u:%02u",row.observed_at.wHour,row.observed_at.wMinute,row.observed_at.wSecond);
        cell(activity_list,int(n),0,time);cell(activity_list,int(n),1,activity_name(row.kind));cell(activity_list,int(n),2,std::to_wstring(row.count));
    }
    if (!at_latest && top<int(history.size())) ListView_EnsureVisible(activity_list,top,FALSE);
    SendMessageW(activity_list,WM_SETREDRAW,TRUE,0);InvalidateRect(activity_list,nullptr,TRUE);
}
void observe_activity() {
    const auto before=activity_history.totals();size_t count=activity_history.rows().size();
    SYSTEMTIME now{};GetLocalTime(&now);activity_history.observe(snapshot,now);
    if (before!=activity_history.totals() || count!=activity_history.rows().size()) refresh_activity();
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
        pid=0;snapshot={};sequence=UINT32_MAX;rows.clear();candidate_history.clear();activity_history.clear_context();game_modules.clear();
        game_exited=false;exit_code_known=false;game_exit_code=0;last_report_path.clear();
        rows_dirty=true;rendered_second=UINT32_MAX;ListView_DeleteAllItems(candidates);refresh_activity();refresh_counts();
    }
}
DWORD WINAPI attach_worker(void* arg) {
    DWORD target=static_cast<DWORD>(reinterpret_cast<uintptr_t>(arg));auto reply=new Reply;
    try {reply->pid=UbAttach(target);} catch(const std::exception& e) {reply->error=UbWide(e.what());}
    catch(...) {reply->error=L"连接失败：无法读取目标进程信息。";}
    if (!PostMessageW(window,WM_ATTACHED,0,reinterpret_cast<LPARAM>(reply))) delete reply;
    return 0;
}
void connect() {
    if (connecting || shared) return;
    LRESULT n=SendMessageW(process_box,CB_GETCURSEL,0,0);
    if (n<0 || size_t(n)>=processes.size()) {error(L"请先选择一个游戏进程。");return;}
    connecting=true;EnableWindow(attach_button,FALSE);display_status();
    HANDLE t=CreateThread(nullptr,0,attach_worker,reinterpret_cast<void*>(uintptr_t(processes[size_t(n)].pid)),0,nullptr);
    if (!t) {connecting=false;error(UbErrorText(GetLastError()));EnableWindow(attach_button,TRUE);display_status();} else CloseHandle(t);
}
void attached(Reply* raw) {
    std::unique_ptr<Reply> reply(raw);connecting=false;
    if (!reply->error.empty()) {attachment_error=reply->error;display_status();EnableWindow(attach_button,TRUE);error(reply->error);return;}
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
    activity_history.begin(snapshot);refresh_activity();attachment_error.clear();EnableWindow(attach_button,FALSE);
    set(attach_button,tr(L"已连接",L"Connected"));policy();display_status();
}
void refresh_candidates(DWORD now) {
    if (sequence!=snapshot.capture_sequence) {
        sequence=snapshot.capture_sequence;candidate_history.observe(snapshot.candidates,snapshot.candidate_count,now);rows_dirty=true;
    }
    if (candidate_history.expire(now)) rows_dirty=true;
    if (!rows_dirty && rendered_second==now/1000) return;
    const auto& recent=candidate_history.rows();bool structure_changed=rows.size()!=recent.size();
    if (!structure_changed) for (size_t i=0;i<rows.size();i++) if (rows[i].steam_id!=recent[i].candidate.steam_id) {structure_changed=true;break;}
    uint64_t selected=0;int current=ListView_GetNextItem(candidates,-1,LVNI_SELECTED);
    if (current>=0 && size_t(current)<rows.size()) selected=rows[size_t(current)].steam_id;
    SendMessageW(candidates,WM_SETREDRAW,FALSE,0);
    if (structure_changed) ListView_DeleteAllItems(candidates);
    rows.clear();rows.reserve(recent.size());
    for (size_t i=0;i<recent.size();i++) {
        const auto& row=recent[i];const auto& c=row.candidate;rows.push_back(c);std::wstring name=tr(L"未知",L"Unknown");
        if ((c.flags&UB_NAME_KNOWN) && memchr(c.name_utf8,0,sizeof(c.name_utf8))) name=UbWide(c.name_utf8);
        if (structure_changed) cell(candidates,int(i),0,name);
        else ListView_SetItemText(candidates,int(i),0,const_cast<wchar_t*>(name.c_str()));
        cell(candidates,int(i),1,(c.flags&UB_PING_KNOWN)?std::to_wstring(c.estimated_ping_ms)+L" ms":tr(L"未知",L"Unknown"));
        cell(candidates,int(i),2,c.connection_type==UB_CONNECTION_WIFI?L"Wi-Fi":c.connection_type==UB_CONNECTION_WIRED?tr(L"有线",L"Wired"):tr(L"未知",L"Unknown"));
        std::wstring state=contains(c.steam_id)?tr(L"已拉黑",L"Blocked"):
            wifi_option && (c.flags&UB_WIFI_EXCLUDED)?tr(L"Wi-Fi 排除",L"Wi-Fi excluded"):
            row.in_latest_result?tr(L"本次返回",L"Current"):tr(L"最近出现",L"Recent");
        cell(candidates,int(i),3,state);
        cell(candidates,int(i),4,std::to_wstring(DWORD(now-row.last_seen)/1000)+tr(L"秒前",L"s ago"));
        if (structure_changed && selected==c.steam_id) ListView_SetItemState(candidates,int(i),LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    }
    SendMessageW(candidates,WM_SETREDRAW,TRUE,0);InvalidateRect(candidates,nullptr,TRUE);
    rows_dirty=false;rendered_second=now/1000;selection_buttons();refresh_counts();
}
std::string diagnostic_report();
bool save_report_file(const std::wstring& path,const std::string& contents);
void tick() {
    if (!shared) return;
    if (game_handle && WaitForSingleObject(game_handle,0)==WAIT_OBJECT_0) {
        if (lock_ipc()) {snapshot=*shared;ReleaseMutex(ipc_mutex);}
        observe_activity();exit_code_known=GetExitCodeProcess(game_handle,&game_exit_code)!=FALSE;game_exited=true;disconnect(true);
        auto directory=UbDiagnosticDirectory(true);
        if (!directory.empty()) {
            auto path=directory+L"\\uni2-"+std::to_wstring(pid)+L"-status.txt";
            if (save_report_file(path,diagnostic_report())) last_report_path=path;
        }
        set(attach_button,tr(L"连接",L"Connect"));EnableWindow(attach_button,TRUE);display_status();return;
    }
    if (!lock_ipc()) {set(status_label,tr(L"等待状态",L"Waiting for status"));return;}
    shared->client_tick=GetTickCount();snapshot=*shared;ReleaseMutex(ipc_mutex);
    observe_activity();refresh_candidates(GetTickCount());display_status();
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
    f.lpstrFilter=language==UbLanguage::Chinese?L"文本文件\0*.txt\0\0":L"Text files\0*.txt\0\0";
    f.lpstrDefExt=L"txt";f.Flags=OFN_OVERWRITEPROMPT|OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&f)) return;
    if (shared && lock_ipc()) {snapshot=*shared;ReleaseMutex(ipc_mutex);}
    if (!save_report_file(path,diagnostic_report())) error(L"状态文件未完整保存。");
}
void apply_language() {
    set(window,app_name());set(GetDlgItem(window,200),app_name());
    set(GetDlgItem(window,ID_SCAN),tr(L"刷新",L"Refresh"));
    set(attach_button,shared?tr(L"已连接",L"Connected"):tr(L"连接",L"Connect"));
    set(enable_box,tr(L"启用拦截",L"Enable filtering"));set(wifi_box,tr(L"排除 Wi-Fi",L"Exclude Wi-Fi"));
    set(GetDlgItem(window,ID_BLOCK),tr(L"拉黑玩家",L"Block player"));
    set(GetDlgItem(window,ID_REMOVE),tr(L"解除拉黑",L"Unblock"));
    set(GetDlgItem(window,ID_REPORT),tr(L"诊断",L"Diagnostics"));
    set(GetDlgItem(window,ID_ACTIVITY),tr(L"请求记录",L"Request activity"));
    const wchar_t* names[]={tr(L"玩家",L"Player"),tr(L"延迟",L"Latency"),tr(L"网络",L"Network"),tr(L"状态",L"Status"),tr(L"最近",L"Seen")};
    for (int i=0;i<5;i++) heading(candidates,i,names[i]);
    heading(blacklist,0,tr(L"玩家",L"Player"));
    if (activity_window) {
        set(activity_window,tr(L"请求记录",L"Request activity"));
        for (size_t i=0;i<4;i++) set(activity_labels[i],activity_name(static_cast<UbActivityKind>(i)));
        heading(activity_list,0,tr(L"观察时间",L"Observed at"));heading(activity_list,1,tr(L"操作",L"Action"));heading(activity_list,2,tr(L"数量",L"Count"));
        set(GetDlgItem(activity_window,ID_CLEAR),tr(L"清空记录",L"Clear records"));
    }
    fonts();rebuild_blacklist();rows_dirty=true;refresh_candidates(GetTickCount());refresh_activity();display_status();layout();
}
void draw_button(const DRAWITEMSTRUCT& item) {
    bool primary=item.CtlID==ID_ATTACH,disabled=(item.itemState&ODS_DISABLED)!=0;
    bool hover=GetPropW(item.hwndItem,L"hover")!=nullptr,pressed=(item.itemState&ODS_SELECTED)!=0;
    COLORREF fill=primary?accent:RGB(255,255,255),border=primary?accent:line,text_color=primary?RGB(255,255,255):ink;
    if (disabled) {fill=RGB(235,239,245);border=RGB(235,239,245);text_color=RGB(148,163,184);}
    else if (pressed) {fill=primary?RGB(29,78,216):RGB(226,232,240);}
    else if (hover) {fill=primary?RGB(29,78,216):RGB(241,245,249);}
    FillRect(item.hDC,&item.rcItem,background_brush);RECT r=item.rcItem;InflateRect(&r,-px(1),-px(1));rounded(item.hDC,r,fill,border,8);
    wchar_t text[256]{};GetWindowTextW(item.hwndItem,text,256);SelectObject(item.hDC,bold_font);SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,text_color);
    DrawTextW(item.hDC,text,-1,&r,DT_CENTER|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
    if ((item.itemState&ODS_FOCUS) && !(item.itemState&ODS_NOFOCUSRECT)) {InflateRect(&r,-px(4),-px(4));DrawFocusRect(item.hDC,&r);}
}
LRESULT colors(HWND child,HDC dc) {
    SetBkMode(dc,TRANSPARENT);SetTextColor(dc,child==status_label?muted:ink);
    int id=GetDlgCtrlID(child);bool white=id==202 || id==205 || (id>=300 && id<=307);
    return reinterpret_cast<LRESULT>(white?white_brush:background_brush);
}
void draw_main(HWND h,HDC dc) {
    RECT r{};GetClientRect(h,&r);FillRect(dc,&r,background_brush);
    rounded(dc,player_card,RGB(255,255,255),line,12);rounded(dc,blacklist_card,RGB(255,255,255),line,12);
    RECT mark{px(20),px(20),px(56),px(56)};rounded(dc,mark,accent,accent,10);SelectObject(dc,bold_font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(255,255,255));
    DrawTextW(dc,L"UB",-1,&mark,DT_CENTER|DT_SINGLELINE|DT_VCENTER);
}
void round_window(HWND h) {
    HMODULE module=LoadLibraryW(L"dwmapi.dll");
    if (module) {
        using Fn=HRESULT(WINAPI*)(HWND,DWORD,LPCVOID,DWORD);
        auto fn=api<Fn>(module,"DwmSetWindowAttribute");DWORD preference=2;
        if (fn) fn(h,33,&preference,sizeof(preference));
        FreeLibrary(module);
    }
}
void draw_activity(HWND h,HDC dc) {
    RECT r{};GetClientRect(h,&r);FillRect(dc,&r,background_brush);
    int gap=px(12),margin=px(20),width=(r.right-2*margin-3*gap)/4;
    for (int i=0;i<4;i++) {RECT tile{margin+i*(width+gap),px(16),margin+i*(width+gap)+width,px(99)};rounded(dc,tile,RGB(255,255,255),line,10);}
    RECT table{margin,px(111),r.right-margin,r.bottom-px(61)};rounded(dc,table,RGB(255,255,255),line,10);
}
LRESULT CALLBACK activity_proc(HWND h,UINT message,WPARAM w,LPARAM l) {
    switch (message) {
    case WM_CREATE:
        activity_window=h;round_window(h);
        for (size_t i=0;i<4;i++) {
            activity_labels[i]=control(L"STATIC",activity_name(static_cast<UbActivityKind>(i)),SS_LEFT,300+int(i),h);
            activity_counts[i]=control(L"STATIC",L"0",SS_LEFT,304+int(i),h);
            SendMessageW(activity_counts[i],WM_SETFONT,reinterpret_cast<WPARAM>(count_font),TRUE);
        }
        activity_list=control(WC_LISTVIEWW,L"",LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|WS_TABSTOP,ID_ACTIVITY_LIST,h);
        column(activity_list,0,tr(L"观察时间",L"Observed at"),px(152));column(activity_list,1,tr(L"操作",L"Action"),px(230));column(activity_list,2,tr(L"数量",L"Count"),px(72));
        control(L"BUTTON",tr(L"清空记录",L"Clear records"),BS_OWNERDRAW|WS_TABSTOP,ID_CLEAR,h);
        layout_activity();refresh_activity();return 0;
    case WM_SIZE:layout_activity();return 0;
    case WM_GETMINMAXINFO:reinterpret_cast<MINMAXINFO*>(l)->ptMinTrackSize={px(530),px(350)};return 0;
    case WM_COMMAND:if (LOWORD(w)==ID_CLEAR) {activity_history.clear_view();refresh_activity();}return 0;
    case WM_DRAWITEM:draw_button(*reinterpret_cast<DRAWITEMSTRUCT*>(l));return TRUE;
    case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:return colors(reinterpret_cast<HWND>(l),reinterpret_cast<HDC>(w));
    case WM_PAINT: {PAINTSTRUCT paint{};HDC dc=BeginPaint(h,&paint);draw_activity(h,dc);EndPaint(h,&paint);return 0;}
    case WM_PRINTCLIENT:draw_activity(h,reinterpret_cast<HDC>(w));return 0;
    case WM_CLOSE:DestroyWindow(h);return 0;
    case WM_DESTROY:activity_window=nullptr;activity_list=nullptr;activity_labels={};activity_counts={};return 0;
    }
    return DefWindowProcW(h,message,w,l);
}
void open_activity() {
    if (activity_window) {ShowWindow(activity_window,SW_SHOWNORMAL);SetForegroundWindow(activity_window);return;}
    RECT parent{};GetWindowRect(window,&parent);
    HWND h=CreateWindowExW(WS_EX_CONTROLPARENT|WS_EX_TOOLWINDOW,L"UNI2BlacklistActivityWindow",tr(L"请求记录",L"Request activity"),
        WS_OVERLAPPEDWINDOW,parent.left+px(80),parent.top+px(70),px(620),px(440),window,nullptr,GetModuleHandleW(nullptr),nullptr);
    if (h) {ShowWindow(h,SW_SHOWNORMAL);UpdateWindow(h);}
}
LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM w,LPARAM l) {
    try {
        switch (msg) {
        case WM_CREATE: {
            window=h;dpi=window_dpi(h);background_brush=CreateSolidBrush(background);white_brush=CreateSolidBrush(RGB(255,255,255));fonts();round_window(h);
            control(L"STATIC",app_name(),SS_LEFT,200);
            process_box=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,ID_PROCESS);
            control(L"BUTTON",tr(L"刷新",L"Refresh"),BS_OWNERDRAW|WS_TABSTOP,ID_SCAN);
            attach_button=control(L"BUTTON",tr(L"连接",L"Connect"),BS_OWNERDRAW|WS_TABSTOP,ID_ATTACH);
            enable_box=control(L"BUTTON",tr(L"启用拦截",L"Enable filtering"),BS_AUTOCHECKBOX|WS_TABSTOP,ID_ENABLE);SendMessageW(enable_box,BM_SETCHECK,BST_CHECKED,0);
            wifi_box=control(L"BUTTON",tr(L"排除 Wi-Fi",L"Exclude Wi-Fi"),BS_AUTOCHECKBOX|WS_TABSTOP,ID_WIFI);
            language_box=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,ID_LANGUAGE);
            for (auto label:{L"中文",L"English"}) SendMessageW(language_box,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
            SendMessageW(language_box,CB_SETCURSEL,language==UbLanguage::Chinese?0:1,0);
            status_label=control(L"STATIC",L"",SS_RIGHT,204);summary_label=control(L"STATIC",L"",SS_LEFT,205);blacklist_heading=control(L"STATIC",L"",SS_LEFT,202);
            candidates=control(WC_LISTVIEWW,L"",LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|WS_TABSTOP,ID_CANDIDATES);
            for (int i=0;i<5;i++) column(candidates,i,L"",px(100));
            blacklist=control(WC_LISTVIEWW,L"",LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|WS_TABSTOP,ID_BLACKLIST);column(blacklist,0,L"",px(200));
            control(L"BUTTON",tr(L"拉黑玩家",L"Block player"),BS_OWNERDRAW|WS_TABSTOP,ID_BLOCK);
            control(L"BUTTON",tr(L"解除拉黑",L"Unblock"),BS_OWNERDRAW|WS_TABSTOP,ID_REMOVE);
            control(L"BUTTON",tr(L"诊断",L"Diagnostics"),BS_OWNERDRAW|WS_TABSTOP,ID_REPORT);
            control(L"BUTTON",tr(L"请求记录",L"Request activity"),BS_OWNERDRAW|WS_TABSTOP,ID_ACTIVITY);
            try {entries=UbLoadSettings();wifi_option=UbLoadWifiOption();SendMessageW(wifi_box,BM_SETCHECK,wifi_option?BST_CHECKED:BST_UNCHECKED,0);}
            catch(const std::exception& e) {settings_failed=true;SendMessageW(enable_box,BM_SETCHECK,BST_UNCHECKED,0);EnableWindow(wifi_box,FALSE);error(UbWide(e.what()));}
            apply_language();scan();layout();SetTimer(h,1,500,nullptr);return 0;
        }
        case WM_SIZE:layout();return 0;
        case WM_GETMINMAXINFO:reinterpret_cast<MINMAXINFO*>(l)->ptMinTrackSize={px(760),px(520)};return 0;
        case WM_DPICHANGED: {
            dpi=HIWORD(w);auto r=reinterpret_cast<RECT*>(l);fonts();
            SetWindowPos(h,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);layout();return 0;
        }
        case WM_ATTACHED:attached(reinterpret_cast<Reply*>(l));return 0;
        case WM_TIMER:tick();return 0;
        case WM_PAINT: {PAINTSTRUCT paint{};HDC dc=BeginPaint(h,&paint);draw_main(h,dc);EndPaint(h,&paint);return 0;}
        case WM_PRINTCLIENT:draw_main(h,reinterpret_cast<HDC>(w));return 0;
        case WM_DRAWITEM:draw_button(*reinterpret_cast<DRAWITEMSTRUCT*>(l));return TRUE;
        case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:return colors(reinterpret_cast<HWND>(l),reinterpret_cast<HDC>(w));
        case WM_NOTIFY:if (reinterpret_cast<NMHDR*>(l)->code==LVN_ITEMCHANGED) selection_buttons();break;
        case WM_COMMAND:
            switch (LOWORD(w)) {
            case ID_SCAN:scan();break;
            case ID_ATTACH:connect();break;
            case ID_ENABLE:policy();break;
            case ID_LANGUAGE:if (HIWORD(w)==CBN_SELCHANGE) {
                LRESULT index=SendMessageW(language_box,CB_GETCURSEL,0,0);if (index!=0 && index!=1) break;
                UbLanguage requested=index==0?UbLanguage::Chinese:UbLanguage::English;
                try {UbSaveLanguage(requested);} catch(...) {SendMessageW(language_box,CB_SETCURSEL,language==UbLanguage::Chinese?0:1,0);throw;}
                language=requested;apply_language();
            }break;
            case ID_ACTIVITY:open_activity();break;
            case ID_WIFI: {
                bool requested=SendMessageW(wifi_box,BM_GETCHECK,0,0)==BST_CHECKED;
                try {UbSaveWifiOption(requested);wifi_option=requested;}
                catch(...) {SendMessageW(wifi_box,BM_SETCHECK,wifi_option?BST_CHECKED:BST_UNCHECKED,0);throw;}
                policy();rows_dirty=true;refresh_candidates(GetTickCount());break;
            }
            case ID_BLOCK: {
                int n=ListView_GetNextItem(candidates,-1,LVNI_SELECTED);
                if (n>=0 && size_t(n)<rows.size()) {
                    const auto& row=rows[size_t(n)];auto name=memchr(row.name_utf8,0,sizeof(row.name_utf8))?UbWide(row.name_utf8):L"";
                    add(row.steam_id,name);refresh_candidates(GetTickCount());
                } else error(L"请在搜索结果中选择一位玩家。");break;
            }
            case ID_REMOVE: {
                int n=ListView_GetNextItem(blacklist,-1,LVNI_SELECTED);
                if (n>=0 && size_t(n)<entries.size() && !settings_failed) {
                    auto changed=entries;changed.erase(changed.begin()+n);UbSaveSettings(changed);entries=std::move(changed);rebuild_blacklist();
                    policy();rows_dirty=true;refresh_candidates(GetTickCount());
                }break;
            }
            case ID_REPORT:report();break;
            }return 0;
        case WM_CLOSE:
            if (connecting) {set(status_label,tr(L"请等待连接结束",L"Wait for connection"));return 0;}
            SendMessageW(enable_box,BM_SETCHECK,BST_UNCHECKED,0);policy();disconnect();
            if (activity_window) DestroyWindow(activity_window);
            DestroyWindow(h);return 0;
        case WM_DESTROY:
            KillTimer(h,1);for (auto value:{font,title_font,bold_font,count_font}) if (value) DeleteObject(value);
            if (list_spacing) ImageList_Destroy(list_spacing);
            DeleteObject(background_brush);DeleteObject(white_brush);PostQuitMessage(0);return 0;
        }
    } catch(const std::exception& e) {error(UbWide(e.what()));}
    catch(...) {error(L"操作失败，原有配置保持不变。");}
    return DefWindowProcW(h,msg,w,l);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show) {
    using DpiFn=BOOL(WINAPI*)(HANDLE);auto dpi_fn=api<DpiFn>(GetModuleHandleW(L"user32.dll"),"SetProcessDpiAwarenessContext");
    if (!dpi_fn || !dpi_fn(reinterpret_cast<HANDLE>(-4))) SetProcessDPIAware();
    try {language=UbLoadLanguage();} catch(const std::exception& e) {error(UbWide(e.what()));}
    HANDLE singleton=CreateMutexW(nullptr,TRUE,L"Local\\UNI2Blacklist-GUI-v1");
    if (!singleton || GetLastError()==ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr,tr(L"黑名单窗口已运行。",L"UNI2 Blacklist is already running."),app_name(),MB_ICONINFORMATION);
        if(singleton)CloseHandle(singleton);
        return 1;
    }
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&common);
    WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.hInstance=instance;cls.lpszClassName=L"UNI2BlacklistWindow";
    cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1));
    cls.hbrBackground=nullptr;RegisterClassW(&cls);
    cls.lpfnWndProc=activity_proc;cls.lpszClassName=L"UNI2BlacklistActivityWindow";RegisterClassW(&cls);
    HDC dc=GetDC(nullptr);dpi=UINT(GetDeviceCaps(dc,LOGPIXELSX));ReleaseDC(nullptr,dc);if (!dpi) dpi=96;
    window=CreateWindowExW(WS_EX_CONTROLPARENT,L"UNI2BlacklistWindow",app_name(),WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
        CW_USEDEFAULT,CW_USEDEFAULT,px(880),px(620),nullptr,nullptr,instance,nullptr);
    if (!window) {CloseHandle(singleton);return 2;}
    // WM_CREATE now knows this window's monitor DPI; size the outer window
    // using it rather than assuming the desktop DC reported that same DPI.
    SetWindowPos(window,nullptr,0,0,px(880),px(620),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    ShowWindow(window,show);UpdateWindow(window);
    MSG msg{};while (GetMessageW(&msg,nullptr,0,0)>0) {
        if (!(activity_window && IsDialogMessageW(activity_window,&msg)) && !IsDialogMessageW(window,&msg)) {TranslateMessage(&msg);DispatchMessageW(&msg);}
    }
    CloseHandle(singleton);return int(msg.wParam);
}
