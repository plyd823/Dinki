#include "dinki_watch.h"
#include "dinki_cmd.h"

#include <chrono>
#include <exception>
#include <algorithm>
#include <memory>
#include <mutex>
#include <utility>

namespace Dinki {
namespace {

    struct Watch {
        std::string id, label, group;
        uint32_t color = 0xFFFFFF;
        int setting = -1;
        WatchSampler sampler;

        // how long the sampler has taken (guarded by State::mu)
        double costTotalMs = 0.0;
        double costMaxMs = 0.0;
        unsigned costSamples = 0;
    };

    struct State {
        std::mutex mu;
        std::vector<std::shared_ptr<Watch>> watches;
        std::vector<WatchLine> snapshot;
    };

    State& S() {
        static State s;
        return s;
    }

} // namespace

int RegisterWatch(const char* id, const char* label, const char* group, uint32_t groupColorRgb,
    bool defaultOn, WatchSampler sampler)
{
    // The checkbox is an ordinary setting. Registered first so a repeat registration finds it.
    const std::string settingId = std::string("watch.") + id;
    const int setting = RegisterBoolSetting(settingId.c_str(), label, "Watches", defaultOn);

    State& s = S();
    std::lock_guard<std::mutex> lock(s.mu);
    for (size_t i = 0; i < s.watches.size(); ++i)
        if (s.watches[i]->id == id)
            return static_cast<int>(i);

    auto w = std::make_shared<Watch>();
    w->id = id;
    w->label = label;
    w->group = group;
    w->color = groupColorRgb;
    w->setting = setting;
    w->sampler = std::move(sampler);
    s.watches.push_back(std::move(w));
    return static_cast<int>(s.watches.size() - 1);
}

void SampleWatches(void* ctx) {
    State& s = S();

    // Take pointers to the watches, then run the samplers without holding the lock:
    // a sampler reads game memory and must not be able to block the renderer.
    std::vector<std::shared_ptr<Watch>> watches;
    {
        std::lock_guard<std::mutex> lock(s.mu);
        watches = s.watches;
    }

    std::vector<WatchLine> lines;
    std::vector<std::pair<std::shared_ptr<Watch>, double>> timings;
    for (const auto& w : watches) {
        if (!GetSettingBool(w->setting) || !w->sampler)
            continue;   // switched off: don't even read it

        std::string text;
        WatchStatus status;
        const auto started = std::chrono::steady_clock::now();
        try {
            status = w->sampler(ctx, text);
        }
        catch (const std::exception&) {
            status = WatchStatus::Failed;
        }
        timings.emplace_back(w, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());

        if (status == WatchStatus::Hidden)
            continue;
        if (status == WatchStatus::Failed)
            text = "?";

        WatchLine line;
        line.group = w->group;
        line.groupColorRgb = w->color;
        line.label = w->label;
        line.text = std::move(text);
        lines.push_back(std::move(line));
    }

    std::lock_guard<std::mutex> lock(s.mu);
    for (const auto& t : timings) {
        t.first->costTotalMs += t.second;
        t.first->costMaxMs = std::max(t.first->costMaxMs, t.second);
        ++t.first->costSamples;
    }
    s.snapshot.swap(lines);
}

std::vector<WatchCost> TakeWatchCosts() {
    State& s = S();
    std::vector<WatchCost> costs;
    std::lock_guard<std::mutex> lock(s.mu);
    for (const auto& w : s.watches) {
        if (w->costSamples == 0)
            continue;
        WatchCost cost;
        cost.id = w->id;
        cost.samples = w->costSamples;
        cost.avgMs = w->costTotalMs / w->costSamples;
        cost.maxMs = w->costMaxMs;
        costs.push_back(cost);
        w->costTotalMs = 0.0;
        w->costMaxMs = 0.0;
        w->costSamples = 0;
    }
    return costs;
}

std::vector<WatchLine> GetWatchSnapshot() {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mu);
    return s.snapshot;
}

void WatchTestReset() {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mu);
    s.watches.clear();
    s.snapshot.clear();
}

} // namespace Dinki
