#include <lumora/rhi/Lumora.h>

#include "RHIImageProvider.h"

#include <lumora/Input/Event.h>
#include <lumora/Paint/Canvas.h>
#include <lumora/rhi/PlatformKeys.h>
#include <lumora/Paint/NullCanvas.h>
#include <lumora/Text/Glyphs.h>

#include <lumora/platform/InputEvents.h>
#include <lumora/platform/SystemMetrics.h>

#include <lumora/rhi/RHI.h>

#include <lumora/log/Log.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
#include <ranges>
#include <utility>

LUMORA_LOG_CATEGORY("Lumora")

#if 0

#define TIMER_START(name) auto name = std::chrono::high_resolution_clock::now()
#define TIMER_MS(name)    std::chrono::duration<float, std::milli>(std::chrono::high_resolution_clock::now() - (name)).count()
#define TIMER_LOG_DEBUG(...) LOG_DEBUG(__VA_ARGS__)
#else
#define TIMER_START(name)
#define TIMER_MS(name)
#define TIMER_LOG_DEBUG(...)
#endif
namespace lumora
{
    namespace
    {
        /// Adapts a free-function root into the class-component path so a lambda root and a Component
        /// subclass mount through the same erasure.
        struct FnRoot
        {
            std::function<Elem()> fn;
            Elem Render() { return fn ? fn() : Elem{}; }
        };

        [[nodiscard]] EventType MapType(platform::InputEventType type)
        {
            switch (type)
            {
                case platform::InputEventType::PointerMove: return EventType::MouseMove;
                case platform::InputEventType::PointerDown: return EventType::MouseDown;
                case platform::InputEventType::PointerUp:   return EventType::MouseUp;
                case platform::InputEventType::Scroll:      return EventType::Scroll;
                case platform::InputEventType::KeyDown:     return EventType::KeyDown;
                case platform::InputEventType::KeyUp:       return EventType::KeyUp;
                case platform::InputEventType::Text:        return EventType::TextInput;
            }
            return EventType::MouseMove;
        }

        [[nodiscard]] Event ToRoutedEvent(const platform::InputEvent& in, float scale)
        {
            // Scroll deltas are wheel notches, not px, so only the position is scaled.
            Event out{};
            out.Type        = MapType(in.type);
            out.Position    = PhysicalToLogical({in.position.x, in.position.y}, scale);
            out.ScrollDelta = {in.scrollDelta.x, in.scrollDelta.y};
            out.Button      = ToLumora(in.button);
            out.KeyCode     = ToLumora(in.key);
            out.Mods        = ToLumora(in.mods);
            std::memcpy(out.Text, in.text, sizeof(out.Text));
            return out;
        }
    } // namespace

    Lumora::Lumora(const LumoraDesc& desc)
    {
        if (desc.measurer)
        {
            m_measurer = desc.measurer;
        }
        else
        {
            m_ownedFonts.emplace();
            m_measurer = &*m_ownedFonts;
        }
        m_clipboard = desc.clipboard ? desc.clipboard : &m_ownedClipboard;
        m_assetLoader = desc.assetLoader;

        if (desc.rhi)
        {
            m_rhi = desc.rhi;
            m_painter.Init(*m_rhi);
            if (desc.shareImagesWith != nullptr && desc.shareImagesWith->m_rhi == m_rhi)
                m_painter.ShareImagesWith(desc.shareImagesWith->m_painter);
            if (!m_painter.LoadPipelines(desc.spirvDir, desc.colorFormat, desc.spirvLoader))
            {
                LOG_WARN("pipelines not loaded from '{}' - UI geometry records but will not draw until "
                         "the SPIR-V shaders are present",
                         desc.spirvDir);
            }
        }

#if 1
        m_processMonitor = std::make_unique<platform::ProcessMonitor>();
#endif

        if (m_rhi) {
            m_imageProvider = std::make_unique<RHIImageProvider>(&m_painter, m_assetLoader);
        }
    }

    Lumora::~Lumora()
    {
        if (m_rhi)
        {
            m_rhi->WaitIdle();
            m_painter.Shutdown();
        }
    }

    MountedApp* Lumora::MountRoot(Elem root)
    {
        auto app = std::make_unique<MountedApp>(std::move(root), m_measurer, m_assetLoader,
                                                m_imageProvider.get());
        app->SetInteractionEnv(m_measurer, m_clipboard);
        app->SetContentScale(m_contentScale);
        MountedApp* raw = app.get();
        m_apps.push_back(std::move(app));
        if (!m_stylesheetSource.empty()) raw->SetStylesheetSource(m_stylesheetSource);
        return raw;
    }

    MountedApp* Lumora::Mount(std::function<Elem()> root)
    {
        return MountRoot(Elem(FnRoot{std::move(root)}));
    }

    void Lumora::Unmount(MountedApp* app)
    {
        std::erase_if(m_apps, [app](const std::unique_ptr<MountedApp>& p) { return p.get() == app; });
    }

    MenuBarController& Lumora::MenuBar()
    {
        return m_apps.front()->MenuBar();
    }

    ImageHandle Lumora::RegisterExternalImage(rhi::TextureViewHandle view, std::uint32_t width,
                                              std::uint32_t height)
    {
        if (!m_rhi) return {};
        return m_painter.RegisterExternalTexture(view, width, height);
    }

    void Lumora::ReleaseImage(ImageHandle image) { m_painter.FreeTexture(image); }

    bool Lumora::Dispatch(const platform::InputEvent& event)
    {
        const Event routed = ToRoutedEvent(event, m_contentScale);
#if 1
        const bool hasModifiers = Has(routed.Mods, KeyMods::Shift) || Has(routed.Mods, KeyMods::Ctrl) ||
                                  Has(routed.Mods, KeyMods::Alt) || Has(routed.Mods, KeyMods::Super);
        if (!hasModifiers && routed.Type == EventType::KeyDown && routed.KeyCode == Key::F11)
        {
            m_perfOverlay = !m_perfOverlay;
            if (m_perfOverlay && m_processMonitor)
            {
                (void)m_processMonitor->Sample(); // prime the CPU delta baseline so the first reading is accurate
                m_perfAccum = 0.f;
            }
            LOG_INFO("perf overlay {}", m_perfOverlay ? "on" : "off");
            return true;
        }
        if (!hasModifiers && routed.Type == EventType::KeyDown && routed.KeyCode == Key::F12)
        {
            m_debugOverlay = !m_debugOverlay;
            LOG_INFO("debug overlay {}", m_debugOverlay ? "on" : "off");
            return true;
        }
        if (routed.Type == EventType::MouseMove)
        {
            m_pointerPos = routed.Position;
        }
        if (m_debugOverlay && routed.Type == EventType::MouseDown && routed.Button == PointerButton::Left)
        {
            m_pointerPos = routed.Position;
            if (const DebugOverlayFlags hit = HitTestDebugControls(m_surface, routed.Position);
                hit != DebugOverlayFlags::None)
            {
                m_debugFlags ^= hit;
                return true;
            }
            if (DebugControlsBounds(m_surface).Contains(routed.Position))
            {
                return true; // swallow clicks on the panel chrome (title / padding)
            }
        }
#endif
        bool        consumed = false;
        for (auto& app : m_apps)
        {
            consumed |= app->Dispatch(routed);
        }
        return consumed;
    }

    bool Lumora::DropExternal(const DragPayload& payload, Vec2 physicalPos)
    {
        const Vec2 logicalPos = PhysicalToLogical(physicalPos, m_contentScale);
        for (const auto& app : m_apps | std::views::reverse)
        {
            if (app->DropExternal(payload, logicalPos)) return true;
        }
        return false;
    }

    void Lumora::SetStylesheetSource(std::string_view css)
    {
        m_stylesheetSource.assign(css);
        for (auto& app : m_apps)
        {
            app->SetStylesheetSource(css);
        }
    }

    void Lumora::LoadStylesheet(std::string_view path)
    {
        std::string text;
        const AssetLoader load = m_assetLoader ? m_assetLoader : &DefaultFileLoad;
        if (!load(path, text))
        {
            LOG_WARN("stylesheet load failed: {}", path);
            return;
        }
        SetStylesheetSource(text);
    }

    void Lumora::ReloadStylesheet(std::string_view path) { LoadStylesheet(path); }

    void Lumora::Update(float deltaSeconds)
    {
        for (auto& app : m_apps)
        {
            app->Tick(deltaSeconds);
        }
#if 1
        if (m_perfOverlay && m_processMonitor)
        {
            constexpr float kPerfSampleInterval = 0.5f; // refresh the readout at a fixed, legible cadence
            m_perfAccum += deltaSeconds;
            if (m_perfAccum >= kPerfSampleInterval)
            {
                const platform::ProcessMetrics m = m_processMonitor->Sample();
                m_perfStats = PerfStats{m.cpuPercent, m.ramBytes};
                m_perfAccum = 0.f;
            }
        }
#endif
    }

    void Lumora::Layout(float width, float height)
    {
        // The host passes the physical surface size; divide by the content scale so apps lay out in logical pixels (the
        // painter scales back).
        const float inv = m_contentScale > 0.f ? 1.f / m_contentScale : 1.f;
        const float lw  = width * inv;
        const float lh  = height * inv;
        for (auto& app : m_apps)
        {
            app->Layout(lw, lh);
        }
    }

    void Lumora::PaintInto(Canvas& canvas) const
    {
        for (auto& app : m_apps)
        {
            app->Paint(canvas);
        }
    }

    void Lumora::Paint(rhi::CommandBuffer& cmd, std::uint32_t frameIndex, rhi::TextureViewHandle target,
                       rhi::Extent2D extent, std::optional<rhi::Color> clear)
    {
        if (!m_rhi)
        {
            return;
        }

        if (m_scaleDirty)
        {
            TIMER_START(t0);
            NullCanvas warm;
            PaintInto(warm);
            m_scaleDirty = false;
            TIMER_LOG_DEBUG("Paint::WarmAtlas {:.3f} ms", TIMER_MS(t0));
        }

        {
            TIMER_START(t0);
            m_painter.SyncGlyphAtlas(AtlasKind::Coverage, m_measurer->Atlas(AtlasKind::Coverage));
            m_painter.SyncGlyphAtlas(AtlasKind::Color, m_measurer->Atlas(AtlasKind::Color));
            TIMER_LOG_DEBUG("Paint::SyncGlyphAtlas {:.3f} ms", TIMER_MS(t0));
        }

        {
            TIMER_START(t0);
            Canvas& canvas = m_painter.BeginFrame(cmd, frameIndex, target, extent, clear);
            TIMER_LOG_DEBUG("Paint::BeginFrame {:.3f} ms", TIMER_MS(t0));

            TIMER_START(t1);
            // Whole-frame skip: every app's retained recording is current and no host overlay is drawing, so last frame's
            // geometry is submitted as-is. Apps are told so their counters describe a real frame.
            const bool overlays = m_debugOverlay || m_perfOverlay;
            bool reused = false;
            if (!overlays)
            {
                bool wanted = false;
                for (auto& app : m_apps)
                {
                    wanted = app->WantsRepaint() || wanted;
                }
                if (!wanted && canvas.ReuseFrame())
                {
                    for (auto& app : m_apps)
                    {
                        app->NoteFrameReused();
                    }
                    reused = true;
                }
            }
            if (!reused)
            {
                PaintInto(canvas);
            }
            TIMER_LOG_DEBUG("Paint::PaintInto {:.3f} ms", TIMER_MS(t1));

#if 1
            const float invScale = m_contentScale > 0.f ? 1.f / m_contentScale : 1.f;
            m_surface = Vec2{static_cast<float>(extent.width) * invScale, static_cast<float>(extent.height) * invScale};
            if (m_perfOverlay)
            {
                m_perfStats.styleMs = 0.f;
                m_perfStats.measureMs = 0.f;
                m_perfStats.paintMs = 0.f;
                for (auto& app : m_apps)
                {
                    const FrameCounters& c = app->Counters();
                    m_perfStats.styleMs += c.styleMs;
                    m_perfStats.measureMs += c.measureMs;
                    m_perfStats.paintMs += c.paintMs;
                }
            }
            if (m_debugOverlay)
            {
                TIMER_START(tDbg);
                for (auto& app : m_apps)
                {
                    app->PaintDebugOverlay(canvas, m_surface, m_debugFlags);
                }
                const DebugOverlayFlags hovered = HitTestDebugControls(m_surface, m_pointerPos);
                PaintDebugControls(canvas, m_measurer, m_surface, m_debugFlags, hovered);
                TIMER_LOG_DEBUG("Paint::DebugOverlay {:.3f} ms", TIMER_MS(tDbg));
            }
            if (m_perfOverlay)
            {
                TIMER_START(tPerf);
                PaintPerfOverlay(canvas, m_measurer, m_surface, m_perfStats);
                TIMER_LOG_DEBUG("Paint::PerfOverlay {:.3f} ms", TIMER_MS(tPerf));
            }
#endif

            TIMER_START(tEnd);
            m_painter.EndFrame();
            TIMER_LOG_DEBUG("Paint::EndFrame {:.3f} ms", TIMER_MS(tEnd));
        }
    }

    void Lumora::SetContentScale(float scale)
    {
        const float s = scale > 0.f ? scale : 1.f;
        if (s == m_contentScale)
        {
            return;
        }
        m_contentScale = s;
        m_painter.SetContentScale(s);
        if (m_measurer)
        {
            m_measurer->SetContentScale(s);
        }
        for (auto& app : m_apps)
        {
            app->SetContentScale(s);
        }
        m_scaleDirty = true;
        LOG_INFO("content scale → {}", s);
    }

    void Lumora::SetDebugOverlay(bool enabled) noexcept
    {
#if 1
        m_debugOverlay = enabled;
#else
        (void)enabled;
#endif
    }

    bool Lumora::DebugOverlayEnabled() const noexcept
    {
#if 1
        return m_debugOverlay;
#else
        return false;
#endif
    }

    void Lumora::SetDebugOverlayFlags(DebugOverlayFlags flags) noexcept
    {
#if 1
        m_debugFlags = flags;
#else
        (void)flags;
#endif
    }

    FontSystem& Lumora::Fonts()
    {
        return *m_ownedFonts;
    }

    Ref<Lumora> MakeLumora(const LumoraDesc& desc)
    {
        return MakeRef<Lumora>(desc);
    }
} // namespace lumora
