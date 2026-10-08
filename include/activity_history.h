#pragma once
#include <array>
#include <vector>
#include "protocol.h"

constexpr size_t UB_MAX_ACTIVITY_ROWS=200;
enum class UbActivityKind : uint8_t { Skipped, Rejected, Intercepted, Dropped };
struct UbActivityRow {
    UbActivityKind kind{};
    uint32_t count=0;
    SYSTEMTIME observed_at{};
};

// GUI-only observations of the native cumulative counters. Call observe from
// the 500 ms GUI poll: each nonzero delta is an aggregate for that interval,
// not a peer-level event or its exact network timestamp. Opening or switching
// processes establishes a baseline without manufacturing historical events.
class UbActivityHistory {
    std::vector<UbActivityRow> rows_;
    std::array<uint32_t,4> totals_{};
    DWORD pid_=0;
    bool active_=false;
    static std::array<uint32_t,4> counts(const UbShared& shared) {
        return {shared.candidate_skips,shared.request_rejects,
                shared.send_rejects,shared.receive_drops};
    }
public:
    const std::vector<UbActivityRow>& rows() const {return rows_;}
    const std::array<uint32_t,4>& totals() const {return totals_;}
    void begin(const UbShared& shared) {
        rows_.clear();totals_=counts(shared);pid_=shared.pid;active_=true;
    }
    void observe(const UbShared& shared,const SYSTEMTIME& observed_at) {
        if (!active_ || pid_!=shared.pid) {begin(shared);return;}
        const auto next=counts(shared);
        for (size_t i=0;i<next.size();++i) {
            // Unsigned subtraction also preserves a native DWORD wrap.
            const uint32_t delta=next[i]-totals_[i];
            if (delta) rows_.push_back({static_cast<UbActivityKind>(i),delta,observed_at});
        }
        totals_=next;
        if (rows_.size()>UB_MAX_ACTIVITY_ROWS)
            rows_.erase(rows_.begin(),rows_.begin()+(rows_.size()-UB_MAX_ACTIVITY_ROWS));
    }
    // Clearing the window keeps the native cumulative totals and baseline.
    void clear_view() {rows_.clear();}
    void clear_context() {rows_.clear();totals_={};pid_=0;active_=false;}
};
