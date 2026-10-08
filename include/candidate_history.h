#pragma once
#include <algorithm>
#include <cstring>
#include <vector>
#include "protocol.h"

constexpr DWORD UB_CANDIDATE_RETENTION_MS=120000;
constexpr size_t UB_MAX_RECENT_CANDIDATES=2048;
struct UbRecentCandidate {
    UbCandidate candidate{};
    DWORD last_seen=0;
    bool in_latest_result=false;
};

// GUI history only: these rows never replace the game's eligible search list.
// Use producer observation times, not redraw times, so UI refreshes cannot
// extend an absent player's lifetime. DWORD subtraction handles clock wrap.
class UbCandidateHistory {
    std::vector<UbRecentCandidate> rows_;
    uint32_t omitted_=0;
public:
    const std::vector<UbRecentCandidate>& rows() const {return rows_;}
    uint32_t omitted() const {return omitted_;}
    void clear() {rows_.clear();omitted_=0;}
    bool expire(DWORD now) {
        size_t before=rows_.size();
        rows_.erase(std::remove_if(rows_.begin(),rows_.end(),[&](const UbRecentCandidate& row) {
            return DWORD(now-row.last_seen)>=UB_CANDIDATE_RETENTION_MS;
        }),rows_.end());
        return before!=rows_.size();
    }
    void observe(const UbCandidate* candidates,size_t count,DWORD now) {
        expire(now);
        for (auto& row:rows_) row.in_latest_result=false;
        for (size_t i=0;i<std::min<size_t>(count,UB_MAX_CANDIDATES);i++) {
            const auto& candidate=candidates[i];
            if (!UbPlayerId(candidate.steam_id) ||
                DWORD(now-candidate.observed_tick)>=UB_CANDIDATE_RETENTION_MS) continue;
            auto found=std::find_if(rows_.begin(),rows_.end(),[&](const UbRecentCandidate& row) {
                return row.candidate.steam_id==candidate.steam_id;
            });
            if (found==rows_.end()) {
                // Keep already displayed players for their full two minutes.
                // If the bound is reached, omit new rows rather than evict them.
                if (rows_.size()>=UB_MAX_RECENT_CANDIDATES) {++omitted_;continue;}
                rows_.push_back({candidate,candidate.observed_tick,true});
            } else {
                found->in_latest_result=true;
                if (DWORD(now-candidate.observed_tick)>DWORD(now-found->last_seen)) continue;
                UbCandidate next=candidate;
                // A temporary Steam persona cache miss needn't erase a known name.
                if (!(next.flags&UB_NAME_KNOWN) && (found->candidate.flags&UB_NAME_KNOWN)) {
                    memcpy(next.name_utf8,found->candidate.name_utf8,sizeof(next.name_utf8));
                    next.flags|=UB_NAME_KNOWN;
                }
                found->candidate=next;found->last_seen=candidate.observed_tick;
            }
        }
    }
};
