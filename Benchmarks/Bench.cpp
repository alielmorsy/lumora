// LumoraBench - the headless frame benchmark.
// Drives production frames (MountedApp::Tick / Layout / Paint) against a NullCanvas; going through MountedApp seeds
// AppCtx::style and runs the shell and virtual-list/positioner/anchor passes.
// Reports min / median / p95 over measured frames (never a mean: frame cost is long-tailed); warm-up frames are discarded.
// Allocations per frame are counted via a replaced global operator new, a steadier signal than wall time.
// Usage: LumoraBench [--scale N] [--frames N] [--warmup N] [--scene substr] [--font path] [--csv]

#include "BenchScenes.h"
#include "FakeFonts.h"

#include <lumora/Debug/FrameCounters.h>
#include <lumora/MountedApp.h>
#include <lumora/Paint/NullCanvas.h>
#include <lumora/Text/FontSystem.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Allocation counting: replacing the global operators is the only way to see allocations made through std containers we do
// not own. Atomics are relaxed (the bench is single-threaded; freetype and the CRT may not be).

namespace
{
    std::atomic<std::uint64_t> g_allocCount{0};
    std::atomic<std::uint64_t> g_allocBytes{0};

    [[nodiscard]] std::uint64_t Allocs() noexcept { return g_allocCount.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t AllocBytes() noexcept { return g_allocBytes.load(std::memory_order_relaxed); }
} // namespace

void* operator new(std::size_t size)
{
    g_allocCount.fetch_add(1, std::memory_order_relaxed);
    g_allocBytes.fetch_add(size, std::memory_order_relaxed);
    if (void* p = std::malloc(size ? size : 1))
    {
        return p;
    }
    throw std::bad_alloc();
}

void* operator new[](std::size_t size) { return operator new(size); }

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    g_allocCount.fetch_add(1, std::memory_order_relaxed);
    g_allocBytes.fetch_add(size, std::memory_order_relaxed);
    return std::malloc(size ? size : 1);
}

void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept
{
    return operator new(size, tag);
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

namespace
{
    using namespace lumora;
    using Clock = std::chrono::steady_clock;

    constexpr float kSurfaceW = 1920.f;
    constexpr float kSurfaceH = 1080.f;
    constexpr float kStep = 1.f / 60.f;

    struct Options
    {
        int scale = 2000;
        int frames = 240;
        int warmup = 40;
        std::string filter;
        std::string font;
        bool csv = false;
    };

    struct Stats
    {
        double min = 0.0;
        double median = 0.0;
        double p95 = 0.0;
    };

    [[nodiscard]] Stats Summarise(std::vector<double> samples)
    {
        if (samples.empty())
        {
            return {};
        }
        std::sort(samples.begin(), samples.end());
        const auto at = [&samples](double q)
        {
            const std::size_t last = samples.size() - 1;
            const auto index = static_cast<std::size_t>(q * static_cast<double>(last) + 0.5);
            return samples[std::min(index, last)];
        };
        return Stats{samples.front(), at(0.5), at(0.95)};
    }

    /// A measured run of one scene against one measurer.
    struct Result
    {
        std::string scene;
        std::string measurer;
        Stats tick, layout, paint, total;
        double allocsPerFrame = 0.0;
        double bytesPerFrame = 0.0;
        FrameCounters last{};   ///< the final measured frame
        FrameCounters worst{};  ///< per-counter maximum across measured frames
        bool counterDrift = false; ///< a counter moved between measured frames (steady state is not steady)
    };

    void AccumulateWorst(FrameCounters& worst, const FrameCounters& frame)
    {
        auto* w = reinterpret_cast<std::uint32_t*>(&worst);
        const auto* f = reinterpret_cast<const std::uint32_t*>(&frame);
        constexpr std::size_t fields = sizeof(FrameCounters) / sizeof(std::uint32_t);
        for (std::size_t i = 0; i < fields; ++i)
        {
            w[i] = std::max(w[i], f[i]);
        }
    }

    [[nodiscard]] bool SameCounters(const FrameCounters& a, const FrameCounters& b)
    {
        return std::memcmp(&a, &b, sizeof(FrameCounters)) == 0;
    }

    [[nodiscard]] Result RunScene(const bench::Scene& scene, const TextMeasurer& fonts,
                                  std::string_view measurerName, const Options& opts)
    {
        Result out;
        out.scene = scene.name;
        out.measurer = std::string(measurerName);

        MountedApp app(scene.build(), &fonts);
        app.SetContentScale(1.f);
        if (!scene.css.empty())
        {
            app.SetStylesheetSource(scene.css);
        }

        NullCanvas canvas;
        std::vector<double> tick, layout, paint, total;
        tick.reserve(static_cast<std::size_t>(opts.frames));
        layout.reserve(static_cast<std::size_t>(opts.frames));
        paint.reserve(static_cast<std::size_t>(opts.frames));
        total.reserve(static_cast<std::size_t>(opts.frames));

        std::uint64_t allocsAtStart = 0;
        std::uint64_t bytesAtStart = 0;
        FrameCounters previous{};
        bool havePrevious = false;

        const int all = opts.warmup + opts.frames;
        for (int frame = 0; frame < all; ++frame)
        {
            const bool measured = frame >= opts.warmup;
            if (measured && frame == opts.warmup)
            {
                allocsAtStart = Allocs();
                bytesAtStart = AllocBytes();
            }

            if (scene.step)
            {
                scene.step(app, frame); // outside the timed region: input is the driver, not the subject
            }

            const auto t0 = Clock::now();
            app.Tick(kStep);
            const auto t1 = Clock::now();
            app.Layout(kSurfaceW, kSurfaceH);
            const auto t2 = Clock::now();
            canvas.BeginFrame({kSurfaceW, kSurfaceH});
            // Same two-step as the runtime: the canvas-wide skip is only sound once the host knows nothing wants to draw, so
            // driving Paint unconditionally would measure a frame the shipped path never runs.
            if (!app.WantsRepaint() && canvas.ReuseFrame())
            {
                app.NoteFrameReused();
            }
            else
            {
                app.Paint(canvas);
            }
            canvas.EndFrame();
            const auto t3 = Clock::now();

            if (!measured)
            {
                continue;
            }
            const auto ms = [](Clock::time_point a, Clock::time_point b)
            {
                return std::chrono::duration<double, std::milli>(b - a).count();
            };
            tick.push_back(ms(t0, t1));
            layout.push_back(ms(t1, t2));
            paint.push_back(ms(t2, t3));
            total.push_back(ms(t0, t3));

            const FrameCounters& counters = app.Counters();
            AccumulateWorst(out.worst, counters);
            if (havePrevious && !SameCounters(previous, counters))
            {
                out.counterDrift = true;
            }
            previous = counters;
            havePrevious = true;
            out.last = counters;
        }

        const double frames = static_cast<double>(std::max(1, opts.frames));
        out.allocsPerFrame = static_cast<double>(Allocs() - allocsAtStart) / frames;
        out.bytesPerFrame = static_cast<double>(AllocBytes() - bytesAtStart) / frames;
        out.tick = Summarise(std::move(tick));
        out.layout = Summarise(std::move(layout));
        out.paint = Summarise(std::move(paint));
        out.total = Summarise(std::move(total));
        return out;
    }

    void PrintHeader(const Options& opts, std::string_view fontNote)
    {
        std::printf("LumoraBench  scale=%d  frames=%d (after %d warm-up)  surface=%.0fx%.0f  fonts=%s\n",
                    opts.scale, opts.frames, opts.warmup, kSurfaceW, kSurfaceH, std::string(fontNote).c_str());
        std::printf("%-18s %-6s | %25s %25s %25s | %10s %9s\n", "scene", "fonts",
                    "tick ms  min/med/p95", "layout ms  min/med/p95", "paint ms  min/med/p95",
                    "allocs/fr", "KiB/fr");
        std::printf("%s\n", std::string(140, '-').c_str());
    }

    void PrintRow(const Result& r)
    {
        const auto phase = [](const Stats& s)
        {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%7.3f %7.3f %7.3f", s.min, s.median, s.p95);
            return std::string(buffer);
        };
        std::printf("%-18s %-6s | %25s %25s %25s | %10.1f %9.1f\n", r.scene.c_str(), r.measurer.c_str(),
                    phase(r.tick).c_str(), phase(r.layout).c_str(), phase(r.paint).c_str(),
                    r.allocsPerFrame, r.bytesPerFrame / 1024.0);
    }

    void PrintCounters(const Result& r)
    {
        const FrameCounters& c = r.last;
        std::printf("    work/frame: refresh %u (dirty %u) solve %u readback %u remeasure %u (repin %u) "
                    "grids %u/%u | emit %u (cull %u hidden %u layer %u clip %u) | cascade %u pseudo %u | "
                    "measure %u shape %u | frameCbs %u/%u passes %u effects %u%s\n",
                    c.refreshVisited, c.refreshDirty, c.solveCalls, c.readBackVisited, c.remeasureVisited,
                    c.remeasureRepins, c.gridsPlaced, c.gridsVisited, c.emitVisited, c.emitCulled,
                    c.emitHidden, c.emitLayers, c.emitClips, c.cascadeCalls, c.pseudoWrites,
                    c.textMeasureCalls, c.glyphCacheCalls, c.frameCallbacksInvoked, c.frameCallbacksAlive,
                    c.framePasses, c.effectRuns,
                    r.counterDrift ? "  [drifts between frames]" : "");
        // Incremental-paint ledger: on a static frame `reused` and `recorded` are 0 and `frameReuse` is 1; on a one-leaf
        // change `reused` should dwarf `recorded`, and neither should scale with the tree.
        std::printf("    paint/frame: recorded %u reused %u (%u B) | frameReuse %u canvasReuse %u | "
                    "list %u B, %u records replayed\n",
                    c.emitRecorded, c.emitReused, c.emitReusedBytes, c.paintFrameReused,
                    c.paintCanvasReused, c.paintRecordedBytes, c.paintReplayed);
    }

    void PrintCsv(const std::vector<Result>& results, const Options& opts)
    {
        std::printf("scene,fonts,scale,tick_min,tick_med,tick_p95,layout_min,layout_med,layout_p95,"
                    "paint_min,paint_med,paint_p95,allocs_per_frame,bytes_per_frame,refresh,refresh_dirty,"
                    "solve,readback,remeasure,emit,emit_culled,cascade,measure,shape,frame_callbacks,"
                    "emit_recorded,emit_reused,paint_frame_reused,paint_list_bytes,paint_replayed\n");
        for (const Result& r : results)
        {
            const FrameCounters& c = r.last;
            std::printf("%s,%s,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.1f,%.1f,"
                        "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
                        r.scene.c_str(), r.measurer.c_str(), opts.scale, r.tick.min, r.tick.median, r.tick.p95,
                        r.layout.min, r.layout.median, r.layout.p95, r.paint.min, r.paint.median, r.paint.p95,
                        r.allocsPerFrame, r.bytesPerFrame, c.refreshVisited, c.refreshDirty, c.solveCalls,
                        c.readBackVisited, c.remeasureVisited, c.emitVisited, c.emitCulled, c.cascadeCalls,
                        c.textMeasureCalls, c.glyphCacheCalls, c.frameCallbacksInvoked,
                        c.emitRecorded, c.emitReused, c.paintFrameReused, c.paintRecordedBytes,
                        c.paintReplayed);
        }
    }

    /// Candidate font files, in priority order. A real face is optional: without one, text scenes run against the fake
    /// measurer and the report says so.
    [[nodiscard]] std::string FindFont(const std::string& explicitPath)
    {
        std::vector<std::string> candidates;
        if (!explicitPath.empty())
        {
            candidates.push_back(explicitPath);
        }
#ifdef LUMORA_BENCH_FONT
        candidates.emplace_back(LUMORA_BENCH_FONT);
#endif
        candidates.emplace_back("C:/Windows/Fonts/segoeui.ttf");
        candidates.emplace_back("C:/Windows/Fonts/arial.ttf");
        candidates.emplace_back("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
        for (const std::string& path : candidates)
        {
            std::error_code ec;
            if (std::filesystem::exists(path, ec))
            {
                return path;
            }
        }
        return {};
    }

    [[nodiscard]] bool LoadFace(FontSystem& fonts, const std::string& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            return false;
        }
        std::vector<char> blob((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (blob.empty())
        {
            return false;
        }
        const auto bytes = std::span<const std::byte>(reinterpret_cast<const std::byte*>(blob.data()),
                                                      blob.size());
        return fonts.RegisterFont(bytes, FontSpec{.family = "bench"}) != FaceId::Invalid;
    }

    [[nodiscard]] Options ParseArgs(int argc, char** argv)
    {
        Options opts;
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view arg = argv[i];
            const auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string{}; };
            if (arg == "--scale") opts.scale = std::max(1, std::atoi(next().c_str()));
            else if (arg == "--frames") opts.frames = std::max(1, std::atoi(next().c_str()));
            else if (arg == "--warmup") opts.warmup = std::max(0, std::atoi(next().c_str()));
            else if (arg == "--scene") opts.filter = next();
            else if (arg == "--font") opts.font = next();
            else if (arg == "--csv") opts.csv = true;
            else std::printf("ignoring unknown argument '%s'\n", std::string(arg).c_str());
        }
        return opts;
    }
} // namespace

int main(int argc, char** argv)
{
    const Options opts = ParseArgs(argc, argv);

    FakeFonts fake;
    FontSystem real;
    const std::string fontPath = FindFont(opts.font);
    const bool haveReal = !fontPath.empty() && LoadFace(real, fontPath);
    if (haveReal)
    {
        real.SetDefaultFamily("bench");
    }

    const std::vector<bench::Scene> scenes = bench::Catalogue(opts.scale);
    std::vector<Result> results;
    results.reserve(scenes.size() + 1);

    for (const bench::Scene& scene : scenes)
    {
        if (!opts.filter.empty() && scene.name.find(opts.filter) == std::string::npos)
        {
            continue;
        }
        results.push_back(RunScene(scene, fake, "fake", opts));
        if (scene.text && haveReal)
        {
            // The same scene twice: the delta is the cost of shaping and rasterising, separate from layout and emit.
            results.push_back(RunScene(scene, real, "real", opts));
        }
    }

    if (opts.csv)
    {
        PrintCsv(results, opts);
        return 0;
    }

    PrintHeader(opts, haveReal ? fontPath : std::string("fake only (no face found; pass --font)"));
    for (const Result& r : results)
    {
        PrintRow(r);
        PrintCounters(r);
    }
    return 0;
}
