# Troubleshooting

Symptoms are listed with their most likely cause and the ordered fix.

## A Unity game process runs but its window is transparent (you see the desktop through it)

**Symptom.** The game appears in `Dock`, `Cmd-Tab` lists it, the macOS
window manager reports a window with the right name, but the window is
visually empty and shows whatever is underneath. `Player.log` stops
around `Input initialized` without a crash; the process sits at
100% CPU (busy rendering -> submitting frames).

**Diagnosis.** Three layered bugs caused this: macdrv window visibility
not being set, a struct ABI drift between DXMT and Metal, and an
OnMainThread re-entrancy issue in the Present path. This was a DXMT
upstream problem; see docs/dxmt-diagnosis.md Phase D for the full
breakdown. If you stopped at `bash install.sh --minimal`, the fork
build was never compiled and the patched DLLs are absent, which
reproduces the symptom.

**Fix.** Build and install the fork:
```bash
bash scripts/07-build-dxmt-fork.sh
```
This compiles and installs the patched `d3d11`, `d3d10core`, `dxgi`,
and `winemetal` DLLs. Fixed in v0.6 of this repo. If you used
`--minimal` mode, re-run the full installer instead:
```bash
bash install.sh
```

## Unity game opens a 4K / Retina-sized window that extends off-screen

**Symptom.** The window registers at `3840x2160` (or the raw pixel
size of a Retina display) even though you passed
`-screen-width 800 -screen-height 600`. Only the top-left patch of
the game is visible.

**Diagnosis.** Wine's `winemac.drv` reports the physical Retina
resolution as the desktop size. Unity 6000 trusts that number at
launch and ignores `-screen-width` / `-screen-height` for the first
run. In v0.6 this is avoided automatically: `launch-steam.sh` starts
Wine under `explorer.exe /desktop=steam-on-m1-wine,<width>x<height>`,
where the dimensions are auto-detected from the macOS Finder desktop
bounds via `osascript`. Physical Retina resolution never reaches the
game.

**Fix.** Since 2026-10-31 the virtual desktop is off by default (it
blocks native macOS fullscreen). If this problem comes back, turn it on:

- Size from the display:
  ```bash
  WINE_VIRTUAL_DESKTOP=auto bash scripts/launch-steam.sh --detach
  ```

- Override the desktop size:
  ```bash
  WINE_VIRTUAL_DESKTOP=1280x800 bash scripts/launch-steam.sh --detach
  ```
- Without the variable (default) Wine creates one macOS window per
  Win32 window.

## Unity 6 game: cursor visible, but mouse clicks do nothing

**Symptom.** Keyboard works, the cursor moves, but the game ignores the
mouse. `Player.log` contains `EnableMouseInPointer failed with the
following error: Call not implemented.` Older Unity games work.

**Cause.** Unity 6 switches to `WM_POINTER*` input and ignores
`WM_MOUSEMOVE`/`WM_LBUTTONDOWN`. Wine before 11.3 stubs
`EnableMouseInPointer` (Wine bug 53847).

**Fix.** Use Gcenx Wine Devel ≥ 11.3 in `~/Applications/Wine Devel.app`
(the scripts prefer it). Then `schwer bash scripts/08-patch-wine-visibility.sh`
(builds winemac.so for `WINE_BUILD_BRANCH`, default wine-11.18) and copy
DXMT's `d3d11/d3d10core/dxgi/winemetal.dll` and `winemetal.so` into the
new Wine tree (`bash scripts/04-install-dxmt.sh` or step 07).

## Game fullscreen covers the notch, no swipe between Spaces

**Symptom.** The game fills the whole screen including the notch area;
there is no red/yellow/green button and no swipe gesture.

**Cause.** The game runs in borderless/exclusive fullscreen, or Steam
runs inside the virtual desktop. winemac only offered native fullscreen
to resizable windows; Unity windowed mode is fixed-size.

**Fix.** Launch without virtual desktop (default), set the game to
windowed mode, then press Ctrl+Cmd+F. Needs the winemac patch from
`patches/wine/` (applied by step 08).

## "Failed to initialize graphics / DirectX 11" at game launch

**Symptom.** The game crashes at startup with a DirectX 11 error, or
`Player.log` shows `D3D11CreateDevice failed`.

**Diagnosis.** DXMT's DLLs (fork build) are not installed, so Wine's
stub `d3d11.dll` handles the `D3D11CreateDevice` call and fails.
Typical for Unity titles shipped as 32-bit Windows binaries. Both
`x86_64-windows` and `i386-windows` variants must be present. This
will happen if you used `bash install.sh --minimal`.

**Fix.** Run the fork build script (installs both architectures):
```bash
bash scripts/07-build-dxmt-fork.sh
```
If you set up with `--minimal`, run the full installer instead:
```bash
bash install.sh
```
This is idempotent and will install `d3d11`, `d3d10core`, `dxgi`, and
`winemetal` for both `x86_64-windows` and `i386-windows`.

## Game hangs at `GfxDevice: creating device client` with CPU at 100%

**Cause.** Steam's `GameOverlayRenderer.dll` was injected into the
child game process and hooked `D3D11CreateDeviceAndSwapChain`.
Unchecking the in-game overlay box in Steam's game properties is
**not** sufficient — the DLL is still mapped.

**Fix.** Ensure `launch-steam.sh` exports
`gameoverlayrenderer,gameoverlayrenderer64=d` inside
`WINEDLLOVERRIDES`. This makes Wine refuse to load the overlay DLL
at all, regardless of Steam's own setting.

## The Wine virtual desktop window freezes the entire Wine session

**Symptom.** The virtual desktop container becomes unresponsive and
Steam, games, and all Wine windows stop reacting to input.

**Diagnosis.** All Wine windows share a single container process via
`explorer.exe /desktop=...`. When one Windows program inside it hangs,
the whole container blocks. In v0.6 the virtual desktop is the default
and recommended mode because it suppresses unwanted fullscreen
transitions. Disabling it is the workaround only if a specific program
reliably hangs the container.

**Fix (disable virtual desktop).**
1. Kill everything:
   ```bash
   pkill -9 -i -f 'steam\.exe|steamwebhelper|wineserver|wine64-preloader|explorer\.exe'
   ```
2. Launch with virtual desktop disabled:
   ```bash
   WINE_VIRTUAL_DESKTOP="" bash scripts/launch-steam.sh --detach
   ```
   Each Wine program now gets its own independent macOS window; one
   hang can no longer take the others down. Be aware that without the
   virtual desktop, fullscreen suppression is inactive and some games
   may attempt a true fullscreen takeover.

## Steam launches but stays stuck with a black window

**Symptom.** The Steam window appears but shows a solid black client
area. The process is alive and responding to `Cmd-Q`.

**Diagnosis.** The CEF 126 / ANGLE renderer requires
`--disable-gpu --single-process` to be passed to `steamwebhelper.exe`.
If the wrapper was built from an older source that only passes
`--in-process-gpu`, the GPU process still spawns out-of-process and
CEF's ANGLE back-end produces a black surface. Note: passing
`--enable-features=NetworkServiceInProcess` instead of
`--single-process` was tested and does not resolve this in CEF 126.

**Fix.**
1. Confirm the wrapper's `EXTRA_FLAGS` includes `--disable-gpu` and
   `--single-process`. The `launch-steam.sh` wrapper integrity check
   will warn if the installed binary does not match the expected hash.
2. Re-run the wrapper installer to ensure the correct build is in
   place:
   ```bash
   bash scripts/06-install-wrapper.sh
   ```
3. Verify `WINEDLLOVERRIDES` actually reaches Steam:
   ```bash
   env | grep WINEDLLOVERRIDES
   ```
4. Confirm the wrapper is in place:
   ```bash
   ls -la "$HOME/.wine-steam/drive_c/Program Files (x86)/Steam/bin/cef/"cef.win*"/steamwebhelper"*
   ```
   You should see `steamwebhelper.exe` (~80 KB, ours) and
   `steamwebhelper_real.exe` (several MB, Valve's).

## Dock shows a Steam icon but no window appears

**Cause.** `-silent` mode. Triggered when a stale Chromium
`SingletonLock` file remains after a previous crash.

**Fix.**
- `scripts/launch-steam.sh` deletes these locks at the start of every
  launch. If you invoked Steam some other way, kill everything and run
  `launch-steam.sh` again:
  ```bash
  pkill -9 -i -f 'steam\.exe|steamwebhelper|wineserver'
  scripts/launch-steam.sh
  ```

## `wine` immediately exits with status 137

**Cause.** macOS Gatekeeper killed an unsigned / ad-hoc-signed binary.

**Fix.** Re-run `scripts/01-install-wine.sh`; it strips the
`com.apple.quarantine` xattr. You can also do it manually:
```bash
xattr -dr com.apple.quarantine "/Applications/Wine Stable.app"
```

## `wineboot` says "version mismatch 931/930"

**Cause.** A wineserver from a previous Wine version is still running.

**Fix.**
```bash
pkill -9 -f wineserver
pkill -9 -f wine64-preloader
scripts/launch-steam.sh
```

## SSL handshake failures in `cef_log.txt` (`net_error -100`, `-107`)

**Observed.** `ssl_client_socket_impl.cc: handshake failed;
returned -1, SSL error code 1, net_error -100`.

**Likely cause.** Chromium's BoringSSL runs fine in principle on Wine,
but its non-blocking socket assumptions collide with Wine's winsock on
macOS when the GPU process is alive and out-of-process. In v0.6 the
wrapper passes `--disable-gpu --single-process`, which keeps everything
in a single process and eliminates these errors. Note:
`--enable-features=NetworkServiceInProcess` was tested as an
alternative in CEF 126 and does not produce the same effect; the
`--single-process` flag is the one that matters.

If the errors persist after installing the correct wrapper:
1. Confirm the wrapper binary is passing `--single-process`:
   ```bash
   STEAMWEBHELPER_WRAPPER_DEBUG=1 scripts/launch-steam.sh
   # then inspect wrapper-debug.log for the argument list
   ```
2. Confirm the host machine can reach Steam with TLS:
   ```bash
   curl -I https://client-update.steamstatic.com/
   ```
   A 200 response means the outer network is fine.
3. Ensure the CA bundle was copied into the prefix:
   ```bash
   ls -l "$HOME/.wine-steam/drive_c/windows/cacert.pem"
   ```

## Window cannot be dragged from the macOS side

**Symptom.** The Wine virtual desktop window has no visible title bar
and cannot be repositioned by dragging with the mouse from macOS.
Mission Control shows the window but dragging it does nothing.

**Diagnosis.** Wine's virtual desktop is created with `WS_POPUP` style,
which maps to `NSWindowStyleMaskBorderless` on the macOS side. This is
intentional Wine behaviour. Setting `AllowImmovableWindows=n` in the
Wine config does not add a title bar.

**Fix.** This is a known limitation of virtual desktop mode. Recommended
workaround: launch with the full screen dimensions so repositioning is
unnecessary. Use standard macOS window-management shortcuts to switch
context:
- `Cmd+Tab` — switch to another app
- `Cmd+H` — hide the Wine session
- `Ctrl+Left/Right` — move to another Space in Mission Control

## Game goes fullscreen inside the virtual desktop

**Symptom.** Inside the virtual desktop the game expands to fill the
entire container, UI elements stretch to the edges, and there is no
windowed-mode border.

**Diagnosis.** Unity's `Screenmanager Fullscreen mode` registry key is
set to `FullScreenWindow = 1`, or a leftover compat flag
`DISABLEDXMAXIMIZEDWINDOWEDMODE` is forcing exclusive-fullscreen
behaviour.

**Fix.**
1. Add to the game's Steam Launch Options:
   ```
   -force-d3d11-no-singlethreaded -screen-fullscreen 0
   ```
2. `launch-steam.sh` automatically scrubs compat flags on every launch,
   so Wine-side influence is already removed.
3. If the problem persists, delete the stale registry key for a clean
   state:
   ```bash
   wine reg delete 'HKCU\Software\<Vendor>\<Title>' \
       /v 'Screenmanager Fullscreen mode_h3630240806' /f
   ```
   Then relaunch; Unity will write a fresh value based on the launch
   options above.

## "Updating Steam" dialog loops forever

**Cause.** `Package/` directory corruption after an interrupted update.

**Fix.**
```bash
rm -rf "$HOME/.wine-steam/drive_c/Program Files (x86)/Steam/package/"
scripts/launch-steam.sh
```

## After a Homebrew upgrade, DXMT or wrapper seems to vanish

**Cause.** `brew upgrade --cask wine-stable` replaces the entire
`Wine Stable.app` bundle, which is where DXMT's DLLs live.

**Fix.** Re-run the full installer (idempotent; re-places any files
clobbered by the Wine upgrade):
```bash
bash install.sh
```
This re-runs quarantine stripping, reinstalls the fork-build DXMT
DLLs for both architectures, and reinstalls the wrapper.

## Both `steamwebhelper.exe` AND `steamwebhelper_real.exe` have the same size

**Cause.** An old version of `scripts/06-install-wrapper.sh` (before
the MD5-based check) could copy the wrapper into both slots, erasing
Valve's original binary.

**Symptom.** `06-install-wrapper.sh` refuses to continue and tells you:

```
cef.winNN: BOTH steamwebhelper.exe AND steamwebhelper_real.exe
are wrapper-sized. Valve's original is gone.
Re-run scripts/03-install-steam.sh to recover.
```

**Fix.**
1. Quit Steam completely, then:
   ```bash
   pkill -9 -i -f 'steam\.exe|steamwebhelper|wineserver'
   ```
2. Remove the corrupted helper files from the affected directory:
   ```bash
   STEAM="$HOME/.wine-steam/drive_c/Program Files (x86)/Steam"
   rm -f "$STEAM/bin/cef/cef.winNN/steamwebhelper.exe"
   rm -f "$STEAM/bin/cef/cef.winNN/steamwebhelper_real.exe"
   ```
3. Launch Steam once **without** `-noverifyfiles` so its bootstrap
   re-extracts `bins_cef_winNN.zip.vz` from `Steam/package/`:
   ```bash
   "$HOME/.wine-steam/drive_c/Program Files (x86)/Steam/steam.exe"  # via Wine
   ```
4. Once the bootstrap dialog reports "Verification complete", quit
   Steam and rerun `scripts/06-install-wrapper.sh`.

## Still blank: enable wrapper debug log

Set `STEAMWEBHELPER_WRAPPER_DEBUG=1` in the environment Wine sees. The
wrapper writes `wrapper-debug.log` next to itself, so you can confirm
it was invoked and with what arguments:

```bash
WRAPPER_DEBUG=1 scripts/launch-steam.sh
ls -la "$HOME/.wine-steam/drive_c/Program Files (x86)/Steam/bin/cef/"cef.win*"/wrapper-debug.log"
```

(Exact plumbing of the env var through Steam's child processes depends
on Wine's environment handling. Expect to tweak `launch-steam.sh` if
you need this.)

## Install fails on newer toolchains (macOS 26, Xcode 27, Homebrew 2026, GCC 16)

Tested on Apple Silicon with macOS 26, Xcode 27, Homebrew 7 and mingw-w64
GCC 16. The installer handles the build errors below automatically; they are
listed so the error messages are searchable. The last two rows are runtime
symptoms and not (fully) fixed.

| Symptom | Cause | Handling |
|---|---|---|
| `brew install --cask wine-stable` fails / cask disabled | Homebrew disabled the cask (Gatekeeper) on 2026-09-01 | `01-install-wine.sh` falls back to the Gcenx 11.0_1 tarball in `~/Applications` (SHA256-verified) when the cask is disabled or its install fails |
| `KeyError: ... build.cpp_importstd` / `nonexistant project parent option build.cpp_importstd` during `meson setup` | Bug in meson 1.10.0/1.10.1; 1.11+ is incompatible with DXMT's `meson.build` | `07` uses the system meson only if it is 1.10.2+ within 1.10.x, otherwise pins `meson==1.10.2` in a venv (`MESON_VENV`). A half-finished `build/` is redone |
| `pip install --user meson` fails with `externally-managed-environment` | Homebrew Python is PEP 668 managed | Same venv pin, no `--user` install |
| `winebuild not found at .../toolchains/wine/bin/winebuild` | The 3Shain toolchain tarball has no top-level directory and spilled into `toolchains/` | `07` extracts into a scratch dir and moves that into place. Delete a broken `toolchains/` and re-run |
| `Undefined symbols ... _ZSTD_compress` linking `airconv` / `winemetal.so` | LLVM's cmake picked up Homebrew's arm64-only zstd | `07` builds LLVM with `-DLLVM_ENABLE_ZSTD=OFF` (delete `toolchains/llvm` to rebuild) |
| `'setfill' is not a member of 'std'` in `com_guid.cpp` | GCC 16 no longer includes `<iomanip>` transitively | `patches/dxmt/*.patch` adds the include; `07` applies it unless already applied |
| Metal compile error in `air_tessellation.metal` (`__metal_atomic_fetch_add_explicit` signature) | The Xcode 27 Metal compiler changed the internal builtin | Same `patches/dxmt` patch uses `atomic_fetch_add_explicit` instead |
| `redefinition of '_OSSwapInt16'` in `tools/makedep.c` | Xcode clang targets arm64 even under `arch -x86_64` | `08` passes `-arch x86_64` explicitly to the Wine configure compilers |
| `Your bison version is too old` during Wine configure | macOS ships bison 2.3; Homebrew's bison/flex are keg-only and not on `PATH` | `08` puts the keg-only `bison`/`flex` `bin` directories on `PATH` |
| `FreeType 64-bit development files not found` | Homebrew's freetype is arm64 and cannot link into the x86_64 Wine build | `08` configures Wine with `--without-freetype` |
| `06-install-wrapper.sh` can't find `bin/cef` on a fresh install | `SteamSetup` only installs the bootstrapper; CEF arrives on first launch | `06` now warns and exits 0; `launch-steam.sh` redeploys the wrapper on every launch |
| DXMT log: `Failed to create metal view ... no exported symbols` | The patched `winemac.drv` from step 08 is not installed, so the window has no visible Metal view | Run `bash scripts/08-patch-wine-visibility.sh` (or the full `install.sh`) |
| Menu runs at 30 FPS with the 60 FPS cap, gameplay at 60 | Upstream DXMT divided the game's `SyncInterval` by the cap instead of the refresh rate | `patches/dxmt/0002` (applied by `07`); re-run `07` and reinstall the DLLs |
| `wine: could not load ntdll.so` after replacing `bin/wine` | Gcenx's `bin/wine` searches other paths than an upstream-built loader; games do not run as `bin/wine` anyway | Restore `bin/wine.gcenx-backup`. For Game Mode use `11-mark-wine-as-game.sh` (bundle + trampoline for `lib/wine/x86_64-unix/wine`) |
| `bench-game.sh`: game exits during warm-up | The Steam API restarts the game through Steam (with Steam's environment) when it was not launched by Steam | The script creates a temporary `steam_appid.txt`; check for a left-over one only if the script was killed |
| `wine trampoline: exec Wine Game.app` error / Game Mode gone after a Wine update | The update replaced or removed the loader bundle | Re-run `bash scripts/11-mark-wine-as-game.sh` |
| Vulkan/D3D12 finds no driver (`vkCreateInstance` without extensions, `D3D12CreateDevice` fails) since `11-mark-wine-as-game.sh` | The loader runs from `Wine Game.app`, so the Vulkan loader looks for ICDs in that bundle's `Resources/vulkan/icd.d` | `11` now writes a MoltenVK ICD manifest into the bundle; re-run `bash scripts/11-mark-wine-as-game.sh` |
| 32-bit game crashes under WoW64 with `Exception frame is not in stack limits` | Known Wine WoW64 limitation with 32-bit processes | Not fixed; use a 64-bit build of the game if it offers one |
| Game says "Unable to initialize game ... apply windows updates" (Space Engineers) | Wine's d3dcompiler_47 can't compile the game's HLSL, or DXMT can't emulate geometry-shader stream output | Native `d3dcompiler_47.dll` next to the exe with `d3dcompiler_47=n`; `patches/dxmt/0003`. See [space-engineers.md](space-engineers.md) |
| XAudio2 `CreateSourceVoice` fails (`0xFFFFFFFE`), crash when music starts; Media Foundation videos fail | xWMA/video decoding needs GStreamer, which is not installed | Native `xaudio2_9.dll` next to the exe with `xaudio2_9=n` (winetricks `xaudio29` in a test prefix) |
