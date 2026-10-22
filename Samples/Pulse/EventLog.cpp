#include "Pulse.h"

#include <format>

namespace pulse {

namespace {

constexpr const char* kLevelName[3]  = {"INFO", "WARN", "CRIT"};
constexpr const char* kLevelClass[3] = {"lvl info", "lvl warn", "lvl crit"};

// Newest first: logical row 0 is the last event.
const LogEvent* At(const Feed& f, int row)
{
    const int n = static_cast<int>(f.events.size());
    return row >= 0 && row < n ? &f.events[static_cast<std::size_t>(n - 1 - row)] : nullptr;
}

} // namespace

Elem EventLog(FeedPtr f)
{
    return Column({.layout = {.width = Expand, .height = Expand}, .className = "log"},
        Row({.className = "log-head"},
            Text("Time", {.className = "lcol ltime"}),
            Text("Level", {.className = "lcol llvl"}),
            Text("Source", {.className = "lcol lsrc"}),
            Text("Message", {.layout = {.width = Expand}, .className = "lcol"})),
        VirtualList({
            .count      = f->eventCount,
            .itemExtent = 26.f,
            .overscan   = 4,
            // Rows are index-keyed and read their event reactively: an append re-labels the materialized rows
            // instead of rebuilding them (a keyed rebuild costs ~40 ms per append in Debug, see report).
            .row        = [f](int i) {
                ProfScope prof(Prof::LogRows);
                const Signal<std::uint64_t> version = f->logVersion;
                const auto event = [f, i, version] { version(); return At(*f, i); };
                const auto field = [event](auto pick) {
                    return Str([event, pick] {
                        const LogEvent* e = event();
                        return e ? std::string(pick(*e)) : std::string{};
                    });
                };
                // The head row alternates flash-a/flash-b so its keyframe replays on every new event.
                const auto rowClass = [f, i, event] {
                    const LogEvent* e = event();
                    if (!e)
                        return std::string("log-row");
                    std::string cls = i == 0                     ? (e->seq % 2 ? "log-row flash-a" : "log-row flash-b")
                                    : f->simTime - e->time < 2.0 ? "log-row recent"
                                                                 : "log-row";
                    return e->level == 2 ? cls + " critical" : cls;
                };
                return Row({.className = rowClass},
                    Text(field([](const LogEvent& e) { return ClockString(e.time); }), {.className = "ltime"}),
                    Row({.className = "llvl"},
                        Box({.className = [event] { const LogEvent* e = event(); return std::string(kLevelClass[e ? e->level : 0]); }},
                            Text(field([](const LogEvent& e) { return kLevelName[e.level]; }), {.className = "lvl-text"}))),
                    Text(field([](const LogEvent& e) { return e.source; }), {.className = "lsrc"}),
                    Text(field([](const LogEvent& e) { return std::format("#{}  {}", e.seq, e.text); }),
                         {.className = "lmsg"}));
            },
            .layout    = {.width = Expand, .height = Expand},
            .className = "vlist",
        }));
}

} // namespace pulse
