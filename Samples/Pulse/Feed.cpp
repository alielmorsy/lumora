#include "Pulse.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <format>
#include <numeric>

namespace pulse {

namespace {

constexpr double kSampleHz = 30.0; // data rate at 1x
constexpr double kCommitS  = 0.5;  // how often headline numbers settle

const std::chrono::system_clock::time_point kStart = std::chrono::system_clock::now();

const char* const kSources[] = {"api-gw", "auth", "billing", "ingest", "search", "cache", "worker-3", "edge-eu"};
const char* const kInfo[]    = {"request served from cache", "session refreshed", "batch committed",
                                "shard rebalanced", "invoice settled", "index segment merged",
                                "health probe ok", "autoscaler steady"};
const char* const kWarn[]    = {"p95 latency above budget", "retry storm detected", "queue depth rising",
                                "slow consumer lag"};

} // namespace

const Palette& Colors(bool dark)
{
    static const Palette kDark{
        .bg = 0x0b0e14_rgb, .surface = 0x121722_rgb, .grid = 0x222a38_rgb, .text = 0xe7ebf3_rgb,
        .muted = 0x7d879a_rgb, .track = 0x1c2330_rgb,
        .accents = {0x5b8cff_rgb, 0x2ed3a5_rgb, 0xff7a59_rgb, 0xb57bff_rgb, 0xffc145_rgb, 0x38bdf8_rgb}};
    static const Palette kLight{
        .bg = 0xf2f4f8_rgb, .surface = 0xffffff_rgb, .grid = 0xe3e7ef_rgb, .text = 0x172033_rgb,
        .muted = 0x6b7487_rgb, .track = 0xe9edf4_rgb,
        .accents = {0x2f6bff_rgb, 0x0ea37a_rgb, 0xf05a33_rgb, 0x8b4dff_rgb, 0xe0a100_rgb, 0x0891d1_rgb}};
    return dark ? kDark : kLight;
}

std::string Thousands(double v)
{
    std::string digits = std::to_string(static_cast<long long>(std::llround(v)));
    std::string out;
    const int n = static_cast<int>(digits.size());
    for (int i = 0; i < n; ++i) {
        out += digits[static_cast<std::size_t>(i)];
        if ((n - 1 - i) % 3 == 0 && i != n - 1)
            out += ',';
    }
    return out;
}

std::string ClockString(double offsetSeconds)
{
    const auto t = kStart + std::chrono::milliseconds(static_cast<long long>(offsetSeconds * 1000.0));
    const std::time_t secs = std::chrono::system_clock::to_time_t(t);
    std::tm tm{};
    localtime_s(&tm, &secs);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t.time_since_epoch()).count() % 1000;
    return std::format("{:02}:{:02}:{:02}.{:03}", tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
}

float Feed::Rand(float lo, float hi)
{
    return std::uniform_real_distribution<float>(lo, hi)(rng);
}

Feed::Feed()
{
    struct Spec { const char* name; const char* unit; int dec; double base, vol, alarm; };
    const Spec specs[kMetricCount] = {
        {"Requests", "rps", 0, 4200, 90, 4650},   {"Latency p95", "ms", 1, 84, 3.2, 96},
        {"Error rate", "%", 2, 0.42, 0.04, 0.6},  {"Active users", "", 0, 18250, 260, 19100},
        {"Revenue / min", "$", 0, 1260, 40, 1380}, {"Heart rate", "bpm", 0, 72, 2.4, 84},
        {"Steps", "/h", 0, 5400, 140, 5900},       {"BTC", "$", 0, 64200, 180, 64900},
        {"CPU", "%", 1, 46, 2.2, 66},              {"Memory", "GB", 2, 11.4, 0.18, 12.2},
        {"Disk IO", "MB/s", 0, 320, 18, 390},      {"Packets", "k/s", 1, 88, 3.4, 99},
    };
    metrics.resize(kMetricCount);
    for (int i = 0; i < kMetricCount; ++i) {
        Metric& m = metrics[static_cast<std::size_t>(i)];
        m.name = specs[i].name;
        m.unit = specs[i].unit;
        m.decimals = specs[i].dec;
        m.base = m.value = m.committed = specs[i].base;
        m.vol = specs[i].vol;
        m.alarm = specs[i].alarm;
        m.hist.reserve(kSparkLen + 1);
    }
    for (auto& s : series)
        s.reserve(kSeriesLen + 1);

    const char* teams[kRaceCount] = {"Aurora", "Basalt", "Cobalt", "Delta", "Ember", "Fjord", "Granite", "Helix"};
    for (int i = 0; i < kRaceCount; ++i) {
        raceNames[static_cast<std::size_t>(i)] = teams[i];
        raceRaw[static_cast<std::size_t>(i)] = 400.0 + i * 37.0;
    }
    // Weekday-heavy activity with a slow weekly swell.
    for (int c = 0; c < kHeatCols; ++c)
        for (int r = 0; r < kHeatRows; ++r)
            heat[static_cast<std::size_t>(c * kHeatRows + r)] =
                (r >= 5 ? 0.25f : 0.7f) * (0.6f + 0.4f * std::sin(c * 0.45f + r * 0.3f)) * Rand(0.55f, 1.f);

    // Pre-roll so charts and the log start full.
    for (int i = 0; i < kSeriesLen; ++i)
        Sample();
    while (events.size() < 5000)
        Log(0, kSources[events.size() % 8], kInfo[events.size() % 8]);
    for (auto& m : metrics)
        m.above = m.value > m.alarm;
    Commit();
    eventCount = static_cast<int>(events.size());
    logVersion = nextSeq;
}

void Feed::Log(int level, std::string source, std::string text)
{
    events.push_back({nextSeq++, simTime, level, std::move(source), std::move(text)});
    if (events.size() > kEventCap)
        events.erase(events.begin(), events.begin() + (events.size() - kEventCap)); // ponytail: O(n) trim, ring buffer if cap grows
}

void Feed::Burst(Vec2 at, Color color)
{
    particles.push_back({.pos = at, .color = color, .life = 0.f, .maxLife = 0.9f, .size = 6.f, .ring = true});
    particles.push_back({.pos = at, .color = color, .life = -0.15f, .maxLife = 0.9f, .size = 6.f, .ring = true});
    for (int i = 0; i < 48; ++i) {
        const float a = Rand(0.f, 6.2831853f);
        const float v = Rand(60.f, 260.f);
        particles.push_back({.pos = at, .vel = {std::cos(a) * v, std::sin(a) * v - 80.f},
                             .color = Colors(true).accents[static_cast<std::size_t>(i % 6)],
                             .life = 0.f, .maxLife = Rand(0.7f, 1.4f), .size = Rand(1.5f, 3.5f)});
    }
}

void Feed::Sample()
{
    simTime += 1.0 / kSampleHz;
    ++samples;
    std::normal_distribution<double> n(0.0, 1.0);

    for (int i = 0; i < kMetricCount; ++i) {
        Metric& m = metrics[static_cast<std::size_t>(i)];
        const double wave = std::sin(simTime * (0.21 + i * 0.037) + i) * m.vol * 1.6;
        m.value += (m.base + wave - m.value) * 0.04 + n(rng) * m.vol * 0.35;
        if (Rand(0.f, 1.f) < 0.0009f)
            m.value += m.vol * Rand(4.f, 9.f); // spike
        m.value = std::max(m.value, 0.0);
        m.hist.push_back(static_cast<float>(m.value));
        if (m.hist.size() > kSparkLen)
            m.hist.erase(m.hist.begin());

        const bool above = m.value > m.alarm;
        if (above && !m.above) {
            const Rect r = cardRects[static_cast<std::size_t>(i)];
            if (r.width > 0.f)
                Burst({r.x + r.width * 0.5f, r.y + r.height * 0.5f}, Colors(true).accents[2]);
            Log(2, "alarm", std::format("{} crossed {:.{}f}{}", m.name, m.alarm, m.decimals, m.unit));
        }
        if (above)
            m.above = true;
        else if (m.value < m.alarm - m.vol * 3.0)
            m.above = false;
    }

    // Stream chart: requests, latency-ish, errors-ish
    const double t = simTime;
    const float s0 = static_cast<float>(62 + 22 * std::sin(t * 0.35) + 9 * std::sin(t * 1.7) + n(rng) * 3.5);
    const float s1 = static_cast<float>(40 + 14 * std::sin(t * 0.22 + 1.3) + 6 * std::sin(t * 2.3) + n(rng) * 2.5);
    const float s2 = static_cast<float>(16 + 7 * std::sin(t * 0.5 + 2.1) + std::abs(n(rng)) * 4.0);
    const float vals[kSeriesCount] = {s0, s1, s2};
    for (int s = 0; s < kSeriesCount; ++s) {
        auto& v = series[static_cast<std::size_t>(s)];
        v.push_back(std::max(vals[s], 0.f));
        if (v.size() > kSeriesLen)
            v.erase(v.begin());
    }

    // Gauges retarget every sample; springs smooth them.
    gauges[0] = static_cast<float>(metrics[8].value);
    gauges[1] = static_cast<float>(metrics[9].value / 16.0 * 100.0);
    gauges[2] = static_cast<float>(metrics[5].value);
    gauges[3] = static_cast<float>(metrics[11].value);

    heat[static_cast<std::size_t>(heatCol * kHeatRows + (samples % kHeatRows))] += Rand(0.f, (samples % kHeatRows) >= 5 ? 0.012f : 0.035f);
    if (++heatTick >= 90) {
        heatTick = 0;
        std::rotate(heat.begin(), heat.begin() + kHeatRows, heat.end());
        std::fill(heat.end() - kHeatRows, heat.end(), 0.f);
    }

    for (int i = 0; i < kRaceCount; ++i)
        raceRaw[static_cast<std::size_t>(i)] += std::max(0.0, n(rng) * 2.2 + 1.0 + std::sin(t * 0.13 + i) * 1.4);

    if (Rand(0.f, 1.f) < 0.12f) {
        const int roll = static_cast<int>(Rand(0.f, 100.f));
        const int lvl = roll < 82 ? 0 : 1;
        const auto src = kSources[static_cast<int>(Rand(0.f, 7.99f))];
        Log(lvl, src, lvl == 0 ? kInfo[static_cast<int>(Rand(0.f, 7.99f))] : kWarn[static_cast<int>(Rand(0.f, 3.99f))]);
    }
}

void Feed::Commit()
{
    for (auto& m : metrics) {
        const double prev = m.committed;
        m.committed = m.value;
        m.shown = static_cast<float>(m.value);
        m.delta = prev != 0.0 ? static_cast<float>((m.value - prev) / prev * 100.0) : 0.f;
    }

    std::array<int, kRaceCount> order{};
    std::iota(order.begin(), order.end(), 0);
    std::ranges::sort(order, [this](int a, int b) { return raceRaw[static_cast<std::size_t>(a)] > raceRaw[static_cast<std::size_t>(b)]; });
    const double lead = raceRaw[static_cast<std::size_t>(order[0])];
    for (int r = 0; r < kRaceCount; ++r)
        raceRank[static_cast<std::size_t>(order[static_cast<std::size_t>(r)])] = r;
    for (int i = 0; i < kRaceCount; ++i)
    {
        raceShare[static_cast<std::size_t>(i)] = static_cast<float>(raceRaw[static_cast<std::size_t>(i)] / lead * 100.0);
        raceValue[static_cast<std::size_t>(i)] = static_cast<float>(raceRaw[static_cast<std::size_t>(i)]);
    }

    const std::uint64_t total = nextSeq - 1;
    const float rate = static_cast<float>(static_cast<double>(total - eventsAtCommit) / kCommitS);
    throughput = eventsAtCommit == 0 ? 3.6f : throughput.Peek() * 0.8f + rate * 0.2f;
    eventsAtCommit = total;
    totalEvents = static_cast<float>(total);
    revenue = revenue.Peek() + static_cast<float>(metrics[4].value * kCommitS / 60.0 * speed.Peek());
    int warn = 0;
    for (auto it = events.rbegin(); it != events.rend() && it->time > simTime - 20.0; ++it)
        warn += it->level > 0;
    alerts = warn;
}

namespace {

using ProfClock = std::chrono::steady_clock;

struct ProfTable {
    bool enabled = std::getenv("PULSE_PROFILE") != nullptr;
    std::array<double, static_cast<int>(Prof::Count)> ms{};
    long long frames = 0;
    ProfClock::time_point last{};
    bool logChanged = false;
    // Gap between ticks, split by whether the previous tick appended to the log (VirtualList rows rebuilt).
    std::array<double, 2> gapMs{};
    std::array<long long, 2> gapN{};
    ~ProfTable()
    {
        if (!enabled || frames < 2)
            return;
        const char* names[] = {"feed step", "sparklines", "stream chart", "heatmap", "gauges", "particles", "log row build"};
        std::printf("pulse profile over %lld ticks (avg ms/tick)\n", frames);
        for (int i = 0; i < static_cast<int>(Prof::Count); ++i)
            std::printf("  %-13s %7.3f\n", names[i], ms[static_cast<std::size_t>(i)] / static_cast<double>(frames));
        for (std::size_t k = 0; k < 2; ++k)
            if (gapN[k] > 0)
                std::printf("  tick gap, %s: %.3f ms over %lld ticks\n", k ? "log appended" : "log idle",
                            gapMs[k] / static_cast<double>(gapN[k]), gapN[k]);
    }
};
ProfTable g_prof;

long long Now() { return ProfClock::now().time_since_epoch().count(); }

} // namespace

ProfScope::ProfScope(Prof p) : m_p(p), m_start(g_prof.enabled ? Now() : 0) {}

ProfScope::~ProfScope()
{
    if (g_prof.enabled)
        g_prof.ms[static_cast<std::size_t>(m_p)] +=
            std::chrono::duration<double, std::milli>(ProfClock::duration(Now() - m_start)).count();
}

void Feed::Step(float dt)
{
    if (g_prof.enabled) {
        const auto now = ProfClock::now();
        if (g_prof.frames++ > 0) {
            const auto k = static_cast<std::size_t>(g_prof.logChanged);
            g_prof.gapMs[k] += std::chrono::duration<double, std::milli>(now - g_prof.last).count();
            ++g_prof.gapN[k];
        }
        g_prof.last = now;
        g_prof.logChanged = false;
    }
    ProfScope prof(Prof::Step);
    wallTime += dt;
    if (!paused.Peek()) {
        sampleAcc += dt * kSampleHz * speed.Peek();
        int budget = 64; // never spiral after a stall
        while (sampleAcc >= 1.0 && budget-- > 0) {
            sampleAcc -= 1.0;
            Sample();
        }
        sampleAcc = std::min(sampleAcc, 1.0);
        commitAcc += dt;
        if (commitAcc >= kCommitS) {
            commitAcc = 0.0;
            Commit();
        }
        if (logVersion.Peek() != nextSeq) {
            g_prof.logChanged = true;
            eventCount = static_cast<int>(events.size());
            logVersion = nextSeq;
        }
    }

    beatPhase += dt * static_cast<float>(metrics[5].value / 60.0);
    for (auto& p : particles) {
        p.life += dt;
        if (!p.ring) {
            p.vel.y += 320.f * dt;
            p.vel.x *= 0.985f;
            p.pos.x += p.vel.x * dt;
            p.pos.y += p.vel.y * dt;
        }
    }
    std::erase_if(particles, [](const Particle& p) { return p.life >= p.maxLife; });

    float peak = 1.f;
    for (const auto& s : series)
        for (float v : s)
            peak = std::max(peak, v);
    chartMax += (peak * 1.15f - chartMax) * std::min(1.f, dt * 3.f);

    frame = frame.Peek() + 1;
}

} // namespace pulse
