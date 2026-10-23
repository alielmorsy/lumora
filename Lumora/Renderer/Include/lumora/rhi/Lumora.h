#pragma once

#include <lumora/Api.h>
#include <lumora/Ref.h>
#include <lumora/Clipboard.h>
#include <lumora/Debug/DebugOverlay.h>
#include <lumora/Elem.h>
#include <lumora/Foundation/Vec2.h>
#include <lumora/MountedApp.h>
#include <lumora/Paint/DisplayList.h>
#include <lumora/Text/FontSystem.h>
#include <lumora/Text/TextMeasurer.h>
#include <lumora/Widgets/Component.h>

#include <lumora/rhi/RHIPainter.h>

#include <lumora/rhi/Common.h>
#include <lumora/rhi/Formats.h>
#include <lumora/rhi/Handles.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace lumora::rhi
{
    class RHI;
    class CommandBuffer;
} // namespace lumora::rhi

namespace lumora::platform
{
    struct InputEvent;
    class ProcessMonitor;
} // namespace lumora::platform

namespace lumora
{
    class MenuBarController;
    class ImageProvider;
    class Lumora;

    /// One recorded frame, handed from the UI thread (RecordFrame) to the render thread (RenderFrame). RenderFrame consumes
    /// it: the list is swapped out, ops are cleared and atlas versions zeroed, so render each packet exactly once, in order.
    struct FramePacket
    {
        DisplayList   list;            // whole frame: every app + focus ring + debug/perf overlays
        bool          reuse = false;   // nothing changed: re-present last frame
        float         contentScale = 1.f;
        rhi::Extent2D extent{};        // physical size the UI laid out for
        std::uint64_t seq = 0;
        AtlasDelta    atlas[2];        // [Coverage, Color]
        PainterOps    ops;
    };

    /// Construction parameters; every field is optional and defaults to an owned object. A null `rhi` runs headless
    /// (Update/Layout/RecordFrame run, Paint and RenderFrame are no-ops).
    /// Pass your own `measurer`/`clipboard` to swap the defaults (e.g. in tests).
    struct LumoraDesc
    {
        rhi::RHI* rhi = nullptr;
        std::string spirvDir;
        /// Resolves the UI shaders by file name instead of reading `spirvDir`. Null → read from disk.
        SpirvLoader spirvLoader = nullptr;
        rhi::Format colorFormat = rhi::Format::B8G8R8A8_Unorm;
        TextMeasurer* measurer = nullptr;
        Clipboard* clipboard = nullptr;
        /// File/asset reader for every mounted app's build walk (Svg loads `src` through it). Null uses DefaultFileLoad.
        AssetLoader assetLoader = nullptr;
        /// Another runtime on the same `rhi` whose image table this one uses, so an ImageHandle registered on
        /// either draws in both. Null → this runtime owns its own table.
        Lumora* shareImagesWith = nullptr;
    };

    /// The runtime: owns the painter, text measurer, clipboard and every mounted app, and drives the per-frame contract. No
    /// globals; instances never interact.
    class LUMORA_API Lumora : public RefCounted
    {
    public:
        explicit Lumora(const LumoraDesc& desc);
        ~Lumora() override;

        Lumora(const Lumora&) = delete;
        Lumora& operator=(const Lumora&) = delete;

        /// Mount a class component (anything with `Render() -> Elem`); the instance is moved into the tree and lives for the
        /// app's lifetime.
        template <typename T>
            requires Component<std::remove_cvref_t<T>>
        MountedApp* Mount(T&& component)
        {
            return MountRoot(Elem(std::forward<T>(component)));
        }

        /// Mount a free-function root: a builder invoked once to produce the root Elem.
        MountedApp* Mount(std::function<Elem()> root);

        /// Unmount and dispose an app previously returned by Mount.
        void Unmount(MountedApp* app);

        /// The first mounted app's menubar model, for host code to fill menus without a component scope. Asserts if nothing is mounted.
        [[nodiscard]] MenuBarController& MenuBar();

        /// Feed one host input event to every mounted app. Call once per pending event before Update. Returns true if any app consumed it.
        bool Dispatch(const platform::InputEvent& event);

        /// Delivers an external drop (physical client pixels) to mounted apps, topmost first; the first to accept ends
        /// delivery. Returns true if one did.
        bool DropExternal(const DragPayload& payload, Vec2 physicalPos);

        /// Loads a `.css` stylesheet through the asset loader and applies it to every mounted app (warns if unreadable).
        /// Remembered and replayed onto apps mounted later, so it may precede Mount; call again to hot-reload.
        void LoadStylesheet(std::string_view path);
        void ReloadStylesheet(std::string_view path);

        /// Applies an in-memory CSS string to every mounted app and remembers it for apps mounted later.
        void SetStylesheetSource(std::string_view css);

        /// Per frame, in order: Update ticks reactive graphs, Layout solves every app, Paint emits every app into `target`.
        /// Pass `clear` when the UI owns `target`; leave it unset to composite over an existing scene (loadOp=Load).
        /// Paint is RecordFrame + RenderFrame on the calling thread.
        void Update(float deltaSeconds);
        void Layout(float width, float height);
        void Paint(rhi::CommandBuffer& cmd, std::uint32_t frameIndex, rhi::TextureViewHandle target,
                   rhi::Extent2D extent, std::optional<rhi::Color> clear = std::nullopt);

        /// UI thread, after Update + Layout: records the frame into `out` (overwritten). Needs no GPU.
        void RecordFrame(FramePacket& out, rhi::Extent2D extent);
        /// Render thread, inside the host's frame and outside any render scope: uploads the packet's images and atlas rows,
        /// then draws its list into `target`. Touches only the painter and render-owned state.
        void RenderFrame(FramePacket& in, rhi::CommandBuffer& cmd, std::uint32_t frameIndex,
                         rhi::TextureViewHandle target, rhi::Extent2D extent,
                         std::optional<rhi::Color> clear = std::nullopt);

        /// Sets the display's logical to physical pixel ratio (OS DPI scale); the UI is laid out in logical pixels and scaled
        /// at the surface.
        /// Pass `window->GetDpiScale()` at startup and on DpiChanged. Default 1.0 (headless).
        void SetContentScale(float scale);
        [[nodiscard]] float ContentScale() const noexcept { return m_contentScale; }

        /// Headless / test seam: emit every app onto an arbitrary canvas (the caller brackets it).
        void PaintInto(Canvas& canvas) const;

        /// Debug inspector overlay (box model, glyph borders, hover info); debug builds only, no-ops in release.
        /// Toggled by F12; SetDebugOverlayFlags picks the layers (default: All).
        void SetDebugOverlay(bool enabled) noexcept;
        [[nodiscard]] bool DebugOverlayEnabled() const noexcept;
        void SetDebugOverlayFlags(DebugOverlayFlags flags) noexcept;

        /// The owned default font system, for registering faces. Valid only when no measurer override was supplied.
        [[nodiscard]] FontSystem& Fonts();
        [[nodiscard]] const TextMeasurer& Measurer() const noexcept { return *m_measurer; }
        [[nodiscard]] Clipboard& ClipboardRef() const noexcept { return *m_clipboard; }

        // Registers an external RHI texture view as an opaque ImageHandle. The view is borrowed: ReleaseImage never destroys
        // the texture. Invalid when headless. Both are UI-thread calls; a release takes effect at the next RenderFrame.
        [[nodiscard]] ImageHandle RegisterExternalImage(rhi::TextureViewHandle view, std::uint32_t width,
                                                        std::uint32_t height);
        void ReleaseImage(ImageHandle image);

    private:
        MountedApp* MountRoot(Elem root);

        // UI thread: everything below except the render-thread block.
        std::optional<FontSystem> m_ownedFonts;
        DefaultClipboard m_ownedClipboard;
        TextMeasurer* m_measurer = nullptr;
        Clipboard* m_clipboard = nullptr;
        AssetLoader m_assetLoader = nullptr;

        rhi::RHI* m_rhi = nullptr; // set once at construction
        // Render thread, except its "any thread"/"UI thread" calls (UploadTexture, FreeTexture, RegisterExternalTexture,
        // TakePendingOps).
        RHIPainter m_painter;
        DisplayList m_lastList; // render thread: the last list drawn, replayed when the canvas cannot reuse its geometry
        FramePacket m_syncPacket; // Paint's packet
        std::unique_ptr<ImageProvider> m_imageProvider; // GPU image decoder/cache; null when headless

        struct SentAtlas
        {
            std::uint64_t version = 0;
            int width = 0;
            int height = 0;
        };
        SentAtlas m_sentAtlas[2]{}; // last atlas state put in a packet, per plane
        std::uint64_t m_frameSeq = 0;

        float m_contentScale = 1.f;
        bool  m_scaleDirty   = false; // a scale change needs a glyph-atlas warm pass before next upload

        std::vector<std::unique_ptr<MountedApp>> m_apps;
        std::string m_stylesheetSource;

#if 1
        bool              m_debugOverlay = false;
        DebugOverlayFlags m_debugFlags   = DebugOverlayFlags::Default;
        Vec2              m_pointerPos{};
        Vec2              m_surface{};

        bool                                      m_perfOverlay = false;
        std::unique_ptr<platform::ProcessMonitor> m_processMonitor;
        float                                     m_perfAccum = 0.f; // seconds since the last perf sample
        PerfStats                                 m_perfStats{};     // last sampled stats, painted every frame
#endif
    };

    [[nodiscard]] LUMORA_API Ref<Lumora> MakeLumora(const LumoraDesc& desc);
} // namespace lumora
