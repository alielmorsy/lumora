#include "NodeStudio.h"

namespace nodestudio {

const char* const kNodeStudioStylesheet = R"css(
.app {
    display: flex;
    flex-direction: column;
    width: 100%;
    height: 100%;
    background-color: #1a1c20;
    color: #e4e6ec;
    font-size: 13px;
}

.menubar {
    display: flex;
    flex-direction: row;
    align-items: center;
    height: 38px;
    padding: 0 14px;
    background-color: #202328;
    gap: 8px;
}

.brand {
    display: flex;
    flex-direction: row;
    align-items: baseline;
    gap: 8px;
}

.brand-title {
    font-size: 15px;
    font-weight: bold;
    color: #61afef;
}

.brand-sub {
    font-size: 12px;
    color: #7f8796;
}

.menu-actions {
    display: flex;
    flex-direction: row;
    align-items: center;
    gap: 4px;
}

.menu-btn {
    padding: 4px 10px;
    border-radius: 4px;
    color: #c5c8d2;
    background-color: transparent;
}

.menu-btn:hover {
    background-color: #2c3038;
    color: #ffffff;
}

.menu-right {
    display: flex;
    flex-direction: row;
    align-items: center;
}

.menu-hint {
    font-size: 12px;
    color: #636b7b;
}

.workspace {
    display: flex;
    flex-direction: row;
    flex-grow: 1;
    overflow: hidden;
    background-color: #16171a;
}

.panel-palette {
    background-color: #1f2227;
}

.palette-panel {
    display: flex;
    flex-direction: column;
    padding: 10px;
    gap: 10px;
}

.panel-header {
    display: flex;
    flex-direction: column;
    gap: 8px;
}

.panel-title {
    font-size: 14px;
    font-weight: bold;
    color: #d7dae2;
}

.palette-search {
    background-color: #18191d;
    border: 1px solid #323640;
    border-radius: 4px;
    padding: 6px 8px;
    color: #e4e6ec;
}

.palette-scroll {
    flex-grow: 1;
}

.palette-list {
    display: flex;
    flex-direction: column;
    gap: 6px;
}

.palette-item {
    display: flex;
    flex-direction: row;
    align-items: center;
    padding: 8px 10px;
    border-radius: 4px;
    background-color: #252830;
    border: 1px solid #2f3440;
}

.palette-item:hover {
    background-color: #2b2f39;
    border: 1px solid #3c4252;
}

.palette-item-text {
    display: flex;
    flex-direction: column;
    gap: 2px;
}

.palette-item-title {
    font-size: 13px;
    color: #e4e6ec;
    font-weight: bold;
}

.palette-item-category {
    font-size: 11px;
    color: #7b8394;
}

.palette-add-btn {
    padding: 3px 8px;
    border-radius: 4px;
    font-size: 11px;
    color: #61afef;
    background-color: #222e3e;
}

.palette-add-btn:hover {
    background-color: #2d3d52;
    color: #8cc7ff;
}

.panel-canvas {
    background-color: #1e2127;
    flex-grow: 1;
}

.canvas-wrapper {
    width: 100%;
    height: 100%;
}

.panel-inspector {
    background-color: #1f2227;
}

.inspector-panel {
    display: flex;
    flex-direction: column;
    padding: 12px;
    gap: 12px;
}

.inspector-scroll {
    flex-grow: 1;
}

.inspector-content {
    display: flex;
    flex-direction: column;
    gap: 12px;
}

.inspector-badge-row {
    display: flex;
    flex-direction: row;
    align-items: center;
}

.inspector-badge {
    font-size: 12px;
    font-weight: bold;
    padding: 2px 8px;
    border-radius: 4px;
    background-color: #28374d;
    color: #61afef;
}

.inspector-dim {
    font-size: 11px;
    color: #687182;
}

.prop-group {
    display: flex;
    flex-direction: column;
    gap: 4px;
}

.prop-label {
    font-size: 12px;
    color: #8c94a4;
}

.prop-input {
    background-color: #18191d;
    border: 1px solid #323640;
    border-radius: 4px;
    padding: 6px 8px;
    color: #e4e6ec;
}

.prop-number {
    background-color: #18191d;
    border: 1px solid #323640;
    border-radius: 4px;
    padding: 4px 6px;
    color: #e4e6ec;
}

.prop-combo {
    border-radius: 4px;
}

.inspector-actions {
    display: flex;
    flex-direction: column;
    gap: 6px;
}

.action-btn {
    border-radius: 4px;
    border: 1px solid #343946;
    background-color: #242730;
    color: #c5c8d2;
    padding: 6px 10px;
}

.action-btn:hover {
    background-color: #2d313c;
    color: #ffffff;
}

.delete-btn {
    border-radius: 4px;
    border: 1px solid #5a282c;
    background-color: #331f22;
    color: #e06c75;
    padding: 6px 10px;
}

.delete-btn:hover {
    background-color: #45262a;
    color: #ff858e;
}

.inspector-empty {
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    padding: 40px 16px;
    gap: 8px;
}

.empty-title {
    font-size: 14px;
    font-weight: bold;
    color: #7a8292;
}

.empty-desc {
    font-size: 12px;
    color: #555b68;
    text-align: center;
}

.statusbar {
    display: flex;
    flex-direction: row;
    align-items: center;
    height: 26px;
    padding: 0 12px;
    background-color: #1b1d22;
    font-size: 12px;
}

.status-section {
    display: flex;
    flex-direction: row;
    align-items: center;
    gap: 8px;
}

.status-item {
    color: #abb2bf;
}

.status-dim {
    color: #4b5263;
}

.status-hint {
    color: #636b7b;
}
)css";

} // namespace nodestudio
