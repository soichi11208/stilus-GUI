# stilus

Minimal, fully statically linkable C++20 GUI toolkit for Linux with zero dependencies on GUI-related external libraries. The Wayland backend implements the wire protocol directly without `libwayland-client`, and the X11 backend vendors and statically links XCB. The default build (`--cross-file cross-musl.txt`) uses musl for fully static linking — no dynamic dependencies, not even on libc. As long as the kernel ABI matches, it (probably) runs on any distro out of the box.

## Status

Work in progress. API is not yet stable.

## Documentation

- [**docs/gui.md**](docs/gui.md) — Guide for writing GUI apps (Japanese)
- [**docs/gui_en.md**](docs/gui_en.md) — Guide for writing GUI apps (English)

## Features

Backends: Wayland (hand-rolled protocol, no libwayland) and X11 (vendored + statically linked XCB). Auto-selected at runtime based on `WAYLAND_DISPLAY`.

Rendering: Software rasterizer (XRGB8888). Rectangles use analytic AA, rounded rects/circles use SDF, arbitrary paths use scanline fill with 8× vertical supersampling.

Transforms: Full 2D affine transform support (translate/scale/rotate/shear) via canvas transform stack.

Clip: Fast rectangular clip plus arbitrary path clip using 8-bit coverage masks.

HiDPI: Integer buffer scaling (`wl_surface.set_buffer_scale` / physical pixel buffer). Fractional scaling not yet supported.

Window decorations: Server-side decorations via `xdg-decoration-v1` if the compositor supports it (KDE/KWin). Falls back to client-side decorations (CSD) otherwise (GNOME/Mutter etc.), with self-drawn title bar and minimize/maximize/close buttons.

Text: `stb_truetype`-based glyph rendering, font fallback chain (missing codepoints in the primary font are looked up in fallback faces, e.g. CJK fonts), `.ttc` face selection, soft line breaking with basic line-start/line-end禁則 (kinsoku) rules.

IME: `zwp_text_input_v3` (preedit + commit) on Wayland.

Widgets: A small retained-mode widget tree built on top of the Canvas immediate-mode API (`src/widget.cpp`, `src/widgets.cpp`). Focus/Tab order support.

Linux-only by design. No plans for official Windows or macOS support in the foreseeable future. GPU rendering is not implemented. Arabic/Hebrew text support is welcome if someone wants to contribute.

## Build (default: fully static musl)

```sh
export PATH="$PWD/tools:$PATH"
meson setup build-musl --cross-file cross-musl.txt --buildtype=release
meson compile -C build-musl
```

Requirements: Meson, Ninja, and [zig](https://ziglang.org/) (tested with 0.16.0). Zig is used as a C/C++ cross-compiler for the `x86_64-linux-musl` target (`tools/zig-musl-{cc,cxx,ar,ranlib}` are thin wrappers). XCB is vendored under `third_party/xcb-musl` rebuilt for musl, so no system packages for `libXau`/`libXdmcp` are needed.

### Running examples

```sh
./build-musl/hello              # Minimal window
./build-musl/paint_demo         # Primitives + text
./build-musl/path_demo          # Arbitrary path fill
./build-musl/widget_demo        # Retained-mode widget tree
./build-musl/transform_demo     # Affine transforms + path clip + HiDPI
./build-musl/cjk_demo           # Font fallback + CJK line breaking
./build-musl/emoji_demo         # Color emoji (CBDT) rendering
```

### Tests

```sh
meson test -C build-musl --print-errorlogs    # Unit tests + golden images + font robustness
./build-musl/test_x11_integration             # Opens actual windows (needs display, uses xvfb-run in CI)
```

- `test_unit` — Window-independent pure logic tests (affine math, path flattening, damage tracking, font line breaking). No display needed.
- `test_golden` — Renders deterministic primitives off-screen and compares against reference images in `test/golden/*.ppm`. After intentional rendering changes, regenerate references with `--regen` (visually verify before committing).
- `test_font_robustness` — Resistance tests against corrupted/truncated fonts (stb_truetype doesn't do bounds checking, so we pin validation in `Font::from_memory`).
- `test_x11_integration` — Opens windows against an actual display. Runs via `xvfb-run` in CI.

CI (`.github/workflows/ci.yml`) runs three jobs on Ubuntu per push/PR: `musl-static` (default build), `build-and-test` (system libc), and `sanitizers` (ASan+UBSan).

### Alternative build: system libc (glibc)

If you don't want to use zig, or you want to link `meson install`'d libraries into existing glibc programs (musl-built `.a` can't be directly linked against glibc programs due to different libc internal ABIs):

```sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

XCB is still vendored and statically linked from `third_party/xcb`, but libstdc++/libc/libm remain dynamically linked (only `-static-libstdc++ -static-libgcc` is added). The only system packages needed are `libXau`/`libXdmcp` (X11 auth) that XCB transitively depends on.

## Installation

To install as a library linkable from other glibc programs, use the system libc build (`build`) — musl-built `.a` can't be directly linked into glibc programs:

```sh
meson setup build --prefix=/usr/local
meson compile -C build
meson install -C build
```

This installs `libstilus.a`, public headers under `stilus/`, vendored XCB static archives (under `<libdir>/stilus/` to avoid conflicts with system `libxcb`), and `stilus.pc` (pkg-config).

```sh
pkg-config --cflags --libs --static stilus
```

The musl build (`build-musl`) is intended for producing standalone fully static binaries for distribution — don't `meson install`, just copy the resulting executables.

## License / Dependencies

stilus itself is [WTFPL](LICENSE) v2 (Do What The Fuck You Want To Public License). Vendored third-party code:

- `third_party/stb/stb_truetype.h`, `stb_image.h` — Public Domain (dual-licensed MIT, see file headers)
- `third_party/xcb/` (glibc), `third_party/xcb-musl/` (musl) — [XCB](https://xcb.freedesktop.org/), MIT/X11 license
- The musl libc itself ([musl libc](https://musl.libc.org/), MIT license) is used via [zig](https://ziglang.org/) (Apache-2.0) bundled toolchain. Neither is included in this repository (only referenced at build time).

No copyleft dependencies (GPL/LGPL/MPL etc.) are used, and none will be added. This avoids the gray area of LGPL static linking with glibc by using musl (MIT-licensed) for fully static builds.

## Acknowledgements

Thanks to the authors of the third-party libraries stilus depends on.

### stb (Public Domain / MIT)

Font rasterization (`stb_truetype.h`) and image loading (`stb_image.h`) library.

- **Author:** Sean Barrett (2009–2024)
- **URL:** http://nothings.org/stb/
- **License:** Public Domain (Unlicense) or MIT License

### XCB — X C Binding (MIT)

Low-level protocol library for the X11 window system. Vendored as static archives.

- **Authors:** Bart Massey, Jamey Sharp, Josh Triplett (2001–2006)
- **URL:** https://xcb.freedesktop.org/
- **License:** MIT License

### musl libc (MIT)

C standard library used in fully static musl builds, bundled with the zig toolchain.

- **Author:** Rich Felker, et al.
- **URL:** https://musl.libc.org/
- **License:** MIT License

### zig (Apache-2.0)

Used as C/C++ toolchain for musl cross-compilation.

- **Author:** Andrew Kelley
- **URL:** https://ziglang.org/
- **License:** Apache License, Version 2.0

### X.Org Foundation (MIT / X11 License)

Copyright holder of libXau, libXdmcp (X11 auth libraries), transitively required by XCB.

- **URL:** https://xorg.freedesktop.org/
- **License:** X11 License (MIT-compatible)

## Repository layout

```
include/stilus/    Public API headers
src/               Implementation
  render/          Software rasterizer + canvas
  text/            Font loading, glyph cache, line breaking
  platform/
    wayland/       Hand-rolled Wayland protocol implementation
    x11/           XCB-based backend
  widget.cpp, widgets.cpp
                   Retained-mode widget tree
third_party/       Vendored stb + XCB (glibc and musl, both static)
tools/             Thin wrappers invoking zig as musl cross-compiler
cross-musl.txt     Meson cross file for fully static musl build (default)
examples/          Runnable demos
test/              Unit tests, golden image tests, font robustness tests, X11 integration smoke tests
```
