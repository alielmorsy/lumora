# UI core: layout, style, widgets and reactivity. No RHI dependency.
if (NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 20)
endif ()
include(${LUMORA_ROOT_DIR}/cmake/deps/masharif.cmake)
include(${LUMORA_ROOT_DIR}/cmake/deps/libtess2.cmake)

add_library(lumora STATIC
    Source/Lumora.cpp
    Source/Animation/Animation.cpp
    Source/Reactive/EffectNode.cpp
    Source/Reactive/ReactiveGraph.cpp
    Source/Node/NodeArena.cpp
    Source/Node/KindDescriptor.cpp
    Source/Node/View.cpp
    Source/Runtime/Instantiate.cpp
    Source/Runtime/ControlRuntime.cpp
    Source/Runtime/MountedApp.cpp
    Source/Runtime/ContextMenuHost.cpp
    Source/Runtime/ContextMenuOverlay.cpp
    Source/Runtime/DragHost.cpp
    Source/Runtime/MenuBar.cpp
    Source/Runtime/MenuBarView.cpp
    Source/Runtime/OverlayRoot.cpp
    Source/Runtime/Paint.cpp
    Source/Runtime/DisplayList.cpp
    Source/Runtime/DebugOverlay.cpp
    Source/Runtime/FocusRing.cpp
    Source/Input/Dispatch.cpp
    Source/Input/FocusTraversal.cpp
    Source/Paint/Paint2D.cpp
    Source/Widgets/TextEmit.cpp
    Source/Widgets/Container.cpp
    Source/Widgets/Scroll.cpp
    Source/Widgets/VirtualList.cpp
    Source/Widgets/Grid.cpp
    Source/Widgets/Text.cpp
    Source/Widgets/TextSelectionScope.cpp
    Source/Widgets/Image.cpp
    Source/Widgets/Icon.cpp
    Source/Widgets/Divider.cpp
    Source/Widgets/Spacer.cpp
    Source/Widgets/Expanded.cpp
    Source/Widgets/Button.cpp
    Source/Widgets/Switch.cpp
    Source/Widgets/Checkbox.cpp
    Source/Widgets/ComboBox.cpp
    Source/Widgets/Chip.cpp
    Source/Widgets/Badge.cpp
    Source/Widgets/Breadcrumb.cpp
    Source/Widgets/Accordion.cpp
    Source/Widgets/NumberField.cpp
    Source/Widgets/Input.cpp
    Source/Widgets/TextEditor.cpp
    Source/Widgets/Painter.cpp
    Source/Widgets/Svg.cpp
    Source/Widgets/Splitter.cpp
    Source/Widgets/Dock.cpp
    Source/Graph/Types.cpp
    Source/Graph/Model.cpp
    Source/Graph/Layout.cpp
    Source/Graph/History.cpp
    Source/Graph/Graph.cpp
    Source/Graph/Paint.cpp
    Source/Graph/Input.cpp
    Source/Graph/Serialize.cpp
    Source/Graph/View.cpp
    Source/Graph/Search.cpp
    Source/Graph/Fields.cpp
    Source/Svg/XmlReader.cpp
    Source/Svg/AttrParser.cpp
    Source/Svg/PathParser.cpp
    Source/Svg/SvgBuilder.cpp
    Source/Svg/SvgDocument.cpp
    Source/Svg/SvgTessellate.cpp
    Source/Svg/SvgRenderer.cpp
    Source/Svg/SvgImage.cpp
    Source/Layout/AspectRatio.cpp
    Source/Layout/LayoutBridge.cpp
    Source/Layout/MasharifBackend.cpp
    Source/Style/Tokenizer.cpp
    Source/Style/ValueParse.cpp
    Source/Style/Parser.cpp
    Source/Style/Matcher.cpp
    Source/Style/Cascade.cpp
    Source/Style/Convert.cpp
    Source/Style/Resolve.cpp
    Source/Style/Transition.cpp
    Source/Style/Keyframes.cpp
    Source/Style/StyleEngine.cpp
    Source/Style/StyleResolver.cpp
)
add_library(lumora::lumora ALIAS lumora)
lumora_defaults(lumora)
target_include_directories(lumora PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Include> PRIVATE Source)
target_link_libraries(lumora PUBLIC lumora::base PRIVATE lumora::log lumora::platform masharif::masharifcore libtess2)
