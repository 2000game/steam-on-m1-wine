#!/usr/bin/env bash
#
# run-dx12probe.sh — Check what D3D12 can do in this Wine, without a game.
#
# Builds dx12probe.exe (mingw), compiles the SM 6.x test shaders with
# Microsoft's dxc (Windows build, run under Wine), then runs the probe in a
# separate prefix (~/.wine-dx12test). The probe reports the D3D12 feature
# level, shader model and caps, renders a triangle and runs compute shaders
# (DXBC and DXIL) with readback, and measures presents/s in a window.
#
# DXMT replaces Wine's dxgi.dll in the Wine lib dir, and Wine always loads
# builtin DLLs from there. D3D12 needs Wine's own dxgi, so the script copies
# the original (dxgi.dll.orig), clears its builtin marker and loads it as a
# native DLL from the probe folder. With DXGI_DLL it copies a given DLL
# (e.g. DXVK's) instead.
#
# Env (all optional):
#   ICD=/path/icd.json    Vulkan driver (default: the MoltenVK bundled with Wine)
#   D3D12_DIR=/path       folder with a native d3d12.dll/d3d12core.dll
#                         (e.g. vkd3d-proton); default is Wine's vkd3d
#   DXGI_DLL=/path/dxgi.dll  native dxgi to use instead of Wine's (e.g. DXVK's,
#                         needed for vkd3d-proton swapchains)
#   SECONDS_PER_LOOP=4    present-loop duration, 0 skips the window test

# shellcheck source=../../lib/common.sh
source "$(dirname "$0")/../../lib/common.sh"
require_wine_installed
require_cmd x86_64-w64-mingw32-g++

src="$(cd "$(dirname "$0")" && pwd)"
work="${WORK_DIR:-$HOME/.cache/steam-on-m1-wine/dx12probe}"
dxc_dir="$HOME/.cache/steam-on-m1-wine/dxc"
export WINEPREFIX="$HOME/.wine-dx12test"
mkdir -p "$work"

if [[ ! -d "$WINEPREFIX/drive_c/windows" ]]; then
    log_info "Creating test prefix $WINEPREFIX"
    wine_run wineboot -i >/dev/null 2>&1
fi

x86_64-w64-mingw32-g++ -O1 -static -o "$work/dx12probe.exe" "$src/dx12probe.cpp" \
    -ld3d12 -ldxgi -ld3dcompiler -luuid || die "dx12probe build failed"

if [[ ! -f "$dxc_dir/bin/x64/dxc.exe" ]]; then
    require_cmd gh
    log_info "Downloading Microsoft dxc"
    tmp=$(mktemp -d)
    gh release download -R microsoft/DirectXShaderCompiler -p 'dxc_*.zip' -D "$tmp" || die "dxc download failed"
    mkdir -p "$dxc_dir" && unzip -oq "$tmp"/dxc_*.zip -d "$dxc_dir" 2>/dev/null; rm -rf "$tmp"
fi
(
    cd "$src"
    dxc() { wine_run "$dxc_dir/bin/x64/dxc.exe" "$@" >/dev/null; }
    dxc -T vs_6_0 -E vs gfx.hlsl -Fo "$work/vs.dxil"
    dxc -T ps_6_0 -E ps gfx.hlsl -Fo "$work/ps.dxil"
    dxc -T cs_6_0 -E cs cs.hlsl -Fo "$work/cs.dxil"
    dxc -T cs_6_6 -E cs cs66.hlsl -Fo "$work/cs66.dxil"
) || log_warn "dxc failed; DXIL tests will be skipped"

orig="$WINE_APP/Contents/Resources/wine/lib/wine/x86_64-windows/dxgi.dll.orig"
[[ -f "$orig" ]] || orig="$WINE_APP/Contents/Resources/wine/lib/wine/x86_64-windows/dxgi.dll"
if [[ -n "${DXGI_DLL:-}" ]]; then
    cp "$DXGI_DLL" "$work/dxgi.dll" || die "cannot copy DXGI_DLL"
else
    python3 - "$orig" "$work/dxgi.dll" <<'EOF'
import sys
b = bytearray(open(sys.argv[1], "rb").read())
i = b.find(b"Wine builtin DLL")
if i >= 0:
    b[i:i + 16] = b"Xine nativeX DLL"
open(sys.argv[2], "wb").write(b)
EOF
fi

overrides="dxgi=n"
rm -f "$work/d3d12.dll" "$work/d3d12core.dll"
if [[ -n "${D3D12_DIR:-}" ]]; then
    cp "$D3D12_DIR"/d3d12.dll "$D3D12_DIR"/d3d12core.dll "$work/"
    overrides="$overrides;d3d12,d3d12core=n"
fi

log_step "dx12probe (ICD: ${ICD:-bundled MoltenVK}, d3d12: ${D3D12_DIR:-Wine vkd3d}, dxgi: ${DXGI_DLL:-Wine})"
cd "$work"
[[ -n "${ICD:-}" ]] && export VK_ICD_FILENAMES="$ICD"
WINEDLLOVERRIDES="$overrides" VKD3D_DEBUG="${VKD3D_DEBUG:-warn}" VKD3D_SHADER_DEBUG="${VKD3D_SHADER_DEBUG:-warn}" \
    wine_run dx12probe.exe "${SECONDS_PER_LOOP:-4}" 2> "$work/probe.err"
status=$?
log_info "Wine/vkd3d warnings: $work/probe.err"
exit $status
