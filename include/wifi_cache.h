#pragma once
#include <algorithm>
#include "protocol.h"

constexpr uint32_t UB_WIFI_RETENTION_MS=120000;
constexpr uint32_t UB_WIFI_CACHE_LIMIT=4096;
struct UbWifiPeer {uint64_t id;DWORD tick;uint8_t type;};
// In-process observations only. Access is serialized by the runtime policy lock.
class UbWifiCache {
    UbWifiPeer peers_[UB_WIFI_CACHE_LIMIT]{};
    uint32_t count_=0;
public:
    void observe(uint64_t id,uint8_t type,DWORD now) {
        if (!UbPlayerId(id)) return;
        auto end=peers_+count_;
        auto found=std::lower_bound(peers_,end,id,[](const UbWifiPeer& p,uint64_t key){return p.id<key;});
        if (found!=end && found->id==id) {
            if (type==UB_CONNECTION_WIFI) *found={id,now,type};
            else {std::move(found+1,end,found);--count_;}
            return;
        }
        if (type!=UB_CONNECTION_WIFI) return;
        if (count_==UB_WIFI_CACHE_LIMIT) {
            // Evict the oldest observation; never retain an account indefinitely.
            auto old=std::max_element(peers_,end,[now](const UbWifiPeer& a,const UbWifiPeer& b){return DWORD(now-a.tick)<DWORD(now-b.tick);});
            std::move(old+1,end,old);--count_;
            found=std::lower_bound(peers_,peers_+count_,id,[](const UbWifiPeer& p,uint64_t key){return p.id<key;});
        }
        std::move_backward(found,peers_+count_,peers_+count_+1);
        *found={id,now,type};++count_;
    }
    bool contains(uint64_t id,DWORD now) const {
        auto end=peers_+count_;
        auto found=std::lower_bound(peers_,end,id,[](const UbWifiPeer& p,uint64_t key){return p.id<key;});
        return found!=end && found->id==id && DWORD(now-found->tick)<UB_WIFI_RETENTION_MS;
    }
    uint32_t expire(DWORD now) {
        auto end=std::remove_if(peers_,peers_+count_,[now](const UbWifiPeer& p){return DWORD(now-p.tick)>=UB_WIFI_RETENTION_MS;});
        count_=static_cast<uint32_t>(end-peers_);return count_;
    }
};
