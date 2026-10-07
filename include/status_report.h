#pragma once
#include <algorithm>
#include <cstring>
#include <string>
#include "protocol.h"

inline std::string UbStatusReport(const UbShared& snapshot,DWORD pid,const std::string& attachment_error,DWORD now) {
    std::string s="UNI2Blacklist "+std::string(UB_VERSION)+"\r\nEXE SHA256: "+std::string(UB_GAME_SHA)+
        "\r\nSteam DLL SHA256: "+UB_STEAM_SHA+"\r\n";
    auto field=[&](const char* k,uint32_t v){s+=std::string(k)+": "+std::to_string(v)+"\r\n";};
    field("pid",pid);field("state",snapshot.state);field("status",snapshot.status);field("hooks_ready",snapshot.network_hooks_ready);
    field("requested_enabled",snapshot.enable);
    field("native_filter_active",snapshot.filter_active && DWORD(now-snapshot.heartbeat)<=3000 && snapshot.policy_ack==snapshot.policy_revision);
    field("effective_enabled",UbEffective(snapshot,now));
    field("heartbeat_age_ms",DWORD(now-snapshot.heartbeat));
    field("policy_revision",snapshot.policy_revision);field("policy_ack",snapshot.policy_ack);
    field("scene",snapshot.host_scene);field("battle_suspended",snapshot.battle_suspended);field("blacklist_count",snapshot.blocked_count);
    field("search_count",snapshot.search_count);field("candidate_count",snapshot.candidate_count);field("omitted",snapshot.omitted_count);
    field("candidate_skips",snapshot.candidate_skips);field("request_rejects",snapshot.request_rejects);field("send_rejects",snapshot.send_rejects);
    field("receive_drops",snapshot.receive_drops);field("metadata_errors",snapshot.metadata_errors);
    const char* groups[]={"native","legacy","messages002"};
    auto hex=[](uint32_t v) {char buf[16]{};wsprintfA(buf,"0x%08lx",static_cast<unsigned long>(v));return std::string(buf);};
    const char* mh_names[]={"MH_OK","MH_ERROR_ALREADY_INITIALIZED","MH_ERROR_NOT_INITIALIZED","MH_ERROR_ALREADY_CREATED",
        "MH_ERROR_NOT_CREATED","MH_ERROR_ENABLED","MH_ERROR_DISABLED","MH_ERROR_NOT_EXECUTABLE","MH_ERROR_UNSUPPORTED_FUNCTION",
        "MH_ERROR_MEMORY_ALLOC","MH_ERROR_MEMORY_PROTECT","MH_ERROR_MODULE_NOT_FOUND","MH_ERROR_FUNCTION_NOT_FOUND"};
    for (unsigned i=0;i<3;i++) {
        const auto& d=snapshot.hook_diagnostics[i];std::string p=std::string(groups[i])+".";
        auto item=[&](const char* key,const std::string& value){s+=p+key+": "+value+"\r\n";};
        auto number=[&](const char* key,uint32_t value){item(key,std::to_string(value));};
        auto address=[&](const char* key,uint32_t value){item(key,hex(value));};
        item("method",d.method==UB_HOOK_MINHOOK?"minhook":d.method==UB_HOOK_VTABLE?"vtable_slot":"not_attempted");
        item("stage",UbStageName(d.stage));number("index",d.index);number("slot",d.slot);
        item("minhook_status",std::to_string(d.minhook_status));
        item("minhook_status_name",d.minhook_status>=0 && d.minhook_status<13?mh_names[d.minhook_status]:"MH_UNKNOWN");
        number("win32_error",d.win32_error);number("cleanup_error",d.cleanup_error);number("restore_error",d.restore_error);
        address("interface",d.interface_address);address("vtable",d.vtable_address);address("slot_address",d.slot_address);
        address("target",d.target_address);address("observed",d.observed_address);
        address("memory_state",d.memory_state);address("memory_type",d.memory_type);address("memory_protect",d.memory_protect);
        address("slot_memory_state",d.slot_memory_state);address("slot_memory_type",d.slot_memory_type);address("slot_memory_protect",d.slot_memory_protect);
        item("module",std::string(d.module_utf8,strnlen(d.module_utf8,sizeof(d.module_utf8))));address("module_rva",d.module_rva);
        std::string bytes;constexpr char digits[]="0123456789abcdef";
        for (unsigned j=0;j<std::min<uint32_t>(d.code_bytes,sizeof(d.code_prefix));j++) {
            if (j) bytes+=' ';
            bytes+=digits[d.code_prefix[j]>>4];bytes+=digits[d.code_prefix[j]&15];
        }
        item("target_prefix",bytes);
        for (unsigned j=0;j<5;j++) item(("target_"+std::to_string(j)).c_str(),hex(d.targets[j]));
    }
    s+="message: "+std::string(snapshot.message_utf8,strnlen(snapshot.message_utf8,256))+"\r\nattach_error: "+attachment_error+"\r\n";
    return s;
}
