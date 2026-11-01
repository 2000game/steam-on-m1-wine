# Performance and efficiency

Measurements and levers for CPU load, heat and fan noise. Test machine:
Apple M4, 24 GB, macOS 26.6, built-in display 120 Hz.

## Where the cost comes from

| Layer | What it does | Cost |
|---|---|---|
| Wine | Translates Win32 API calls to macOS. Game code runs directly, no emulation. | small |
| Rosetta 2 | Translates the game's x86_64 code to ARM64. | largest share of CPU overhead |
| DXMT | Translates D3D11 to Metal. Shader compilation on first use causes short stutters. | moderate |
| Steam client | Runs alongside; UI is CPU-rastered (`--disable-gpu`). | background CPU while its window is open |

Every rendered frame pays for all layers, so the frame rate is the biggest
lever. Unity games with vSync and no frame limit render at the panel's
refresh rate (120 Hz on this Mac).

`sample <pid>` is of little use: almost all frames show up as `???` in
Rosetta JIT or runtime routines, without symbols.

## Levers

| Lever | How | Status |
|---|---|---|
| Frame cap | `DXMT_CONFIG="d3d11.preferredMaxFrameRate=60"`, default in `launch-steam.sh` since 2026-10-31. `=0` disables it. | measured, see below |
| MetalFX upscaling | `DXMT_METALFX_SPATIAL_SWAPCHAIN=1`; the game must render below window size (lower resolution or render scale). | not tested |
| Metal HUD | `MTL_HUD_ENABLED=1 bash scripts/launch-steam.sh --detach` shows FPS and GPU time in game. | works as env pass-through |
| Steam window | Close or minimise Steam while playing. | not measured |
| Frame latency | `dxgi.maxFrameLatency=N` in `DXMT_CONFIG` (patch `patches/dxmt/0002`). Fixes how many frames the CPU may run ahead of the GPU; the game's own request is ignored. | measured in menu: no gain, 1 costs CPU |
| Game Mode | `scripts/11-mark-wine-as-game.sh` runs every Windows process from `x86_64-unix/Wine Game.app` (games category). Needs native fullscreen (Ctrl+Cmd+F). | works, verified 2026-10-31 (HUD "Game Mode On", rocket icon in menu bar) |

DXMT reads `DXMT_CONFIG` (entries separated by `;`) or a `dxmt.conf` file
(`DXMT_CONFIG_FILE`). The cap sets the Metal present interval, so the CPU
waits instead of preparing more frames.

**Cap bug (fixed 2026-10-31, `patches/dxmt/0002`):** upstream DXMT computed
the minimum frame time as `SyncInterval / cap`. Moon Sweepers' menu presents
with `SyncInterval=2` (every second refresh = 60 FPS at 120 Hz), so cap 60
gave 33 ms = 30 FPS. Now `max(SyncInterval / refresh rate, 1 / cap)`: the
cap is only a lower bound. Gameplay uses `SyncInterval=1`, so it was not
affected; the menu now runs at 60 like without cap.

## Measurements

`top -l 4 -s 3 -pid <pid> -stats pid,cpu,power`, game in the same scene,
Steam window open.

| Date | Game | Setup | Game %CPU | Energy impact | WindowServer %CPU |
|---|---|---|---|---|---|
| 2026-10-31 | Moon Sweepers | Wine 11.18, no cap, vSync on, native fullscreen | ~87 | ~89 | ~20 |
| 2026-10-31 | Moon Sweepers | same, cap 60 (upgrade menu, 3D scene behind) | ~77 | ~79 | ~22 |
| 2026-10-31 | Moon Sweepers | same, cap 30 (gameplay, outdoors) | ~44 | ~45 | ~11 |
| 2026-10-31 | Moon Sweepers | cap 60, gameplay outdoors, default graphics (preset 2, shadows 3) | ~80 | ~81 | ~21 |
| 2026-10-31 | Moon Sweepers | cap 60, gameplay outdoors, graphics lowered in game | ~71 | ~72 | ~26 |
| 2026-11-01 | Space Engineers 1 (M4) | main menu, window, 120 FPS (cap not applied), see [space-engineers.md](space-engineers.md) | 53 | 63 | 9 |

Metal HUD at cap 60 (1512x949, native fullscreen below the notch): FPS 59.75,
GPU 13.8 ms per frame, presentation "Composited", Game Mode off. The GPU
already needs most of the 16.7 ms budget, so the uncapped run cannot have
been much above 60-70 FPS. That explains why the cap saves only ~10 % CPU.

Cap 30 is only a diagnostic; it plays badly. In gameplay the HUD shows
GPU 16.3 ms per frame, so at 60 FPS the GPU is close to full load there.

**Finding:** CPU load scales with the frame rate (60 → 30 FPS roughly
halves game CPU and WindowServer). There is no large fixed overhead such
as Wine threads spinning; the cost is per frame (Rosetta-translated game
code + D3D11→Metal translation + compositing). So the levers are fewer
frames and cheaper frames, not Wine tuning.

The HUD shows "Composited" presentation: WindowServer composites every
frame (~20 % CPU at 60 FPS). The HUD overlay itself may force this; not
checked without HUD.

Lower in-game graphics at cap 60 saved ~10 % game CPU. `top`'s energy
impact is mostly CPU, so GPU savings (less heat, less fan) do not show
up there; compare the HUD's GPU ms instead.

Metal HUD with lowered graphics, cap 60, gameplay: FPS 47.4, GPU 10.4 ms,
frame interval 21.1 ms (graph shows a plateau of slow frames, then back to
target). Per-thread CPU over 5 s at that time (`ps -M` UTIME+STIME delta):
89 threads, 54 % total, hottest thread 19 %. So neither the GPU
(10.4 of 16.7 ms) nor a single CPU thread was saturated while below 60 FPS.

**Open lead:** CPU and GPU may be waiting on each other instead of
overlapping (frame latency / present pacing). DXMT honours the game's
`IDXGISwapChain2::SetMaximumFrameLatency`; patch 0002 adds
`dxgi.maxFrameLatency` to override it (menu results below; gameplay still
untested). Game Mode: see the Wine source analysis.

## Benchmark tool (`scripts/bench-game.sh`)

Measures without anyone at the Mac. Steam must be running.

    DXMT_CONFIG="d3d11.preferredMaxFrameRate=60;dxgi.maxFrameLatency=1" \
        bash scripts/bench-game.sh lat1

- Starts the game directly with the script's environment (so DXMT settings
  change per run), sends **no input**, only sits in the main menu, then
  kills it.
- A temporary `steam_appid.txt` next to the exe stops the Steam API from
  exiting and having Steam relaunch the game with Steam's environment.
  Removed after the run.
- Save protection: snapshots the save folder and the game's registry key,
  compares hashes afterwards and restores if anything changed. Moon Sweepers
  rewrites `session.last`/`session.running` on every start, so every run
  ends with "restoring snapshot"; that is expected.
- Frame data comes from `DXMT_STATS_FILE` (patch 0002): every 2 s FPS,
  frame-time percentiles, PresentBoundary wait (latency fence) and DXMT's
  encode/drawable/commit times. CPU and energy from `top`.
- Results: `~/.cache/steam-on-m1-wine/bench/results.tsv`, one row per run
  (medians), raw files per run in the folder next to it.
- Limits: the window is not frontmost (macOS 26 does not let a background
  process activate an app) and stays windowed, so it cannot measure
  fullscreen, Game Mode or gameplay. The menu renders a 3D scene, so DXMT
  and Rosetta costs are real, but it is lighter than gameplay.

### Results: main menu, windowed, 120 Hz (2026-10-31)

Five runs, 30 s warm-up, 30 s measured, medians of 2 s windows.

| Config | FPS | p95 ms | p99 ms | Game %CPU | WindowServer %CPU |
|---|---|---|---|---|---|
| no cap | 60.0 | 17.1 | 17.4–18.1 | 46–48 | 46 |
| cap 60 (fixed) | 60.0 | 17.1 | 17.3–17.5 | 47–48 | 46–47 |
| cap 60, latency 1 | 60.0 | 17.1 | 17.6 | 53 | 45 |
| cap 60, latency 2 | 60.0 | 17.0 | 17.3 | 48 | 46 |
| cap 60, old formula (smoke run) | 30.0 | | | 33 | 27 |
| cap 60, `-screen-fullscreen 1 -window-mode borderless` | 59.9 | 17.1 | 17.3 | 56 | 44 |

- The menu is paced by `SyncInterval=2`; the cap does not change anything
  there any more. Frame pacing is very even (p99 < 18 ms).
- Latency 1 costs ~5 % CPU and gains nothing (the render thread then waits
  15 ms per frame in PresentBoundary instead of on the drawable). Default 3
  stays; latency is only worth testing again in gameplay where the GPU is
  near its limit.
- WindowServer: 9 % without the game, 46 % with it in a window. ~37 % CPU
  only for compositing the game's layer at 60 FPS. In native fullscreen it
  was ~20 % (manual measurement above). Fullscreen is therefore also the
  cheaper mode.
- Borderless runs at native resolution, so its game CPU is not comparable.

## Source analysis: DXMT (fork in ~/dev/dxmt, 2026-10-31)

Static read, line numbers approximate.

- Command encoding is already asynchronous (`ASYNC_ENCODING 1`,
  `dxmt_command_queue.cpp:8`): separate encode and finish threads that sleep
  on `std::atomic::wait`, no busy-spinning.
- The real CPU/GPU throttle is `CommandQueue::PresentBoundary()`
  (`dxmt_command_queue.hpp:240-252`): after each Present the game's render
  thread waits until frame N-`max_latency_` has finished on the GPU.
  `max_latency_` defaults to 3 and only changes via the game's
  `IDXGIDevice1::SetMaximumFrameLatency` (`d3d11_device.cpp:1246`). The
  fence is signalled by the finish thread after `waitUntilCompleted()`.
  There is no config override.
- The frame cap uses `presentDrawableAfterMinimumDuration` (`dxmt_context.cpp:1032`);
  OS-scheduled presents plus 3 drawables can quantise frame times (21 ms
  observed at a 16.7 ms cap).
- CAMetalLayer (`dxmt_presenter.cpp:18-20`): opaque, displaySync off,
  framebufferOnly off (comment says on was slower), default 3 drawables.
  `nextDrawable` runs on the encode thread.
- `WMTQueryDisplaySettingForLayer` runs on every Present and touches
  NSView/NSWindow/NSScreen (small cost).
- Only spin loop: `d3d11_device_mutex::lock` (`d3d11_multithread.cpp:18`)
  with `_mm_pause`, which is close to a no-op under Rosetta; matters only if
  the game enables D3D11 multithread protection and contends.
- "Composited" most likely comes from Wine's view hierarchy (Metal layer
  is a sublayer of a Wine NSView), not from DXMT.

Ranked experiments (gain/effort):
1. Config option `dxgi.maxFrameLatency` → `max_latency_`; first log what
   Unity requests, then test 1–4 with the HUD. 15 min.
2. Optional CPU-side frame limiter instead of `presentDrawableAfterMinimumDuration`
   for smoother pacing. 1–2 h.
3. Read DXMT's own stats `drawable_blocking_interval` / `present_lantency_interval`
   to tell compositor waits from latency-fence waits. Free.
4. Cache `WMTQueryDisplaySettingForLayer` per window event. Small gain.
5. Composited path: try display colourspace, framebufferOnly=true. Uncertain.
6. Replace the device mutex spin with spin-then-yield. Only if contended.

## Source analysis: Wine 11.18 (2026-10-31)

Static read of ~/dev/wine-build/wine-11.18.

- **Sync:** no fast path on macOS. esync/fsync/msync do not exist upstream,
  `WINEESYNC`/`WINEMSYNC` are ignored, ntsync is Linux-only. Only critical
  sections, SRW locks and WaitOnAddress use `os_sync_wait_on_address`/ulock
  (`dlls/ntdll/unix/sync.c:144-200`); every event/semaphore/mutex wait is a
  wineserver round trip (`sync.c:2912`). A macOS msync would be a large
  rewrite. wineserver used only a few % CPU, so likely not the bottleneck.
- **Game Mode:** ntdll re-executes every Windows process as
  `lib/wine/x86_64-unix/wine` (`dlls/ntdll/unix/loader.c:400`); `bin/wine`
  only starts it. That loader's Info.plist is embedded at link time
  (`loader/wine_info.plist.in`, `-sectcreate __TEXT __info_plist`, 938 bytes
  in Gcenx 11.18) and has `LSUIElement=1` and no games category.
  - First attempt (failed): rebuilding `bin/wine` from upstream source. Gcenx's
    `bin/wine` is a different loader (looks for `%s/wine%s/ntdll.so`, no plist
    section at all), the upstream one only finds `ntdll.so` in its own
    folder: "could not load ntdll.so". And it was the wrong binary anyway.
  - Second attempt (no effect): add the category to the embedded plist in
    place and re-sign. Wine ran fine, but Game Mode stayed off: macOS
    registers an unbundled executable without bundle ID or category
    (`lsappinfo` shows none), the embedded plist is not used for this.
  - Working: a real bundle. `scripts/11-mark-wine-as-game.sh` moves the
    original loader to `x86_64-unix/Wine Game.app/Contents/MacOS/wine`
    (Info.plist with `LSApplicationCategoryType=public.app-category.games`,
    `ntdll.so` symlinked next to it because the loader looks in its own
    folder) and puts a small trampoline at the old path that `execv`s the
    bundled loader. `lsappinfo` then shows `org.winehq.wine.game`, and in
    native fullscreen the HUD says "Game Mode On". A symlink instead of the
    trampoline does not work: macOS takes the bundle from the exec path,
    and through a symlink that path is outside the bundle.
  - Side effect: all Wine processes (also Steam) appear as "Wine Game" in
    the menu bar; Game Mode only starts for the frontmost fullscreen one.
- **Composited:** Wine windows start non-opaque with a clear background
  (`dlls/winemac.drv/cocoa_window.m:950-951`) and become opaque only in
  `checkTransparency` (`:1962-1978`). Check `[window isOpaque]` in
  fullscreen. A fullscreen Space may stay composited by design.
- **GetCursorPos:** if the cached position is older than 100 ms
  (`win32u/input.c:860`), winemac does a synchronous hop to the main thread
  per call (`mouse.c:667`, `cocoa_app.m:2538`). Small gain from caching.
- Message pump, WM_POINTER, cursor clipping: no polling found.
- Timers: `NtSetTimerResolution` is a stub; Sleep goes through wineserver
  timers, macOS timer coalescing may add jitter to frame pacing. Hard to fix.
