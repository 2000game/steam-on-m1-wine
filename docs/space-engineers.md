# Space Engineers 1 (D3D11)

Status 2026-11-01: SE1 (appid 244850) starts, creates its D3D11 device and
runs its main menu stable at 120 FPS. Loading a world was not tested: that
needs input in the game and the account's saves (not touched on purpose).

Machine: M4 (10-core GPU), 24 GB, macOS 26.6, Wine Devel 11.18, DXMT fork
with `patches/dxmt/0001`–`0003`. The game runs on Wine Mono (6.13), no
.NET install needed.

## What it needs

Three things, all in the game folder (`steamapps/common/SpaceEngineers/Bin64`)
plus DLL overrides. Without each one the game shows "Unable to initialize
game ... Please apply windows updates":

| Problem | Cause | Fix |
|---|---|---|
| Shader compile fails, precompiled shader cache misses | Wine's builtin `d3dcompiler_47` can't compile SE's HLSL (`#error C++ compilation required` in `Content/Shaders/D3DX_DXGIFormatConvert.inl`) | Microsoft's `d3dcompiler_47.dll` in `Bin64`, `d3dcompiler_47=n` |
| Device creation fails with `E_FAIL` | `Foliage/FoliageStreaming.hlsl` is a gs_5_0 with stream output; DXMT only emulates stream output for vertex shaders | `patches/dxmt/0003`: fall back to a plain geometry shader (the foliage stream output is lost) |
| Crash ~12 s into the menu, `SharpDXException 0xFFFFFFFE` in `XAudio2.CreateSourceVoice` (`MyXAudio2.PlayMusicByTransition`) | The music is xWMA (350 `.xwm` files). Wine's XAudio2 (FAudio) decodes xWMA through winegstreamer, and the GStreamer framework is not installed (`winegstreamer.so` links `@rpath/libgstreamer-1.0`) | Microsoft's `xaudio2_9.dll` in `Bin64`, `xaudio2_9=n`. It decodes xWMA itself. Also fixed the menu video error (`Unable to Load or Play the video file ... 0xd0000005`) |

Both DLLs came from winetricks in the test prefix, so `~/.wine-steam`
stays unchanged:

```bash
WINEPREFIX=~/.wine-dx12test winetricks -q d3dcompiler_47 xaudio29
cp ~/.wine-dx12test/drive_c/windows/system32/{d3dcompiler_47,xaudio2_9}.dll \
   "$HOME/.wine-steam/drive_c/Program Files (x86)/Steam/steamapps/common/SpaceEngineers/Bin64/"
```

Steam Launch Options for a normal start from the library:

```
WINEDLLOVERRIDES="d3dcompiler_47,xaudio2_9=n" %command% -skipintro
```

(Not tested from the library yet; the bench starts the exe directly.)
Installing GStreamer instead would be the general fix for Wine's XAudio2,
but the Homebrew cask `gstreamer-runtime` is disabled (fails Gatekeeper)
and the official pkg is a system install under `/Library/Frameworks`.

## Install without the Steam UI

Steam installs a game when an `appmanifest_<appid>.acf` with
`StateFlags 1026` sits in `steamapps/` and Steam is restarted. SE1 is
about 42 GB.

## Benchmark

```bash
B="$HOME/.wine-steam/drive_c/Program Files (x86)/Steam/steamapps/common/SpaceEngineers/Bin64"
EXTRA_OVERRIDES="d3dcompiler_47=n;xaudio2_9=n" GAME_EXE="$B/SpaceEngineers.exe" \
GAME_APPID=244850 GAME_ARGS="-skipintro" \
GAME_SAVE_DIR="$HOME/.wine-steam/drive_c/users/$USER/AppData/Roaming/SpaceEngineers" \
GAME_REG_KEY='HKCU\Software\KeenSoftwareHouse' WARMUP=150 DURATION=30 \
    bash scripts/bench-game.sh se1
```

The save folder in the prefix was empty: cloud saves only sync when Steam
starts the game. The game writes its logs and `SpaceEngineers.cfg` there,
so the bench restores the (empty) snapshot after each run and keeps the
files in `<run>/save-after/`.

| Date | Scene | FPS | p95 frame | Game %CPU | Energy | WindowServer %CPU |
|---|---|---|---|---|---|---|
| 2026-11-01 | main menu, window, default settings | 120 | 9.4 ms | 53 | 63 | 9 |

The frame cap from `DXMT_CONFIG=d3d11.preferredMaxFrameRate=60` did not
apply: the menu ran at 120. Not yet looked into.

## Open points

1. A world (load or new) and the in-game FPS. Needs a test world that is not Felix's save.
2. Start from the Steam library with the launch options above (cloud saves then sync; check what that does to the snapshot logic).
3. Why the 60 FPS cap does not apply.
4. Foliage: with stream output dropped, grass may be missing or wrong.
