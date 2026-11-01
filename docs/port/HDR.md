# HDR output

The port can send its picture to an HDR screen. The game draws in SDR, for a television of 2002,
and every frame is an 8-bit picture already in the display's gamma (the world, the effects and the
HUD alike). HDR output shows that picture on an HDR screen with the game's white at a chosen
brightness ("paper white"), so it looks as it does on an SDR monitor, without the dim, washed-out
look a desktop in HDR mode gives SDR programs.

What goes above white comes from the game itself, the way RenoDX upgrades SDR games: while HDR is
shown the frame is drawn in half floats instead of bytes. Each draw's own colour is still clamped
to 0..1, as the 8-bit frame clamps it, so nothing the game draws changes by itself; only what
blending adds up (glows, explosions, lasers, sparkles laid over each other) can go past white,
where the 8-bit frame stopped at 255. That light rolls off towards the screen's peak. The HUD,
drawn with plain alpha, stays at paper white.

Off by default. With `hdr = off` nothing of it runs: the Vulkan instance, device and swapchain are
made as before and the picture is copied to the screen byte for byte as before.

## Settings

| Key | Values | |
|---|---|---|
| `hdr` | `off` (default), `auto`, `on` | `auto`: HDR when the system says the window's display is in HDR (Windows, macOS, Wayland; Android when the display shows HDR10). `on`: whenever the screen offers an HDR format, also where the system cannot say (X11) |
| `hdr_paper_white` | `0`, `80` to `1000` nits | The game's white. `0`: the system's SDR white (Windows' "SDR content brightness"), else 203 (ITU-R BT.2408) |
| `hdr_peak` | `0`, `400` to `10000` nits | The brightest the light above white may go, and the screen's metadata. `0`: what the display reports, else 1000 |

Turning `hdr` on from `off` counts from the next start (the Vulkan instance is made with the
colour-space extension only then). Between `auto` and `on`, and the two levels, changes apply at
once.

Where to set it: the game's Video options page (HDR), the options drawer (HDR, HDR paper white),
the settings file, and the launcher's Settings > HDR. The launcher's Game leaves it to the game;
Off, Auto or On is given to every start (`OPENRAC_HDR`) over the file's.

## Where it works

| | Renderer | Format | Detection (`auto`) |
|---|---|---|---|
| Windows | Vulkan | HDR10 (PQ, BT.2020), else scRGB | SDL (DXGI): HDR on, SDR white, peak |
| Linux, Wayland | Vulkan (Mesa or a driver with `VK_EXT_swapchain_colorspace` and a compositor with colour management) | HDR10, else scRGB | SDL (colour-management protocol): HDR on, headroom |
| Linux, X11 | Vulkan | where the driver offers it | none: `on` only |
| macOS | Vulkan (MoltenVK) | scRGB (EDR), else HDR10 | SDL: EDR headroom |
| Android | Vulkan | HDR10, else scRGB | `GameActivity.displayHdr` (JNI): `Display.isHdr` and HDR10 in its capabilities, the desired peak |
| any | OpenGL | SDR | |

OpenGL has no HDR output: SDL makes no HDR OpenGL surface on any platform. The OpenGL and
GLES routes (WGL float pixel formats, EGL `EXT_gl_colorspace_bt2020_pq`) are a later stage.

## How

- `renderer/hdr.h`: the arithmetic and the choices, with no GPU or SDL in it. A pixel's bytes go to
  linear light through a 2.2 gamma (what an sRGB monitor really shows; the piecewise sRGB curve
  would lift the game's dark scenes), are scaled to paper white, and for HDR10 turned into BT.2020
  primaries and the PQ curve (SMPTE ST 2084). For scRGB, 1.0 is 80 nits.
- Above white: the frame's values past 1 go on along the same 2.2 curve (RenoDX's gamma
  correction does the same) and roll off towards the peak, `1 + h (1 - e^(-(m - 1) / h))` on the
  brightest channel `m`, `h` the headroom over paper white; all three channels are scaled alike, so
  the hue stays. A pixel up to white is not touched. Below 0 (a subtraction in the float frame)
  is black.
- The HDR frame (`vkgl::hdr_frame`): the game's frame (`renderer/framebuffer.h`, `float_colour`)
  and the default framebuffer are RGBA16F while HDR is shown, 8-bit otherwise.
  `vkgl::clamp_colour_writes` marks the frame: its draws use each program's fragment stage made
  again with `clamp(colour, 0, 1)` at its end (`clamped_fragment`, compiled the first time it is
  needed), which is what an 8-bit target does to a fragment's colour before blending. Copies of
  the frame into the game's 8-bit textures (the frame read back as a texture, the menus'
  snapshot) clamp as before, so what the game reads back is unchanged.
- `renderer/vulkan/surface_format.h`: the swapchain's format. SDR picks what the backend always
  picked, and never an HDR colour space; HDR picks A2B10G10R10 in HDR10, or RGBA16F in extended
  linear sRGB, where the device can store to the format; else SDR.
- `renderer/vulkan/hdr_output.cpp` and `shaders/hdr_encode.comp`: one compute pass writes the
  default framebuffer into the swapchain image, in place of the SDR blit (the same scaling: NEAREST
  1:1, else linear). One full-screen pass a frame, only while HDR is shown. The screen is told
  BT.2020 primaries, the peak, MaxCLL = the peak and MaxFALL = paper white
  (`VK_EXT_hdr_metadata`, where offered).
- Where the swapchain's images cannot be storage images, the pass writes an image of its own in the
  same format, copied 1:1 into the screen (one more full-screen copy, only then).
- Android: SDL says nothing of HDR there, so `platform/display_hdr.h` asks the activity
  (`port/android/display_hdr.cpp`, `GameActivity.displayHdr`), and while HDR is sent the window
  asks for the display's HDR headroom (`Window.setColorMode(COLOR_MODE_HDR)`, Android 14 and
  later).
- `platform/window.cpp` reads SDL's window properties (`SDL_PROP_WINDOW_HDR_ENABLED_BOOLEAN`, the
  SDR white level and the HDR headroom); the frontend asks again on
  `SDL_EVENT_WINDOW_HDR_STATE_CHANGED` and when the window moves to another display.

## Tests

- `hdr`: PQ against ST 2084's published points, round trips, white at paper white, the roll-off
  above white (nothing up to white changes, never past the peak, hues kept), the levels and the
  output each mode asks for.
- `surface_format`: SDR as before (also with HDR colour spaces listed first), HDR's fallbacks.
- `vulkan_hdr`: the encode pass on lavapipe against `renderer/hdr.h`, every pixel, HDR10 and scRGB,
  the right way up, and scaled; and the HDR frame: a draw's colour clamped, two adds of 0.75 kept
  at 1.5, the same adds stopping at 1 in an 8-bit frame.

`OPENRAC_HDR_FRAME=1` draws the HDR frame without an HDR screen (shown clamped) and logs how much
of it lies above white every 60 frames, to compare its picture with the 8-bit frame's.

Known differences from the 8-bit frame, only while HDR is shown: a draw blended over a pixel
already above white (or below black, after a subtraction) starts from that value instead of the
clamped one; and alpha added up past 1 stays there for a later blend by the frame's alpha.

What no test here can show is a real HDR screen. On one: start with `hdr = on` and the Vulkan
renderer, check the log's `vulkan: HDR output HDR10 at ... nits`, and compare the menu's white and
a dark level against the same scene with `hdr = off` on the same screen in SDR mode.
