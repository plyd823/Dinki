#pragma once

// Dinki watches: live values shown on the overlay. Each watch has an on/off checkbox in the
// Dinki menu (saved in dinki.ini) and a sampler function that reads its value.
//
// Threading
//   * SampleWatches() runs on the GAME thread, where it is safe to read game objects. It builds a
//     snapshot of text lines.
//   * GetWatchSnapshot() can be called from any thread (the overlay uses the render thread) and
//     only ever copies finished text, so the renderer never touches game memory.
//
// Adding a watch:
//
//   static const int kMyWatch = Dinki::RegisterWatch("my.thing", "My thing", "MY GROUP", 0x00FF00, true,
//       [](void* ctx, std::string& text) {
//           float v = 0;
//           if (!ReadSomething(ctx, v)) return Dinki::WatchStatus::Failed;   // shows "?"
//           text = "Value: " + std::to_string(v);
//           return Dinki::WatchStatus::Ok;
//       });

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Dinki {

    enum class WatchStatus {
        Ok,       // `text` holds the value to show
        Hidden,   // not applicable right now (e.g. a vehicle watch while on foot): show nothing
        Failed,   // couldn't read it: shown as "?"
    };

    // `ctx` is whatever was passed to SampleWatches(); `text` may contain newlines.
    using WatchSampler = std::function<WatchStatus(void* ctx, std::string& text)>;

    // Registers a watch and returns its handle. Safe to call from static initializers.
    //   id            stable name; its on/off switch is saved as "watch.<id>" in the ini
    //   label         checkbox text in the menu
    //   group         heading it is listed under on the overlay (consecutive watches sharing one share a heading)
    //   groupColorRgb heading color as 0xRRGGBB
    //   defaultOn     whether it is shown until the user turns it off
    // Registering an id that already exists returns the existing handle.
    int RegisterWatch(const char* id, const char* label, const char* group, uint32_t groupColorRgb,
        bool defaultOn, WatchSampler sampler);

    // Game thread: runs every enabled watch's sampler and publishes the result.
    void SampleWatches(void* ctx);

    struct WatchLine {
        std::string group;
        uint32_t groupColorRgb = 0xFFFFFF;
        std::string label;
        std::string text;
    };

    // Any thread: the lines from the most recent SampleWatches(), in registration order.
    std::vector<WatchLine> GetWatchSnapshot();

    // How long each watch's sampler took, since the last call (the counters then reset). Watches that weren't
    // sampled in that time (switched off) are left out.
    struct WatchCost {
        std::string id;
        double avgMs = 0.0;
        double maxMs = 0.0;
        unsigned samples = 0;
    };
    std::vector<WatchCost> TakeWatchCosts();

    // Forget every watch and the snapshot. For unit tests.
    void WatchTestReset();

} // namespace Dinki
