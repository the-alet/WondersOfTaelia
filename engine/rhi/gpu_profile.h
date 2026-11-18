// Optional, fence-delayed GPU timestamp capture. Markers are inclusive (do not sum nested rows).
#pragma once
#include <array>
#include <fstream>
#include <iomanip>
#include <locale>
#include "rhi/rhi.h"

namespace taelia::rhi {
class GpuProfile {
public:
    static constexpr uint32_t kQueries = 256, kInvalidQuery = UINT32_MAX;
    bool open(const std::string& path) {
        if (path.empty()) return true;
        output_.open(path, std::ios::trunc);
        output_.imbue(std::locale::classic());
        output_ << "frame,pass,gpu_ms\n" << std::fixed << std::setprecision(6);
        return bool(output_);
    }
    bool enabled() const { return output_.is_open(); }
    void reset(uint32_t slot, uint64_t frame) {
        auto& s = slots_[slot];
        s.frame = frame;
        s.next = 2; // 0/1 enclose the whole GPU command list, excluding present and readback
        s.events.clear();
        s.stack.clear();
        s.pending = false;
    }
    uint32_t begin(uint32_t slot, const char* name) {
        auto& s = slots_[slot];
        if (s.next + 2 > kQueries) {
            s.stack.push_back(kInvalidQuery);
            return kInvalidQuery;
        }
        const uint32_t q = s.next;
        s.next += 2;
        s.stack.push_back(uint32_t(s.events.size()));
        s.events.push_back({name, q, q + 1});
        return q;
    }
    uint32_t end(uint32_t slot) {
        auto& s = slots_[slot];
        if (s.stack.empty()) return kInvalidQuery;
        const uint32_t event = s.stack.back();
        s.stack.pop_back();
        return event == kInvalidQuery ? kInvalidQuery : s.events[event].end;
    }
    uint32_t count(uint32_t slot) const { return slots_[slot].next; }
    bool pending(uint32_t slot) const { return slots_[slot].pending; }
    void submitted(uint32_t slot) { slots_[slot].pending = true; }
    void collect(uint32_t slot, const uint64_t* ticks, double msPerTick, uint64_t mask = UINT64_MAX) {
        auto& s = slots_[slot];
        if (!s.pending) return;
        auto row = [&](const char* name, uint32_t a, uint32_t b) {
            // The Vulkan queue may expose fewer than 64 valid timestamp bits.
            output_ << s.frame << ",\"";
            for (const char* c = name; *c; ++c) {
                if (*c == '"') output_ << '"';
                output_ << *c;
            }
            output_ << "\"," << double((ticks[b] - ticks[a]) & mask) * msPerTick << '\n';
        };
        row("gpu frame", 0, 1);
        for (const auto& event : s.events) row(event.name.c_str(), event.begin, event.end);
        s.pending = false;
        output_.flush();
    }
private:
    struct Event { std::string name; uint32_t begin, end; };
    struct Slot {
        uint64_t frame = 0;
        uint32_t next = 0;
        bool pending = false;
        std::vector<Event> events;
        std::vector<uint32_t> stack;
    };
    std::array<Slot, kFramesInFlight> slots_;
    std::ofstream output_;
};
} // namespace taelia::rhi
