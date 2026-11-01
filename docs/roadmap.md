# Roadmap (idea stage, 2026-11-01)

Goal: grow this repo step by step into a place where Windows games on Apple
Silicon actually work, optimised game by game, and make it public.

## Direction

Not a CrossOver clone. CrossOver's DX11/DX12 path is Apple's D3DMetal
(closed, not redistributable), and wrappers around Wine already exist
(Whisky, discontinued 2025; Sikarugir, Porting Kit, Heroic). What is
missing is the per-game knowledge, like ProtonDB for Linux:

1. **Game profiles**: `games/<appid>-<name>.conf` with DLL overrides, DLLs to
   fetch via winetricks (never commit Microsoft DLLs), launch options, known
   issues, measurements. `scripts/setup-game.sh <appid>` applies one. First
   examples: Space Engineers 1, Moon Sweepers.
2. **Compatibility table**: game, status, FPS, Mac model, date; contributions
   via PR.
3. **Upstream the fixes**: DXMT `patches/dxmt/0002` (cap bug, stats) and
   `0003` (GS stream-output fallback) as PRs to 3Shain/dxmt; winemac
   fullscreen patch to Wine; x86_64 KosmicKrisp build to Mesa.
4. A small launcher app only once there are users.

## Open-source layers to contribute to

| Project | Role | Where we can help |
|---|---|---|
| DXMT (MIT) | D3D11 → Metal, main path | our patches; real GS stream output |
| Wine | base | winemac, XAudio2/xWMA without GStreamer |
| KosmicKrisp (Mesa) | Vulkan 1.4 on Metal | x86_64 build, sparse, denorm control |
| vkd3d-proton | D3D12 → Vulkan | Metal patch (upstream unlikely) |
| DXVK, FAudio | dxgi for D3D12, audio | small patches |

Closed: D3DMetal, Rosetta, Apple's GPU drivers. Asahi Linux is M1/M2 only.

## CrossOver comparison (planned)

D3DMetal also translates; Rosetta costs the same in both. CrossOver leads
on D3D12 and per-game fixes. Plan: 14-day trial, same scene and
`bench-game.sh` metrics for Moon Sweepers and SE1, results into
`performance.md`. Needs Felix to register and ~42 GB for SE1 in a bottle.

## Publishing

r/macgaming, AppleGamingWiki, later Show HN once 2–3 games run cleanly (SE1
with numbers as the hook). Donations via `.github/FUNDING.yml` (GitHub
Sponsors and/or Buy Me a Coffee).

## Open decisions (Felix)

1. Own repo with a new name (recommended; credits notpop) or keep the fork.
2. Name.
3. Buy Me a Coffee, GitHub Sponsors or both (accounts by Felix).
4. Order: game profiles first, then DXMT PRs?
5. Post under `2000game`? Drafts by Claude, posting by Felix.
