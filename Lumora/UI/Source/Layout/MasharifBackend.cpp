// MasharifBackend.cpp - the only translation unit that names masharif::; holds MasharifBackend, LayoutTree::Impl and the
// out-of-line LayoutTree methods.

#include "Layout/LayoutBridge.h"

#include <masharifcore/layout/Node.h>
#include <masharifcore/structure/Align.h>
#include <masharifcore/structure/BoxInfo.h>
#include <masharifcore/structure/CSSValue.h>
#include <masharifcore/structure/Dimension.h>
#include <masharifcore/structure/Flex.h>
#include <masharifcore/structure/FlexDirection.h>
#include <masharifcore/structure/Justify.h>
#include <masharifcore/structure/Style.h>

#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace lumora
{
    namespace
    {
        // Pure converters: Lumora primitives to Masharif style values. Gaps Masharif cannot express: cross-axis Between/Around/Evenly
        // -> Stretch; main-axis Stretch -> FlexStart.

        [[nodiscard]] masharif::JustifyContent ToJustify(Align align) noexcept
        {
            using J = masharif::JustifyContent;
            switch (align)
            {
            case Align::Auto: // resolved per kind at instantiate; treated as Start if it ever arrives
            case Align::Start: return J::FlexStart;
            case Align::Center: return J::FlexCenter;
            case Align::End: return J::FlexEnd;
            case Align::Between: return J::SpaceBetween;
            case Align::Around: return J::SpaceAround;
            case Align::Evenly: return J::SpaceEvenly;
            case Align::Stretch: return J::FlexStart; // no main-axis item stretch
            }
            return J::FlexStart;
        }

        [[nodiscard]] masharif::AlignItems ToAlign(Align align) noexcept
        {
            using A = masharif::AlignItems;
            switch (align)
            {
            case Align::Auto: // resolved per kind at instantiate; treated as Start if it ever arrives
            case Align::Start: return A::FlexStart;
            case Align::Center: return A::FlexCenter;
            case Align::End: return A::FlexEnd;
            case Align::Stretch: return A::Stretch;
            case Align::Between:
            case Align::Around:
            case Align::Evenly: return A::Stretch; // no cross-axis distribution equivalent
            }
            return A::Stretch;
        }

        /// align-content (cross-axis line packing) maps to AlignContent, which unlike align-items has between/around/evenly.
        [[nodiscard]] masharif::AlignContent ToContentAlign(Align align) noexcept
        {
            using A = masharif::AlignContent;
            switch (align)
            {
            case Align::Auto:
            case Align::Stretch: return A::Stretch;
            case Align::Start: return A::FlexStart;
            case Align::Center: return A::FlexCenter;
            case Align::End: return A::FlexEnd;
            case Align::Between: return A::SpaceBetween;
            case Align::Around: return A::SpaceAround;
            case Align::Evenly: return A::SpaceEvenly;
            }
            return A::Stretch;
        }

        [[nodiscard]] masharif::FlexWrap ToWrap(FlexWrap wrap) noexcept
        {
            switch (wrap)
            {
            case FlexWrap::NoWrap: return masharif::FlexWrap::NoWrap;
            case FlexWrap::Wrap: return masharif::FlexWrap::Wrap;
            case FlexWrap::WrapReverse: return masharif::FlexWrap::WrapReverse;
            }
            return masharif::FlexWrap::NoWrap;
        }

        /// A SizeHint → a CSS dimension. Expand resolves through flex-grow, not a dimension, so it maps
        /// to AUTO here exactly like Wrap.
        [[nodiscard]] masharif::CSSValue ToCSSSize(SizeHint hint) noexcept
        {
            using namespace masharif;
            switch (hint.kind)
            {
            case SizeHint::Kind::Fixed: return CSSValue(hint.value, CSSUnit::Px);
            case SizeHint::Kind::Percent: return CSSValue(hint.value, CSSUnit::Percent);
            case SizeHint::Kind::Expand: return CSSValue();
            case SizeHint::Kind::Wrap: return CSSValue();
            }
            return CSSValue();
        }

        [[nodiscard]] float ExpandWeight(SizeHint hint) noexcept
        {
            return hint.kind == SizeHint::Kind::Expand ? hint.value : 0.f;
        }

        /// An inset edge → a CSS Left/Top/Right/Bottom value. An auto (NaN) edge maps to AUTO so the
        /// backend's static-position pass aligns the node from the containing block's flex on that axis.
        [[nodiscard]] masharif::CSSValue ToInset(float edge) noexcept
        {
            using namespace masharif;
            return Inset::IsAuto(edge) ? CSSValue() : CSSValue(edge, CSSUnit::Px);
        }


        struct MasharifBackend
        {
            // Slab of solver nodes indexed by LayoutHandle::value (0 is null). The SharedNode held here is the ownership anchor;
            // Masharif's parent->child links are an extra reference dropped on the next SetChildren.
            std::vector<masharif::SharedNode> nodes{nullptr};
            std::vector<std::uint32_t> freeList;

            [[nodiscard]] LayoutHandle Create()
            {
                std::uint32_t index;
                if (!freeList.empty())
                {
                    index = freeList.back();
                    freeList.pop_back();
                }
                else
                {
                    index = static_cast<std::uint32_t>(nodes.size());
                    nodes.emplace_back();
                }
                nodes[index] = std::make_shared<masharif::Node>(masharif::OuterDisplay::Flex);
                return LayoutHandle{index};
            }

            void Destroy(LayoutHandle handle) noexcept
            {
                if (!handle.Valid() || handle.value >= nodes.size())
                {
                    return;
                }
                nodes[handle.value].reset();
                freeList.push_back(handle.value);
            }

            [[nodiscard]] masharif::Node& Resolve(LayoutHandle handle) const { return *nodes[handle.value]; }

            void ApplyStyle(LayoutHandle handle, const SolverInput& input)
            {
                using namespace masharif;
                Node& node = Resolve(handle);
                const LayoutStyle& style = input.style;

                // display:block routes to Masharif's normal-flow strategy; everything else stays flex. Grid containers stay flex here (the
                // Lumora grid pass places cells as absolute children).
                const bool blockFlow = style.display == DisplayMode::Block;
                node.SetDisplay(blockFlow ? OuterDisplay::Block : OuterDisplay::Flex);

                auto& flex = node.GetStyle().Modify<CSSFlex>();
                flex.Direction = style.axis == FlexAxis::Row ? FlexDirection::Row : FlexDirection::Column;
                flex.Justify = ToJustify(style.main);
                flex.Align = ToAlign(style.cross);
                flex.AlignSelf = style.alignSelf == Align::Auto ? AlignItems::AutoAlign : ToAlign(style.alignSelf);
                flex.ContentAlign = ToContentAlign(style.alignContent);
                flex.Wrap = ToWrap(style.wrap);
                flex.Order = style.order;
                const CSSValue gapValue = style.gapPct != 0.f ? CSSValue(style.gapPct, CSSUnit::Percent)
                                                              : CSSValue(style.gap, CSSUnit::Px);
                flex.Gaps.Row = gapValue;
                flex.Gaps.Column = gapValue;

                // Lumora sizes are rigid, so default flex-shrink is 0: Masharif's CSS default of 1 would crush childless content nodes to 0
                // under an over-full parent. Overflow instead; a CSS flex-shrink overrides per element.
                flex.FlexShrink = style.flexShrink;
                // Reset the conditionally-set fields so a removed class reverts (the solver node is reused across restyles).
                flex.FlexGrow = 0.f;
                flex.FlexBasis = CSSValue();

                auto& dims = node.GetStyle().Modify<Dimensions>();
                dims.Width = ToCSSSize(style.width);
                dims.Height = ToCSSSize(style.height);
                if (style.minWidth.kind != SizeHint::Kind::Wrap) dims.MinWidth = ToCSSSize(style.minWidth);
                if (style.minHeight.kind != SizeHint::Kind::Wrap) dims.MinHeight = ToCSSSize(style.minHeight);
                if (style.maxWidth.kind != SizeHint::Kind::Wrap) dims.MaxWidth = ToCSSSize(style.maxWidth);
                if (style.maxHeight.kind != SizeHint::Kind::Wrap) dims.MaxHeight = ToCSSSize(style.maxHeight);

                // A Stack is a relative containing block and each layer an absolute overlay aligned by the static-position pass from the
                // Stack's justify/align; overlays skip the grow/fill block below.
                const bool selfStack = style.axis == FlexAxis::Stack;
                const bool selfGrid = style.axis == FlexAxis::Grid;
                const bool parentStack = input.hasParent && input.parentAxis == FlexAxis::Stack;
                const bool outOfFlow = style.position != Position::Flow; // Absolute / Fixed

                // An explicit CSS flex-basis wins over the basis derived by the grow path (Wrap == auto == unset).
                const bool basisSet = style.flexBasis.kind != SizeHint::Kind::Wrap;
                if (basisSet) flex.FlexBasis = ToCSSSize(style.flexBasis);

                // Flex grow: only a MAIN-axis Expand or an explicit weight grows. A CROSS-axis Expand becomes a 100% dimension, never
                // flex-grow (it made a width=Expand,height=Fixed bar stretch vertically in a Column). Out-of-flow nodes never grow here.
                if (!blockFlow && !parentStack && !outOfFlow)
                {
                    const bool parentRow = input.hasParent && input.parentAxis == FlexAxis::Row;
                    const float grow = (parentRow ? ExpandWeight(style.width) : ExpandWeight(style.height)) + style.
                        flexWeight;
                    if (grow > 0.f)
                    {
                        flex.FlexGrow = grow;
                        if (!basisSet) flex.FlexBasis = CSSValue(0.f, CSSUnit::Px);
                    }
                    if (parentRow && style.height.kind == SizeHint::Kind::Expand)
                    {
                        dims.Height = CSSValue(100.f, CSSUnit::Percent);
                    }
                    if (input.hasParent && !parentRow && style.width.kind == SizeHint::Kind::Expand)
                    {
                        dims.Width = CSSValue(100.f, CSSUnit::Percent);
                    }
                }

                // A nonzero percent edge goes to Masharif natively (CSSUnit::Percent), otherwise the px edge.
                const auto edge = [](float px, float pct) {
                    return pct != 0.f ? CSSValue(pct, CSSUnit::Percent) : CSSValue(px, CSSUnit::Px);
                };
                auto& pad = node.GetStyle().Modify<PaddingEdge>();
                pad.Left = edge(style.padding.left, style.paddingPct.left);
                pad.Top = edge(style.padding.top, style.paddingPct.top);
                pad.Right = edge(style.padding.right, style.paddingPct.right);
                pad.Bottom = edge(style.padding.bottom, style.paddingPct.bottom);

                auto& margin = node.GetStyle().Modify<MarginEdge>();
                margin.Left = edge(style.margin.left, style.marginPct.left);
                margin.Top = edge(style.margin.top, style.marginPct.top);
                margin.Right = edge(style.margin.right, style.marginPct.right);
                margin.Bottom = edge(style.margin.bottom, style.marginPct.bottom);

                if (outOfFlow)
                {
                    // Out of flow: each edge is a px inset or AUTO (NaN); an auto edge uses the containing block's flex static position on that axis.
                    // Absolute resolves against the nearest anchor ancestor (else the root), Fixed against the surface. Expand fills, never flex-grow.
                    dims.Position = style.position == Position::Fixed ? PositionType::Fixed : PositionType::Absolute;
                    dims.Left = ToInset(style.inset.left);
                    dims.Top = ToInset(style.inset.top);
                    dims.Right = ToInset(style.inset.right);
                    dims.Bottom = ToInset(style.inset.bottom);
                    if (style.width.kind == SizeHint::Kind::Expand)
                    {
                        dims.Width = CSSValue(100.f, CSSUnit::Percent);
                    }
                    if (style.height.kind == SizeHint::Kind::Expand)
                    {
                        dims.Height = CSSValue(100.f, CSSUnit::Percent);
                    }
                }
                else if (parentStack)
                {
                    // A Stack layer: absolute overlay with auto insets, aligned by the static-position pass from the Stack's justify/align. Expand fills.
                    dims.Position = PositionType::Absolute;
                    dims.Left = dims.Right = dims.Top = dims.Bottom = CSSValue(); // Auto
                    if (style.width.kind == SizeHint::Kind::Expand)
                    {
                        dims.Width = CSSValue(100.f, CSSUnit::Percent);
                    }
                    if (style.height.kind == SizeHint::Kind::Expand)
                    {
                        dims.Height = CSSValue(100.f, CSSUnit::Percent);
                    }
                }
                else if (selfStack || selfGrid || style.anchor)
                {
                    // A positioning context (containing block for absolute descendants). Relative is laid out like Static, so this only stops
                    // FindContainingBlock's walk here. Opt in via `.anchor`, or by being a Stack or a Grid.
                    dims.Position = PositionType::Relative;
                }
                else
                {
                    // Default: a plain container is NOT a positioning context, so FindContainingBlock walks past it to the nearest anchor (else
                    // the root); `.anchor` opts a panel in.
                    dims.Position = PositionType::Static;
                }

                node.MarkDirtyToRoot();
            }

            void SetChildren(LayoutHandle handle, std::span<const LayoutHandle> children)
            {
                std::vector<masharif::SharedNode> resolved;
                resolved.reserve(children.size());
                for (const LayoutHandle child : children)
                {
                    resolved.push_back(nodes[child.value]);
                }
                Resolve(handle).SetChildren(std::move(resolved));
            }

            /// Pin a content-measured leaf's size onto an axis left auto (Wrap); an explicit Fixed/Percent/Expand still wins. Called twice
            /// per frame (first-pass MaxContent measure, then the wrap-aware re-measure), so it dirties the node for the second solve.
            void PinContentSize(LayoutHandle handle, Vec2 size, bool pinWidth, bool pinHeight)
            {
                masharif::Node& node = Resolve(handle);
                auto& dims = node.GetStyle().Modify<masharif::Dimensions>();
                if (pinWidth)
                {
                    dims.Width = masharif::CSSValue(size.x, masharif::CSSUnit::Px);
                }
                if (pinHeight)
                {
                    dims.Height = masharif::CSSValue(size.y, masharif::CSSUnit::Px);
                }
                if (pinWidth || pinHeight)
                {
                    node.MarkDirtyToRoot();
                }
            }

            /// Pin the root to the surface so percentages and stretch resolve against it.
            void PinSurface(LayoutHandle handle, float width, float height)
            {
                auto& dims = Resolve(handle).GetStyle().Modify<masharif::Dimensions>();
                dims.Width = masharif::CSSValue(width, masharif::CSSUnit::Px);
                dims.Height = masharif::CSSValue(height, masharif::CSSUnit::Px);
                Resolve(handle).MarkDirtyToRoot();
            }

            void Solve(LayoutHandle handle, float width, float height) { Resolve(handle).Calculate(width, height); }

            [[nodiscard]] Rect ReadRect(LayoutHandle handle) const
            {
                const masharif::Layout& layout = Resolve(handle).GetLayout();
                return Rect{layout.ComputedX, layout.ComputedY, layout.ComputedWidth, layout.ComputedHeight};
            }

            [[nodiscard]] std::uint32_t SolveCount(LayoutHandle handle) const
            {
                return Resolve(handle).GetLayout().StrategyRuns;
            }
        };

        static_assert(LayoutBackend<MasharifBackend>, "MasharifBackend must satisfy the LayoutBackend trait");
    } // namespace

    struct LayoutTree::Impl
    {
        MasharifBackend backend;
        LayoutDriver<MasharifBackend> driver{backend};
    };

    LayoutTree::LayoutTree() : m_impl(std::make_unique<Impl>())
    {
    }

    LayoutTree::~LayoutTree() = default;

    LayoutHandle LayoutTree::Attach() { return m_impl->backend.Create(); }

    void LayoutTree::Detach(LayoutHandle handle) noexcept { m_impl->backend.Destroy(handle); }

    void LayoutTree::Run(NodeArena& arena, NodeId root, float surfaceW, float surfaceH,
                         const TextMeasurer* measurer, ImageProvider* images, FrameCounters* counters)
    {
        const MeasureContext measure{measurer, &arena, images, counters};
        m_impl->driver.Run(arena, root, surfaceW, surfaceH, measure);
    }

    std::uint32_t LayoutTree::SolveCount(LayoutHandle handle) const { return m_impl->backend.SolveCount(handle); }
} // namespace lumora
