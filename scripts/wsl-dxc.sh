#!/bin/bash
# dxc for the "wsl" preset: runs the Windows dxc.exe (found on the interop PATH, e.g. the Vulkan SDK's Bin) and
# translates absolute Linux paths to Windows ones. Set LUMORA_DXC_EXE to point at a specific dxc.exe.
exe="${LUMORA_DXC_EXE:-$(command -v dxc.exe)}"
if [[ -z "$exe" ]]; then
    echo "wsl-dxc: dxc.exe not found; put the Vulkan SDK Bin on the Windows PATH or set LUMORA_DXC_EXE" >&2
    exit 127
fi

args=()
for arg in "$@"; do
    if [[ "$arg" == /* ]]; then args+=("$(wslpath -w "$arg")"); else args+=("$arg"); fi
done
exec "$exe" "${args[@]}"
