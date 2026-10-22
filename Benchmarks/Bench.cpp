// LumoraBench - the headless frame benchmark.
// Drives production frames (MountedApp::Tick / Layout / Paint) against a NullCanvas; going through MountedApp seeds
// AppCtx::style and runs the shell and virtual-list/positioner/anchor passes.
// Reports min / median / p95 over measured frames (never a mean: frame cost is long-tailed); warm-up frames are discarded.
// Allocations per frame are counted via a replaced global operator new, a steadier signal than wall time.
// Usage: LumoraBench [--scale N] [--frames N] [--warmup N] [--scene substr] [--font path] [--csv] [--alloc-sites]
// --alloc-sites (Windows): heap still held after warm-up, grouped by the first non-std caller of operator new.

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

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#endif

// Allocation counting: replacing the global operators is the only way to see allocations made through std containers we do
// not own. Atomics are relaxed (the bench is single-threaded; freetype and the CRT may not be).

namespace
{
    std::atomic<std::uint64_t> g_allocCount{0};
    std::atomic<std::uint64_t> g_allocBytes{0};
    std::atomic<std::int64_t> g_liveBytes{0};
    std::atomic<std::int64_t> g_peakBytes{0};

    [[nodiscard]] std::uint64_t Allocs() noexcept { return g_allocCount.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t AllocBytes() noexcept { return g_allocBytes.load(std::memory_order_relaxed); }
    [[nodiscard]] std::int64_t LiveBytes() noexcept { return g_liveBytes.load(std::memory_order_relaxed); }
    [[nodiscard]] std::int64_t PeakBytes() noexcept { return g_peakBytes.load(std::memory_order_relaxed); }
    void ResetPeak() noexcept { g_peakBytes.store(LiveBytes(), std::memory_order_relaxed); }

    // Each block carries its size (and its call-site slot when --alloc-sites is on) in a 16-byte header, so delete can keep
    // the live totals.
    constexpr std::size_t kHeader = 16; // malloc alignment on 64-bit targets (MSVC max_align_t is only 8)

    struct BlockHeader
    {
        std::size_t size;
        std::uint32_t site;
    };
    static_assert(sizeof(BlockHeader) <= kHeader);

    // ponytail: fixed open-addressed table, no locking; fine for the single-threaded bench, sites past capacity land in slot 0.
    constexpr std::uint32_t kSiteSlots = 1u << 14;
    constexpr int kSiteFrames = 24;

    struct Site
    {
        std::uint32_t hash = 0;
        std::uint16_t depth = 0;
        void* frames[kSiteFrames]{};
        std::int64_t liveBytes = 0;
        std::int64_t liveBlocks = 0;
    };

    Site g_sites[kSiteSlots];
    bool g_trackSites = false;

    [[nodiscard]] std::uint32_t RecordSite() noexcept
    {
#ifdef _WIN32
        void* frames[kSiteFrames];
        DWORD hash = 0;
        const USHORT depth = RtlCaptureStackBackTrace(2, kSiteFrames, frames, &hash);
        const std::uint32_t h = hash ? static_cast<std::uint32_t>(hash) : 1u;
        for (std::uint32_t i = 0; i < kSiteSlots - 1; ++i)
        {
            const std::uint32_t slot = 1 + (h + i) % (kSiteSlots - 1);
            Site& site = g_sites[slot];
            if (site.hash == h)
            {
                return slot;
            }
            if (site.hash == 0)
            {
                site.hash = h;
                site.depth = depth;
                std::memcpy(site.frames, frames, depth * sizeof(void*));
                return slot;
            }
        }
#endif
        return 0;
    }

    [[nodiscard]] void* TrackedAlloc(std::size_t size) noexcept
    {
        auto* raw = static_cast<std::byte*>(std::malloc(size + kHeader));
        if (!raw)
        {
            return nullptr;
        }
        auto* header = reinterpret_cast<BlockHeader*>(raw);
        header->size = size;
        header->site = 0;
        if (g_trackSites)
        {
            g_trackSites = false; // RtlCaptureStackBackTrace must not recurse into us
            header->site = RecordSite();
            g_sites[header->site].liveBytes += static_cast<std::int64_t>(size);
            ++g_sites[header->site].liveBlocks;
            g_trackSites = true;
        }
        g_allocCount.fetch_add(1, std::memory_order_relaxed);
        g_allocBytes.fetch_add(size, std::memory_order_relaxed);
        const std::int64_t live = g_liveBytes.fetch_add(static_cast<std::int64_t>(size), std::memory_order_relaxed) +
                                  static_cast<std::int64_t>(size);
        std::int64_t peak = g_peakBytes.load(std::memory_order_relaxed);
        while (live > peak && !g_peakBytes.compare_exchange_weak(peak, live, std::memory_order_relaxed))
        {
        }
        return raw + kHeader;
    }

    void TrackedFree(void* p) noexcept
    {
        if (!p)
        {
            return;
        }
        auto* raw = static_cast<std::byte*>(p) - kHeader;
        const auto* header = reinterpret_cast<const BlockHeader*>(raw);
        g_liveBytes.fetch_sub(static_cast<std::int64_t>(header->size), std::memory_order_relaxed);
        if (header->site)
        {
            g_sites[header->site].liveBytes -= static_cast<std::int64_t>(header->size);
            --g_sites[header->site].liveBlocks;
        }
        std::free(raw);
    }
} // namespace

void* operator new(std::size_t size)
{
    if (void* p = TrackedAlloc(size))
    {
        return p;
    }
    throw std::bad_alloc();
}

void* operator new[](std::size_t size) { return operator new(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return TrackedAlloc(size); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return TrackedAlloc(size); }

void operator delete(void* p) noexcept { TrackedFree(p); }
void operator delete[](void* p) noexcept { TrackedFree(p); }
void operator delete(void* p, std::size_t) noexcept { TrackedFree(p); }
void operator delete[](void* p, std::size_t) noexcept { TrackedFree(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { TrackedFree(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { TrackedFree(p); }

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
        int frames = 200;
        int warmup = 40;
        std::string filter;
        std::string font;
        bool csv = false;
        bool allocSites = false;
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
        double mountMs = 0.0;            ///< construct + first Tick/Layout/Paint
        std::int64_t blueprintBytes = 0; ///< heap held by the Elem tree before it is mounted
        std::int64_t peakMountBytes = 0; ///< heap high-water mark above the pre-mount baseline, through warm-up
        std::int64_t liveBytes = 0;      ///< heap held after warm-up
        std::int64_t growthBytes = 0;    ///< heap change across the measured frames (non-zero = steady state still allocates)
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

    /// Live heap per call site, keyed by the first three callers outside the allocator and std.
    void PrintAllocSites(const std::string& sceneName)
    {
#ifdef _WIN32
        static const bool symbols = []
        {
            SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
            return SymInitialize(GetCurrentProcess(), nullptr, TRUE) != FALSE;
        }();
        if (!symbols)
        {
            std::printf("alloc-sites: SymInitialize failed\n");
            return;
        }

        const auto name = [](void* addr) -> std::string
        {
            alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 512];
            auto* info = reinterpret_cast<SYMBOL_INFO*>(buffer);
            info->SizeOfStruct = sizeof(SYMBOL_INFO);
            info->MaxNameLen = 511;
            DWORD64 displacement = 0;
            if (!SymFromAddr(GetCurrentProcess(), reinterpret_cast<DWORD64>(addr), &displacement, info))
            {
                return "?";
            }
            std::string out(info->Name);
            IMAGEHLP_LINE64 line{};
            line.SizeOfStruct = sizeof(line);
            DWORD lineDisp = 0;
            if (SymGetLineFromAddr64(GetCurrentProcess(), reinterpret_cast<DWORD64>(addr), &lineDisp, &line))
            {
                const std::string_view file = line.FileName;
                const std::size_t slash = file.find_last_of("\\/");
                out += " (" + std::string(file.substr(slash == std::string_view::npos ? 0 : slash + 1)) + ":" +
                       std::to_string(line.LineNumber) + ")";
            }
            return out;
        };
        const auto boring = [](const std::string& n)
        {
            for (const char* prefix : {"operator new", "std::", "`anonymous namespace'::", "malloc", "lumora::MakeRef",
                                       "lumora::Ref<", "_malloc", "?"})
            {
                if (n.rfind(prefix, 0) == 0)
                {
                    return true;
                }
            }
            return false;
        };

        struct Group
        {
            std::int64_t bytes = 0;
            std::int64_t blocks = 0;
        };
        std::vector<std::pair<std::string, Group>> groups;
        std::int64_t total = 0;
        for (const Site& site : g_sites)
        {
            if (site.liveBytes <= 0)
            {
                continue;
            }
            total += site.liveBytes;
            std::string key;
            int kept = 0;
            for (int f = 0; f < site.depth && kept < 3; ++f)
            {
                std::string n = name(site.frames[f]);
                if (boring(n))
                {
                    continue;
                }
                key += kept ? "\n            <- " : "";
                key += n;
                ++kept;
            }
            auto it = std::find_if(groups.begin(), groups.end(), [&](const auto& g) { return g.first == key; });
            if (it == groups.end())
            {
                groups.emplace_back(key, Group{});
                it = groups.end() - 1;
            }
            it->second.bytes += site.liveBytes;
            it->second.blocks += site.liveBlocks;
        }
        std::sort(groups.begin(), groups.end(),
                  [](const auto& a, const auto& b) { return a.second.bytes > b.second.bytes; });
        std::printf("alloc-sites %s: %.1f KiB live in tracked sites\n", sceneName.c_str(),
                    static_cast<double>(total) / 1024.0);
        for (std::size_t i = 0; i < std::min<std::size_t>(groups.size(), 30); ++i)
        {
            std::printf("%9.1f KiB %7lld blk  %s\n", static_cast<double>(groups[i].second.bytes) / 1024.0,
                        static_cast<long long>(groups[i].second.blocks), groups[i].first.c_str());
        }
#else
        std::printf("alloc-sites %s: Windows only\n", sceneName.c_str());
#endif
    }

    [[nodiscard]] Result RunScene(const bench::Scene& scene, const TextMeasurer& fonts,
                                  std::string_view measurerName, const Options& opts)
    {
        Result out;
        out.scene = scene.name;
        out.measurer = std::string(measurerName);

        NullCanvas canvas;
        const std::int64_t liveAtStart = LiveBytes();
        if (opts.allocSites)
        {
            std::memset(g_sites, 0, sizeof(g_sites));
            g_trackSites = true;
        }
        ResetPeak();
        const auto mount0 = Clock::now();

        Elem tree = scene.build();
        out.blueprintBytes = LiveBytes() - liveAtStart;
        MountedApp app(std::move(tree), &fonts);
        app.SetContentScale(1.f);
        if (!scene.css.empty())
        {
            app.SetStylesheetSource(scene.css);
        }
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
                out.liveBytes = LiveBytes() - liveAtStart;
                out.peakMountBytes = PeakBytes() - liveAtStart;
                if (opts.allocSites)
                {
                    g_trackSites = false;
                    PrintAllocSites(scene.name);
                }
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
            if (frame == 0)
            {
                out.mountMs = std::chrono::duration<double, std::milli>(t3 - mount0).count();
            }

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

        out.growthBytes = LiveBytes() - liveAtStart - out.liveBytes;
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
        const double nodes = std::max(1u, c.refreshVisited);
        std::printf("    memory: mount %.2f ms | blueprint %.1f KiB | live %.1f KiB (%.0f B/node over %u nodes) peak %.1f KiB | "
                    "steady growth %lld B\n",
                    r.mountMs, static_cast<double>(r.blueprintBytes) / 1024.0, static_cast<double>(r.liveBytes) / 1024.0, static_cast<double>(r.liveBytes) / nodes,
                    c.refreshVisited, static_cast<double>(r.peakMountBytes) / 1024.0,
                    static_cast<long long>(r.growthBytes));
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
                    "emit_recorded,emit_reused,paint_frame_reused,paint_list_bytes,paint_replayed,mount_ms,live_bytes,"
                    "peak_mount_bytes,growth_bytes\n");
        for (const Result& r : results)
        {
            const FrameCounters& c = r.last;
            std::printf("%s,%s,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.1f,%.1f,"
                        "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%.3f,%lld,%lld,%lld\n",
                        r.scene.c_str(), r.measurer.c_str(), opts.scale, r.tick.min, r.tick.median, r.tick.p95,
                        r.layout.min, r.layout.median, r.layout.p95, r.paint.min, r.paint.median, r.paint.p95,
                        r.allocsPerFrame, r.bytesPerFrame, c.refreshVisited, c.refreshDirty, c.solveCalls,
                        c.readBackVisited, c.remeasureVisited, c.emitVisited, c.emitCulled, c.cascadeCalls,
                        c.textMeasureCalls, c.glyphCacheCalls, c.frameCallbacksInvoked,
                        c.emitRecorded, c.emitReused, c.paintFrameReused, c.paintRecordedBytes,
                        c.paintReplayed, r.mountMs, static_cast<long long>(r.liveBytes),
                        static_cast<long long>(r.peakMountBytes), static_cast<long long>(r.growthBytes));
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
            else if (arg == "--alloc-sites") opts.allocSites = true;
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
