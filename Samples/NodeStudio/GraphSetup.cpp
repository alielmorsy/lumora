#include "NodeStudio.h"

namespace nodestudio {

void SetupNodeTypesAndSample(NodeStudioState& state)
{
    Controller& ctrl = state.graphCtrl;

    ctrl.Policy().pinColor = [](PinTypeId type) {
        if (type == PinTypeId{"float"}) return Color::Rgba(90, 180, 245, 255);
        if (type == PinTypeId{"color"}) return Color::Rgba(235, 110, 180, 255);
        if (type == PinTypeId{"vec3"}) return Color::Rgba(245, 195, 75, 255);
        return Color::Rgba(160, 170, 185, 255);
    };

    ctrl.Policy().canConnect = [&ctrl](const Model& model, PinRef from, PinRef to) {
        const Node* nFrom = model.Get(from.node);
        const Node* nTo = model.Get(to.node);
        if (!nFrom || !nTo) return false;
        const PinDesc* pFrom = ctrl.Types().Pin(nFrom->type, from.slot);
        const PinDesc* pTo = ctrl.Types().Pin(nTo->type, to.slot);
        if (!pFrom || !pTo) return false;
        if (pFrom->type == pTo->type) return true;
        if (pFrom->type == PinTypeId{"float"} && (pTo->type == PinTypeId{"color"} || pTo->type == PinTypeId{"vec3"}))
            return true;
        return false;
    };
    ctrl.Policy().acyclic = true;

    // Register node types
    NodeTypeDesc dFloat;
    dFloat.title = "Float Constant";
    dFloat.category = "Input";
    dFloat.minWidth = 140.f;
    dFloat.pins = {
        PinDesc{.name = "Val", .type = PinTypeId{"float"}, .dir = PinDir::Output, .multi = true}
    };
    dFloat.body = {PinRowCell{.output = 0}};
    const NodeTypeId tFloat = ctrl.Type("val_float", std::move(dFloat));

    NodeTypeDesc dColor;
    dColor.title = "Color Constant";
    dColor.category = "Input";
    dColor.minWidth = 150.f;
    dColor.pins = {
        PinDesc{.name = "Color", .type = PinTypeId{"color"}, .dir = PinDir::Output, .multi = true, .shape = PinShape::Square}
    };
    dColor.body = {PinRowCell{.output = 0}};
    const NodeTypeId tColor = ctrl.Type("val_color", std::move(dColor));

    NodeTypeDesc dVec3;
    dVec3.title = "Vector3";
    dVec3.category = "Input";
    dVec3.minWidth = 140.f;
    dVec3.pins = {
        PinDesc{.name = "X", .type = PinTypeId{"float"}, .dir = PinDir::Input},
        PinDesc{.name = "Y", .type = PinTypeId{"float"}, .dir = PinDir::Input},
        PinDesc{.name = "Z", .type = PinTypeId{"float"}, .dir = PinDir::Input},
        PinDesc{.name = "Vec", .type = PinTypeId{"vec3"}, .dir = PinDir::Output, .multi = true, .shape = PinShape::Diamond}
    };
    dVec3.body = {
        PinRowCell{.input = 0, .output = 3},
        PinRowCell{.input = 1},
        PinRowCell{.input = 2}
    };
    ctrl.Type("val_vec3", std::move(dVec3));

    NodeTypeDesc dMath;
    dMath.title = "Math Operation";
    dMath.category = "Math";
    dMath.minWidth = 150.f;
    dMath.pins = {
        PinDesc{.name = "A", .type = PinTypeId{"float"}, .dir = PinDir::Input},
        PinDesc{.name = "B", .type = PinTypeId{"float"}, .dir = PinDir::Input},
        PinDesc{.name = "Out", .type = PinTypeId{"float"}, .dir = PinDir::Output, .multi = true}
    };
    dMath.body = {
        PinRowCell{.input = 0, .output = 2},
        PinRowCell{.input = 1}
    };
    const NodeTypeId tMath = ctrl.Type("math_op", std::move(dMath));

    NodeTypeDesc dTrig;
    dTrig.title = "Trigonometry";
    dTrig.category = "Math";
    dTrig.minWidth = 140.f;
    dTrig.pins = {
        PinDesc{.name = "In", .type = PinTypeId{"float"}, .dir = PinDir::Input},
        PinDesc{.name = "Out", .type = PinTypeId{"float"}, .dir = PinDir::Output, .multi = true}
    };
    dTrig.body = {PinRowCell{.input = 0, .output = 1}};
    const NodeTypeId tTrig = ctrl.Type("math_trig", std::move(dTrig));

    NodeTypeDesc dBlend;
    dBlend.title = "Color Blend";
    dBlend.category = "Color";
    dBlend.minWidth = 160.f;
    dBlend.pins = {
        PinDesc{.name = "Base", .type = PinTypeId{"color"}, .dir = PinDir::Input, .shape = PinShape::Square},
        PinDesc{.name = "Blend", .type = PinTypeId{"color"}, .dir = PinDir::Input, .shape = PinShape::Square},
        PinDesc{.name = "Factor", .type = PinTypeId{"float"}, .dir = PinDir::Input},
        PinDesc{.name = "Result", .type = PinTypeId{"color"}, .dir = PinDir::Output, .multi = true, .shape = PinShape::Square}
    };
    dBlend.body = {
        PinRowCell{.input = 0, .output = 3},
        PinRowCell{.input = 1},
        PinRowCell{.input = 2}
    };
    const NodeTypeId tBlend = ctrl.Type("color_blend", std::move(dBlend));

    NodeTypeDesc dInvert;
    dInvert.title = "Invert Color";
    dInvert.category = "Color";
    dInvert.minWidth = 140.f;
    dInvert.pins = {
        PinDesc{.name = "In", .type = PinTypeId{"color"}, .dir = PinDir::Input, .shape = PinShape::Square},
        PinDesc{.name = "Out", .type = PinTypeId{"color"}, .dir = PinDir::Output, .multi = true, .shape = PinShape::Square}
    };
    dInvert.body = {PinRowCell{.input = 0, .output = 1}};
    ctrl.Type("color_invert", std::move(dInvert));

    NodeTypeDesc dOut;
    dOut.title = "Material Output";
    dOut.category = "Output";
    dOut.minWidth = 160.f;
    dOut.pins = {
        PinDesc{.name = "Base Color", .type = PinTypeId{"color"}, .dir = PinDir::Input, .shape = PinShape::Square},
        PinDesc{.name = "Roughness", .type = PinTypeId{"float"}, .dir = PinDir::Input},
        PinDesc{.name = "Metallic", .type = PinTypeId{"float"}, .dir = PinDir::Input},
        PinDesc{.name = "Normal", .type = PinTypeId{"vec3"}, .dir = PinDir::Input, .shape = PinShape::Diamond}
    };
    dOut.body = {
        PinRowCell{.input = 0},
        PinRowCell{.input = 1},
        PinRowCell{.input = 2},
        PinRowCell{.input = 3}
    };
    const NodeTypeId tOut = ctrl.Type("output_mat", std::move(dOut));

    // Seed prebuilt sample graph
    ctrl.Begin("Init Sample Graph");

    const GraphNodeId nFreq = ctrl.AddNode(tFloat, Vec2{60.f, 80.f});
    ctrl.SetNodeTitle(nFreq, "Frequency");
    state.GetProps(nFreq).paramA = 2.5;

    const GraphNodeId nTime = ctrl.AddNode(tFloat, Vec2{60.f, 220.f});
    ctrl.SetNodeTitle(nTime, "Time Multiplier");
    state.GetProps(nTime).paramA = 1.0;

    const GraphNodeId nMul = ctrl.AddNode(tMath, Vec2{280.f, 130.f});
    ctrl.SetNodeTitle(nMul, "Multiply");
    state.GetProps(nMul).operation = 2;

    const GraphNodeId nTrig = ctrl.AddNode(tTrig, Vec2{490.f, 130.f});
    ctrl.SetNodeTitle(nTrig, "Sine Wave");
    state.GetProps(nTrig).operation = 0;

    const GraphNodeId nColor = ctrl.AddNode(tColor, Vec2{280.f, 320.f});
    ctrl.SetNodeTitle(nColor, "Tint Color");

    const GraphNodeId nBlend = ctrl.AddNode(tBlend, Vec2{710.f, 210.f});
    ctrl.SetNodeTitle(nBlend, "Mixer");

    const GraphNodeId nOut = ctrl.AddNode(tOut, Vec2{960.f, 210.f});
    ctrl.SetNodeTitle(nOut, "Surface Output");

    // Connect nodes
    ctrl.Connect(PinRef{.node = nFreq, .slot = 0, .dir = PinDir::Output},
                 PinRef{.node = nMul, .slot = 0, .dir = PinDir::Input});
    ctrl.Connect(PinRef{.node = nTime, .slot = 0, .dir = PinDir::Output},
                 PinRef{.node = nMul, .slot = 1, .dir = PinDir::Input});
    ctrl.Connect(PinRef{.node = nMul, .slot = 2, .dir = PinDir::Output},
                 PinRef{.node = nTrig, .slot = 0, .dir = PinDir::Input});
    ctrl.Connect(PinRef{.node = nColor, .slot = 0, .dir = PinDir::Output},
                 PinRef{.node = nBlend, .slot = 0, .dir = PinDir::Input});
    ctrl.Connect(PinRef{.node = nTrig, .slot = 1, .dir = PinDir::Output},
                 PinRef{.node = nBlend, .slot = 2, .dir = PinDir::Input});
    ctrl.Connect(PinRef{.node = nBlend, .slot = 3, .dir = PinDir::Output},
                 PinRef{.node = nOut, .slot = 0, .dir = PinDir::Input});

    // Frame wave generator nodes
    const GraphNodeId mathGroup[] = {nFreq, nTime, nMul, nTrig};
    ctrl.Select(mathGroup);
    ctrl.AddFrame("Wave Modulation", Color::Rgba(45, 95, 150, 70));
    ctrl.ClearSelection();

    ctrl.Commit();

    // Wire notifications
    ctrl.onSelectionChanged = [&state] {
        const auto sel = state.graphCtrl.Selection();
        if (sel.empty()) {
            state.selectedNode = GraphNodeId{};
        } else {
            state.selectedNode = sel.front();
        }
        state.Touch();
    };
    ctrl.onModelChanged = [&state] {
        state.Touch();
    };
    ctrl.onNodeAdded = [&state](GraphNodeId) {
        state.Touch();
    };
    ctrl.onNodeRemoved = [&state](GraphNodeId id) {
        if (state.selectedNode.Peek().index == id.index) {
            state.selectedNode = GraphNodeId{};
        }
        state.Touch();
    };
    ctrl.onLinkAdded = [&state](LinkId) {
        state.Touch();
    };
    ctrl.onLinkRemoved = [&state](LinkId) {
        state.Touch();
    };

    state.Touch();
}

} // namespace nodestudio
