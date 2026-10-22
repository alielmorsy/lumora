#pragma once

#include <lumora/Animation/Animation.h>
#include <lumora/Foundation/Color.h>
#include <lumora/Foundation/Rect.h>
#include <lumora/Foundation/Vec2.h>
#include <lumora/Reactive/Computed.h>
#include <lumora/Reactive/Format.h>
#include <lumora/Reactive/Signal.h>
#include <lumora/Widgets/Widgets.h>

#include <array>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace pulse {

using namespace lumora;

constexpr int kMetricCount = 12;
constexpr int kSparkLen    = 90;
constexpr int kSeriesCount = 3;
constexpr int kSeriesLen   = 600;
constexpr int kHeatCols    = 30;
constexpr int kHeatRows    = 7;
constexpr int kRaceCount   = 8;
constexpr int kGaugeCount  = 4;
constexpr int kEventCap    = 20000;

struct Metric {
    std::string name;
    std::string unit;
    int         decimals = 0;
    double      base     = 0.0;
    double      vol      = 0.0;
    double      alarm    = 0.0; // crossing it upward fires a burst
    double      value    = 0.0;
    double      committed = 0.0;
    bool        above    = false;
    std::vector<float> hist;
    Signal<float> shown{0.f};
    Signal<float> delta{0.f}; // percent change since the last commit
};

struct LogEvent {
    std::uint64_t seq   = 0;
    double        time  = 0.0; // seconds since feed start
    int           level = 0;   // 0 info, 1 warn, 2 crit
    std::string   source;
    std::string   text;
};

struct Particle {
    Vec2  pos{};
    Vec2  vel{};
    Color color{};
    float life    = 0.f;
    float maxLife = 1.f;
    float size    = 2.f;
    bool  ring    = false;
};

// Painter palette; picked per frame from the theme signal.
struct Palette {
    Color bg, surface, grid, text, muted, track;
    std::array<Color, 6> accents;
};
const Palette& Colors(bool dark);

struct Feed {
    Feed();

    // UI state
    Signal<bool>   paused{false};
    Signal<bool>   dark{true};
    Signal<double> speed{1.0};
    Signal<int>    tab{0};

    // Ticks every rendered frame; painters bind it as their repaint source.
    Signal<std::uint64_t> frame{0};

    // Header counters
    Signal<float> totalEvents{0.f};
    Signal<float> throughput{0.f};
    Signal<float> revenue{1284000.f};
    Signal<int>   alerts{0};

    std::vector<Metric> metrics;
    std::array<std::vector<float>, kSeriesCount> series;
    std::uint64_t samples = 0;
    float chartMax = 100.f;

    std::array<Signal<float>, kGaugeCount> gauges;
    float beatPhase = 0.f;

    std::array<float, kHeatCols * kHeatRows> heat{};
    int   heatCol  = kHeatCols - 1;
    int   heatTick = 0;

    std::array<std::string, kRaceCount> raceNames;
    std::array<double, kRaceCount>      raceRaw{};
    std::array<Signal<float>, kRaceCount> raceShare; // percent of the leader
    std::array<Signal<int>, kRaceCount>   raceRank;
    std::array<Signal<float>, kRaceCount> raceValue;

    std::vector<LogEvent> events;
    Signal<int>   eventCount{0};
    Signal<std::uint64_t> logVersion{0}; // last seq; changes even when the log is at its cap
    std::uint64_t nextSeq = 1;

    std::vector<Particle> particles;
    std::array<Rect, kMetricCount> cardRects{};

    double simTime   = 0.0;
    double wallTime  = 0.0;
    double sampleAcc = 0.0;
    double commitAcc = 0.0;
    std::uint64_t eventsAtCommit = 0;
    std::mt19937 rng{1234};

    void Step(float dt);
    void Sample();
    void Commit();
    void Log(int level, std::string source, std::string text);
    void Burst(Vec2 at, Color color);
    float Rand(float lo, float hi);
};

using FeedPtr = std::shared_ptr<Feed>;

// Opt-in timing: set PULSE_PROFILE=1 to print per-section averages at exit.
enum class Prof { Step, Spark, Stream, Heat, Gauge, Particles, LogRows, Count };
class ProfScope {
public:
    explicit ProfScope(Prof p);
    ~ProfScope();
private:
    Prof m_p;
    long long m_start = 0;
};

std::string Thousands(double v);
std::string ClockString(double offsetSeconds);

Elem Header(FeedPtr f);
Elem MetricGrid(FeedPtr f);
Elem MetricTable(FeedPtr f);
Elem StreamChart(FeedPtr f, float height);
Elem GaugePanel(FeedPtr f);
Elem Heatmap(FeedPtr f, float height);
Elem BarRace(FeedPtr f);
Elem EventLog(FeedPtr f);
Elem ParticleOverlay(FeedPtr f);

// Titled panel shell shared by every section.
Elem Panel(Str title, Str caption, std::string extraClass, Children children);

extern const char* const kStylesheet;

} // namespace pulse
