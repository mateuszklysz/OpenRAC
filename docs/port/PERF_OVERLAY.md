# The performance overlay

The native port can show, over the game, where each tick's time goes and what
holds the game back. It is a developer feature: the launcher turns it on, the
player's build never shows it unasked.

## Turning it on

- **From the launcher:** Settings, *Developer tools* on, then *Performance
  overlay* on. Every Play action then starts the game with `OPENRAC_PERF=1`
  (`launcher/core/src/actions.rs`, `plan`).
- **From a shell:** `OPENRAC_PERF=1` or `--perf`.
- **In the game:** F3 cycles the overlay full, off and compact while it is on.
- `OPENRAC_PERF_CSV=FILE` also writes each tick as a line of FILE (the stages
  in milliseconds, the counts, the renderers' GPU time), for comparing two
  builds. It needs `OPENRAC_PERF` too.

Off, the port reads no clock for it and makes no GPU query: each marked stage
costs one test of a flag.

## What a tick is

The game's frame is its tick: it runs its code, then waits for a vertical
blank (`openrac_game_vsync`, `port/game/common/main.cpp`), 50 times a second in
PAL. The port does its work at that wait. The overlay's tick runs from one wait
to the next and is cut into stages, each charged only for the time spent in it
and not in a stage nested inside it, so a tick's stages add up to its length:

| Stage | What runs | Where it is marked |
|---|---|---|
| game code | the decompiled C, translated by hostgen: logic, AI, animation, the display lists; disc reads and sound commands too | everything outside the others |
| port, other | the port's work at the vertical blank not counted below (the development switches' logging) | `openrac_game_vsync` |
| input | window events, the pads, the port's menus | `frontend::frame`, `frontend::between` |
| scene from memory | the tick's scene read from game memory: camera, mobys and their poses, lights, the 2D path's packets, particles; a level's geometry loaded | `frontend::frame` |
| renderers (cpu) | the CPU's time in each renderer (`Renderer::render`) | `draw_kept` |
| present | the debug menu, the picture put into the window, the overlay | `draw_kept` |
| swap / vsync | the buffer swap: with vsync on, mostly waiting for the display's refresh | `swapped` |
| between ticks | the frames drawn between ticks when the frame rate follows the display (all but their swaps and waits) | `main.cpp` |
| wait for tick | sleeping until the next tick is due | `main.cpp`, `swapped` |

The work of a tick is its length without the swaps and the waiting. While it
fits the tick's budget (20 ms in PAL) the game keeps its speed.

## Reading it

- **Bottleneck:** *none* with the time to spare, while the work fits with 5%
  left and fewer than one tick in twenty runs more than 10% late. Otherwise
  *gpu* when the renderers' GPU time fills more than 80% of the budget and
  outweighs their CPU time, else the stage that works longest. Averaged over
  the last 120 ticks.
- **Stages:** average and worst milliseconds per tick, and a bar of the
  average against the budget.
- **Renderers:** each renderer's CPU and GPU milliseconds per tick (timer
  queries, read back a few frames late so the GPU is never waited for).
- **Counts per tick:** frames drawn (more than one when frames are drawn
  between ticks), draw calls, triangles, image transfers from the game,
  textures converted to RGBA8 (each a cache miss: new pixels or a palette),
  disc sectors read, and calls to functions without C, in red when there are
  any. A function without C does nothing, so the game may be wrong there, and
  each call takes a lock.
- **Graph:** the last 240 ticks, their stages stacked (the wait left out), the
  yellow line the budget, a red mark over each hitch.
- **Hitches:** ticks longer than 1.25 budgets, with the stage that took longest
  and what else happened in them: disc reads, texture conversions, a level
  change.

The movies (`show_picture`) are not timed by stage; their time goes to game
code.

## Code

`port/game/common/perf.{h,cpp}` keeps the records and needs neither GL nor a
window (the headless program keeps them too); `perf_overlay.{h,cpp}` draws
them as one of the game's menu panels (its navy, khaki frame, light blue and
yellow) with the 5 x 7 font of `picture.{h,cpp}`, which the debug menu also
uses. The picture is made again every tenth frame and only then copied to the
GPU.
The renderer's timing is `Renderer::set_timing` and `Renderer::totals`
(`port/renderer/renderer.h`); the texture pool counts its transfers and
conversions (`TexturePool::counts`), the disc library its sectors
(`openrac_game_cd_sectors_read`), the runtime its calls to functions without C
(`openrac_guest_missing_calls`). Tests: `port/tests/perf_test.cpp`, and the
timed frames in `renderer_direct_test.cpp`.

None of it touches a decompilation: the game's code is timed from outside,
between vertical blanks, so it works whichever decompilation the port is built
from (`OPENRAC_RAC1_PAL_SOURCE`).
