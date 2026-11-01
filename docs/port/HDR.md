# HDR output

The port can send its picture to an HDR screen. The game draws in SDR, for a television of 2002,
and every frame is an 8-bit picture already in the display's gamma (the world, the effects and the
HUD alike). HDR output shows that picture on an HDR screen
with the game's white at a chosen brightness ("paper white"), so it looks as it does on an SDR
monitor, without the dim, washed-out look a desktop in HDR mode gives SDR programs. On top of
that, highlights lifts the brightest parts (lamps, explosions, the sky) above paper white towards
the screen's peak, which is where an HDR screen looks different from SDR. With highlights at 0 the
picture is exactly the SDR one.

Off by default. With `hdr = off` nothing of it runs: the Vulkan instance, device and swapchain are
made as before and the picture is copied to the screen byte for byte as before.

## Settings

| Key | Values | |
|---|---|---|
| `hdr` | `off` (default), `auto`, `on` | `auto`: HDR when the system says the window's display is in HDR (Windows, macOS, Wayland; Android when the display shows HDR10). `on`: whenever the screen offers an HDR format, also where the system cannot say (X11) |
| `hdr_paper_white` | `0`, `80` to `1000` nits | The game's white. `0`: the system's SDR white (Windows' "SDR content brightness"), else 203 (ITU-R BT.2408) |
| `hdr_peak` | `0`, `400` to `10000` nits | The brightest the highlights may go, and the screen's metadata. `0`: what the display reports, else 1000 |
| `hdr_highlights` | `0` to `100` percent (default `50`) | How far what is near white is lifted above paper white towards the peak. `0`: exactly the SDR look |

Turning `hdr` on from `off` counts from the next start (the Vulkan instance is made with the
colour-space extension only then). Between `auto` and `on`, and the two levels, changes apply at
once.

Where to set it: the game's Video options page (HDR, HDR highlights), the options drawer (HDR,
HDR paper white, HDR highlights),
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
- Highlights (`lift_highlights`): a pixel's luminance above 0.5 of the game's white (about byte
  186) is lifted along a curve that starts flat there, up to `highlights` of the way from paper
  white to the peak at the game's white. All three channels are scaled alike, so hues stay. The
  game draws its HUD into the same picture, so white HUD text is lifted too.
- `renderer/vulkan/surface_format.h`: the swapchain's format. SDR picks what the backend always
  picked, and never an HDR colour space; HDR picks A2B10G10R10 in HDR10, or RGBA16F in extended
  linear sRGB, where the device can store to the format; else SDR.
- `renderer/vulkan/hdr_output.cpp` and `shaders/hdr_encode.comp`: one compute pass writes the
  default framebuffer into the swapchain image, in place of the SDR blit (the same scaling: NEAREST
  1:1, else linear). One full-screen pass a frame, only while HDR is shown. The screen is told
  BT.2020 primaries, the peak, and MaxCLL = the brightest highlight (`VK_EXT_hdr_metadata`, where
  offered).
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

- `hdr`: PQ against ST 2084's published points, round trips, white at paper white, the levels,
  the highlights curve (flat below the knee, rising, hues kept) and the output each mode asks for.
- `surface_format`: SDR as before (also with HDR colour spaces listed first), HDR's fallbacks.
- `vulkan_hdr`: the encode pass on lavapipe against `renderer/hdr.h`, every pixel, HDR10 and scRGB,
  with and without highlights, the right way up, and scaled.

What no test here can show is a real HDR screen. On one: start with `hdr = on` and the Vulkan
renderer, check the log's `vulkan: HDR output HDR10 at ... nits`, and compare the menu's white and
a dark level against the same scene with `hdr = off` on the same screen in SDR mode.
