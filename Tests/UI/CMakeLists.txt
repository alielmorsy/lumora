lumora_add_test(lumora_tests SOURCES
    ReactiveTests.cpp
    NodeTests.cpp
    WidgetTests.cpp
    LayoutTests.cpp
    ControlFlowTests.cpp
    VirtualListTests.cpp
    DragDropTests.cpp
    DoubleClickTests.cpp
    TextSelectionTests.cpp
    NumberFieldTests.cpp
    FocusTests.cpp
    InputRoutingTests.cpp
    AnimationTests.cpp
    PaintTests.cpp
    ImageTests.cpp
    CountersTests.cpp
    DockDetachTests.cpp
)

lumora_add_test(lumora_style_tests SOURCES
    StyleParserTests.cpp
    StyleMatchTests.cpp
    StyleApplyTests.cpp
    TransformTests.cpp
    StylePseudoTests.cpp
    StyleInheritVarsMediaTests.cpp
    StyleTransitionTests.cpp
    StyleKeyframesTests.cpp
)

lumora_add_test(lumora_graph_tests SOURCES
    GraphPainterRepaintTests.cpp
    GraphCameraTests.cpp
    GraphTypesTests.cpp
    GraphModelTests.cpp
    GraphLayoutTests.cpp
    GraphHistoryTests.cpp
    GraphControllerTests.cpp
    GraphPaintTests.cpp
    GraphInputTests.cpp
    GraphWidgetTests.cpp
    GraphSerializeTests.cpp
    GraphFlowTests.cpp
)
